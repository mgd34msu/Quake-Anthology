#include "internal.h"
#include "qa/binary.h"
#include "qa/q3_key.h"
#include "qa/audio_save.h"
#include "source_restore.h"
#include "capture.h"
#include "round.h"
#include "campaign.h"
#include "campaign_cinematic.h"
#include "qc_rerelease_events.h"
#include "qa/q3_assets_save.h"
#include "qa/material_library_save.h"
#include "qa/scene_resource_save.h"
#include "qa/font_save.h"
#include "qa/persistence_content.h"
#include "qa/application_q3_client.h"
#include "qa/application_character_selection.h"
#include "qa/console_cvar_observer.h"
#include "save_private.h"
#include "frame_time.h"
#include "audio_identity_save.h"
#include "network_q3_restart.h"
#include "equipment_source.h"
#include "config_store.h"
#include "qa/application_q3_factory.h"
#include <limits.h>
#include <stdio.h>

typedef struct frontend_source_lease frontend_source_lease;
struct frontend_source {
    frontend_source *next;
    qa_frontend *frontend;
    qa_application *application;
    qa_actor_owner owner;
    uint32_t seat,launch_seat;
    uint64_t identity;
    unsigned leases;
    size_t role_operations;
    frontend_source_lease *lease_list;
    frontend_source_lease *retired_leases;
    frontend_source_role_identity *restore_roles;
    size_t restore_role_count;
    bool constructed, construction_started;
    qa_vfs *mounts;
    const qa_vfs *source_files;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_font_library *fonts;
    qa_audio_bank *sounds;
    qa_audio_music *music;
    char *music_intro, *music_loop;
    bool music_looping;
    qa_media_library *movies;
    qa_q3_key *keys;
    frontend_key_profile *key_profile;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *presentation;
    qa_audio_listener listener;
    bool has_listener, music_attached;
};
struct frontend_source_lease {
    frontend_source_lease *next;
    frontend_source *source;
    qa_qvm_role role;
    uint64_t service_owner;
    qa_q3_host_common_services common;
    qa_console *console;
    qa_cvars *cvars;
    qa_command_context command;
    frontend_equipment_source *equipment;
    qa_application_q3_client_context time_context;
    const char *time_names[6];
    qa_cvar_observer_token time_owner_tokens[6], time_mirror_tokens[6];
    size_t time_count, time_busy;
    char *system_info;
    char *disconnect_reason;
    bool disconnect_pending;
    bool time_bound, released;
};
static void source_retry_retirement(frontend_source *);
static bool lease_dispose(frontend_source_lease *lease)
{
    if (lease->time_busy || !frontend_equipment_source_idle(lease->equipment)) return false;
    frontend_source_lease **link=&lease->source->retired_leases;
    while (*link && *link!=lease) link=&(*link)->next;
    if (*link!=lease) return false;
    qa_error error={0};
    if (!frontend_equipment_source_destroy(lease->equipment,&error)) return false;
    *link=lease->next;
    free(lease->system_info); free(lease->disconnect_reason); free(lease); return true;
}
static bool time_context_read(qa_frontend *f,qa_actor_owner owner,uint32_t seat,
    qa_application_q3_client_context *out,qa_error *error)
{
    return frontend_network_remote(f)?frontend_network_q3_client_context_read(f,owner,seat,out,error):
        qa_application_q3_client_context_read(f->application,owner,seat,out,error);
}
static bool time_current(const frontend_source_lease *lease)
{
    if (!lease || lease->released || lease->role!=QA_QVM_CGAME) return false;
    frontend_source *source=lease->source; qa_frontend *f=source->frontend;
    if (!source->application || source->application!=f->application || !source->leases ||
        lease->time_context.frontend_lifetime!=lease || lease->time_context.receiver!=source->owner ||
        lease->time_context.seat!=source->launch_seat || lease->time_context.service_owner!=lease->service_owner) return false;
    bool linked=false;
    for (const frontend_source_lease *row=source->lease_list;row;row=row->next) if (row==lease) { linked=true; break; }
    return linked && (frontend_network_remote(f)?frontend_network_q3_client_context_current(f,&lease->time_context):
        qa_application_q3_client_context_current(source->application,&lease->time_context));
}
static bool time_enter(frontend_source_lease *lease,qa_error *error)
{
    if (!time_current(lease) || lease->time_busy==SIZE_MAX || lease->source->role_operations==SIZE_MAX)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frame time role no longer owns its actual client lifetime");
    ++lease->time_busy; ++lease->source->role_operations; return true;
}
static void time_close(frontend_source_lease *lease)
{
    qa_cvars *owner=lease->time_context.client_time_cvars,*mirror=lease->time_context.cvars;
    for (size_t i=0;i<lease->time_count;++i) {
        if (lease->time_owner_tokens[i]) qa_cvars_unobserve(owner,lease->time_owner_tokens[i]);
        if (lease->time_mirror_tokens[i]) qa_cvars_unobserve(mirror,lease->time_mirror_tokens[i]);
        lease->time_owner_tokens[i]=lease->time_mirror_tokens[i]=0;
    }
    lease->time_bound=false;
}
static void time_leave(frontend_source_lease *lease)
{
    frontend_source *source=lease->source;
    --lease->time_busy; --source->role_operations;
    if (lease->released && !lease->time_busy) (void)lease_dispose(lease);
    source_retry_retirement(source);
}
static bool time_write(frontend_source_lease *lease,qa_cvars *registry,const char *name,const char *value,qa_error *error)
{
    if (!time_current(lease)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Frame time client retired before cvar publication");
    bool ok=qa_cvars_set(registry,name,value,true,error);
    return ok && (time_current(lease) || frontend_fail(error,QA_ERROR_ARGUMENT,"Frame time client retired during cvar publication"));
}
static bool time_refresh(frontend_source_lease *lease,bool subscriptions,qa_error *error)
{
    qa_cvars *owner=lease->time_context.client_time_cvars,*mirror=lease->time_context.cvars;
    if (!owner || owner==mirror) return true;
    bool ok=true; size_t suppressed=0;
    if (subscriptions) for (;ok && suppressed<lease->time_count;++suppressed)
        ok=qa_cvars_observer_suppress(mirror,lease->time_mirror_tokens[suppressed],true,error);
    for (size_t i=0;ok && i<lease->time_count;++i) {
        const qa_cvar_view *value=qa_cvars_find(owner,lease->time_names[i]);
        if (!value) { ok=frontend_fail(error,QA_ERROR_FORMAT,"Frame time subscribed source cvar was retired"); break; }
        /* The mirror publication can reenter the source owner. Copy the value
         * before publishing, so its retained registry text never goes stale. */
        size_t length=strlen(value->value); char *copy=length<SIZE_MAX?malloc(length+1):NULL;
        if (!copy) { ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining frame time publication value"); break; }
        memcpy(copy,value->value,length+1); ok=time_write(lease,mirror,lease->time_names[i],copy,error); free(copy);
    }
    if (time_current(lease)) for (size_t i=0;i<suppressed;++i) {
        qa_error cleanup={0};
        if (!qa_cvars_observer_suppress(mirror,lease->time_mirror_tokens[i],false,&cleanup)) {
            if (ok && error) *error=cleanup; ok=false;
        }
    }
    return ok;
}
static bool time_owner_published(void *context,qa_cvars *registry,const char *name,qa_error *error)
{
    frontend_source_lease *lease=context; (void)name;
    if (!time_enter(lease,error)) return false;
    bool ok=registry==lease->time_context.client_time_cvars && time_refresh(lease,true,error);
    time_leave(lease); return ok;
}
static bool time_mirror_published(void *context,qa_cvars *registry,const char *name,qa_error *error)
{
    frontend_source_lease *lease=context;
    if (!time_enter(lease,error)) return false;
    const qa_cvar_view *value=registry==lease->time_context.cvars?qa_cvars_find(registry,name):NULL;
    char *copy=NULL; bool ok=value!=NULL;
    if (ok) {
        size_t length=strlen(value->value); copy=length<SIZE_MAX?malloc(length+1):NULL;
        ok=copy!=NULL;
        if (ok) memcpy(copy,value->value,length+1);
        else frontend_fail(error,QA_ERROR_MEMORY,"Retaining client frame time publication");
    }
    if (ok) ok=time_write(lease,lease->time_context.client_time_cvars,name,copy,error);
    free(copy); time_leave(lease); return ok;
}
static size_t time_names(qa_console_dialect dialect,const char **names,bool collision)
{
    size_t count=0; names[count++]="timescale";
    if (dialect==QA_CONSOLE_Q1 || dialect==QA_CONSOLE_QW) names[count++]="host_framerate";
    else { names[count++]="fixedtime"; if (dialect==QA_CONSOLE_Q3) names[count++]="com_cameraMode"; }
    if (collision) { names[count++]="cm_noAreas"; names[count++]="cm_noCurves"; names[count++]="cm_playerCurveClip"; }
    return count;
}
static bool time_subscribe(frontend_source_lease *lease,bool restoring,qa_error *error)
{
    qa_cvars *owner=lease->time_context.client_time_cvars,*mirror=lease->time_context.cvars;
    if (!owner || owner==mirror) return true;
    if (qa_cvars_dialect(owner)!=qa_cvars_dialect(mirror))
        return frontend_fail(error,QA_ERROR_FORMAT,"Frame time mirror registries must retain the same actual dialect");
    if (!restoring) {
        const char *names[6]; size_t count=time_names(qa_cvars_dialect(owner),names,true);
        lease->time_count=0;
        for (size_t i=0;i<count;++i) {
            const qa_cvar_view *value=qa_cvars_find(owner,names[i]);
            if (!value) continue;
            lease->time_names[lease->time_count++]=names[i];
            if (!qa_cvars_register(mirror,names[i],value->reset_value,value->flags,lease->service_owner,
                "Shared source frame control",error) || !time_current(lease)) return false;
        }
        if (!time_refresh(lease,false,error)) return false;
    }
    for (size_t i=0;i<lease->time_count;++i) {
        if (!qa_cvars_find(owner,lease->time_names[i]) || !qa_cvars_find(mirror,lease->time_names[i]) ||
            !qa_cvars_observe(mirror,lease->time_names[i],lease->service_owner,time_mirror_published,lease,
                lease->time_mirror_tokens+i,error) ||
            !qa_cvars_observe(owner,lease->time_names[i],lease->service_owner,time_owner_published,lease,
                lease->time_owner_tokens+i,error)) { time_close(lease); return false; }
    }
    lease->time_bound=true; return true;
}
static bool equal_ascii(const char *text,size_t length,const char *name)
{
    if (strlen(name)!=length) return false;
    for (size_t i=0;i<length;++i) {
        unsigned a=(unsigned char)text[i],b=(unsigned char)name[i];
        if (a>='A' && a<='Z') a+='a'-'A';
        if (b>='A' && b<='Z') b+='a'-'A';
        if (a!=b) return false;
    }
    return true;
}
bool frontend_source_system_info(qa_frontend *f,const qa_application_q3_client_context *view,const char *info,qa_error *error)
{
    frontend_source_lease *lease=view?view->frontend_lifetime:NULL;
    /* Locate the token without dereferencing a caller-supplied pointer. */
    bool found=false;
    for (frontend_source *source=f?f->sources:NULL;source;source=source->next)
        for (frontend_source_lease *row=source->lease_list;row;row=row->next) if (row==lease) found=true;
    if (!found || !info || lease->role!=QA_QVM_CGAME || lease->source->frontend!=f)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"SystemInfo lacks its actual linked frontend client role");
    lease->time_context=*view;
    if (!time_enter(lease,error)) return false;
    size_t length=strlen(info); char *retained=length<SIZE_MAX?malloc(length+1):NULL;
    bool ok=retained!=NULL;
    if (!ok) frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual client SystemInfo continuation");
    if (ok) memcpy(retained,info,length+1);
    const char *names[6]; size_t name_count=view->client_time_cvars?time_names(qa_cvars_dialect(view->client_time_cvars),names,false):0;
    const char *cursor=info+(*info=='\\');
    while (ok && *cursor) {
        const char *separator=strchr(cursor,'\\'); if (!separator) break;
        const char *value=separator+1,*end=strchr(value,'\\'); if (!end) end=value+strlen(value);
        size_t name_length=(size_t)(separator-cursor),value_length=(size_t)(end-value);
        bool skip=!name_length || equal_ascii(cursor,name_length,"cl_allowdownload");
        for (size_t i=0;i<name_count;++i) if (equal_ascii(cursor,name_length,names[i])) skip=true;
        if (!skip && equal_ascii(cursor,name_length,"timescale") && lease->system_info && !strcmp(info,lease->system_info)) skip=true;
        if (!skip) {
            char *name=malloc(name_length+1),*copy=malloc(value_length+1);
            if (!name || !copy) ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining source SystemInfo field publication");
            else {
                memcpy(name,cursor,name_length); name[name_length]=0; memcpy(copy,value,value_length); copy[value_length]=0;
                ok=time_write(lease,view->cvars,name,copy,error);
            }
            free(name); free(copy);
        }
        cursor=*end?end+1:end;
    }
    if (ok) {
        free(lease->system_info); lease->system_info=retained; retained=NULL;
        if (view->client_time_cvars && view->client_time_cvars!=view->cvars) {
            if (lease->time_bound) ok=time_refresh(lease,true,error);
            else {
                lease->time_count=0;
                for (size_t i=0;i<name_count;++i) if (qa_cvars_find(view->client_time_cvars,names[i]))
                    lease->time_names[lease->time_count++]=names[i];
                ok=time_refresh(lease,false,error);
                lease->time_count=0;
            }
        }
    }
    free(retained); time_leave(lease); return ok;
}
bool frontend_source_times_sync(qa_frontend *f,bool restoring,qa_error *error)
{
    if (!f || !f->application || (restoring && !f->source_restoring))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Frame time binding requires its real frontend application");
    for (frontend_source *source=f->sources;source;source=source->next) {
        for (frontend_source_lease *lease=source->lease_list;lease;lease=lease->next) {
            if (lease->role!=QA_QVM_CGAME || lease->time_bound) continue;
            qa_application_q3_client_context view;
            if (!time_context_read(f,source->owner,source->launch_seat,&view,error)) return false;
            if (!view.initialized) continue;
            if (view.frontend_lifetime!=lease) return frontend_fail(error,QA_ERROR_FORMAT,"Frame time context has another physical frontend role");
            lease->time_context=view;
            if (!time_enter(lease,error)) return false;
            bool ok=time_subscribe(lease,restoring,error); time_leave(lease);
            if (!ok) return false;
        }
    }
    return true;
}
bool frontend_source_effect(void *context,qa_application *application,qa_actor_owner receiver,uint32_t seat,
    qa_application_q3_client_effect effect,const char *text,qa_error *error)
{
    qa_frontend *f=context; qa_application_q3_client_context view; uint32_t ordinal;
    if (!f || !application || application!=f->application || f->capture || f->source_restoring ||
        !text || !qa_application_constructor_seat_ordinal(application,receiver,seat,&ordinal,error) ||
        ordinal>=f->options.seats || !time_context_read(f,receiver,seat,&view,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 effect requires its actual frontend receiver lifetime");
    if (effect==QA_APPLICATION_Q3_SYSTEM_INFO) return frontend_source_system_info(f,&view,text,error);
    if (frontend_network_remote(f) && (effect==QA_APPLICATION_Q3_MAP_RESTART || effect==QA_APPLICATION_Q3_DISCONNECT))
        return frontend_network_q3_client_effect(f,&view,effect,text,error);
    frontend_source_lease *lease=NULL;
    for (frontend_source *source=f->sources;source;source=source->next)
        for (frontend_source_lease *row=source->lease_list;row;row=row->next)
            if (row==view.frontend_lifetime) lease=row;
    if (!lease || lease->role!=QA_QVM_CGAME)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 effect receiver has no linked frontend CGAME role");
    lease->time_context=view;
    if (!time_enter(lease,error)) return false;
    bool ok=false;
    switch (effect) {
    case QA_APPLICATION_Q3_MAP_RESTART: {
        /* Original/native source client owners have already cleared their
         * actual command ring. CGAME consumes the reliable restart itself.
         * Reset only the frontend's command builder's private input history. */
        frontend_seat *target=f->seats+ordinal;
        qa_movement_kind kind=target->builder.kind; qa_vec3 angles=target->builder.angles;
        qa_input_command_clear(&target->builder); target->builder.kind=kind; target->builder.angles=angles;
        ok=qa_q3_presentation_clear(lease->source->presentation,error);
        break;
    }
    case QA_APPLICATION_Q3_LEVEL_SHOT:
        ok=qa_seat_console_open(f->seats[ordinal].console,false,error) &&
            qa_tools_capture_levelshot(frontend_tools_owner(f),&view.command_context,error);
        break;
    case QA_APPLICATION_Q3_DISCONNECT: {
        size_t length=strlen(text); char *copy=length<SIZE_MAX?malloc(length+1):NULL;
        if (!copy) { frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual local client disconnect"); break; }
        memcpy(copy,text,length+1); free(lease->disconnect_reason); lease->disconnect_reason=copy;
        lease->disconnect_pending=true; ok=true;
        break;
    }
    default:
        frontend_fail(error,QA_ERROR_ARGUMENT,"Unknown frontend Q3 source effect");
        break;
    }
    ok=ok && (time_current(lease) || frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 effect retired its retained frontend receiver"));
    time_leave(lease); return ok;
}
bool frontend_source_drain(qa_frontend *f,qa_error *error)
{
    if (!f || !f->application || f->stepping || f->capture || !frontend_sources_idle(f))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Local client retirement requires returned frontend/source callbacks");
    for (;;) {
        frontend_source_lease *pending=NULL;
        for (frontend_source *source=f->sources;source && !pending;source=source->next)
            for (frontend_source_lease *lease=source->lease_list;lease;lease=lease->next)
                if (lease->disconnect_pending) { pending=lease; break; }
        if (!pending) return true;
        if (frontend_network_remote(f) || !time_current(pending))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Pending local client disconnect lost its actual receiver");
        /* Consume the attempt before Shutdown can reenter. A failed physical
         * teardown remains with the application's real retired descriptor;
         * enclosing destruction owns cleanup, never a fresh callback replay. */
        pending->disconnect_pending=false;
        qa_application_q3_client_context retained=pending->time_context;
        frontend_console_print(f,&retained.command_context,pending->disconnect_reason);
        frontend_console_print(f,&retained.command_context,"\n");
        if (!qa_application_q3_client_retire(f->application,&retained,error)) return false;
        /* The real host destructor has released the lease. Restart physical
         * traversal; neither the source group nor the pending pointer survives. */
    }
}

static double milliseconds(void *context)
{
    frontend_source *source = context;
    return (double)source->frontend->time_ns / 1000000.0;
}
static int32_t source_milliseconds(void *context) { return (int32_t)((uint64_t)milliseconds(context) & INT32_MAX); }
static int32_t frame_number(void *context)
{
    return (int32_t)(((frontend_source *)context)->frontend->frame_number & INT32_MAX);
}
static uint64_t audio_bus(void *context) { return ((frontend_source *)context)->identity; }
static void print_source(void *context, const char *text)
{
    frontend_source *source = context;
    qa_command_context command = {.origin = QA_COMMAND_SEAT, .seat = source->launch_seat, .owner = source->owner};
    frontend_console_print(source->frontend, &command, text);
}
static bool source_remap(void *context, const char *original, const char *replacement, float offset, qa_error *error)
{
    return frontend_shader_remap(((frontend_source *)context)->frontend, original, replacement, offset, error);
}
static bool source_actor(void *context, int32_t number, uint64_t *out, qa_error *error)
{
    frontend_source *source = context;
    if (!source->application || !source->leases)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "source actor lookup requires its live application lease");
    if (number < 0) { *out = QA_AUDIO_NO_ACTOR; return true; }
    qa_actor_id actor = {0};
    qa_error observed = {0};
    if (!qa_application_guest_source_actor(source->application, source->owner,
            source->launch_seat, number, &actor, &observed)) {
        if (observed.code == QA_ERROR_NOT_FOUND) { *out = QA_AUDIO_NO_ACTOR; return true; }
        if (error) *error = observed;
        return false;
    }
    *out = frontend_audio_actor(source->frontend, actor, error);
    return *out != QA_AUDIO_NO_ACTOR;
}
static bool listener(void *context, const qa_audio_listener *value, qa_error *error)
{
    frontend_source *source = context; (void)error;
    source->listener = *value;
    source->listener.gain = 1.0f / (float)source->frontend->options.seats;
    source->has_listener = true;
    return true;
}
static bool music(void *context, const char *intro_name, const char *loop_name, qa_error *error)
{
    frontend_source *source = context;
    if (!source->frontend->audio) return frontend_fail(error, QA_ERROR_UNSUPPORTED, "source music output is disabled");
    if (source->music_attached) {
        source->music = qa_audio_engine_bus_music(source->frontend->audio, source->identity);
        source->music_attached = source->music != NULL;
    }
    if (!source->music && !qa_audio_music_create(qa_audio_engine_rate(source->frontend->audio), QA_AUDIO_Q3, false, &source->music, error)) return false;
    const char *requested_loop=loop_name?loop_name:"";
    if (intro_name && source->music_intro && source->music_loop && source->music_looping &&
        !strcmp(source->music_intro,intro_name) && !strcmp(source->music_loop,requested_loop) &&
        qa_audio_music_playing(source->music)) return true;
    qa_audio_music_stop(source->music);
    free(source->music_intro); free(source->music_loop);
    source->music_intro=source->music_loop=NULL; source->music_looping=false;
    if (!intro_name || !*intro_name) return true;
    size_t intro_length=strlen(intro_name),loop_length=strlen(requested_loop);
    source->music_intro=intro_length<SIZE_MAX?malloc(intro_length+1):NULL;
    source->music_loop=loop_length<SIZE_MAX?malloc(loop_length+1):NULL;
    if (!source->music_intro || !source->music_loop) {
        free(source->music_intro); free(source->music_loop); source->music_intro=source->music_loop=NULL;
        return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual source music selection");
    }
    memcpy(source->music_intro,intro_name,intro_length+1); memcpy(source->music_loop,requested_loop,loop_length+1);
    source->music_looping=true;
    qa_audio_stream *intro = NULL, *loop = NULL;
    if (!qa_audio_bank_music_cue(source->sounds, intro_name, QA_AUDIO_Q3, NULL, NULL, &intro, error)) return false;
    if (!intro) return true;
    loop=intro;
    if (loop_name && *loop_name) {
        if (!strcmp(intro_name, loop_name)) loop = intro;
        else if (!qa_audio_bank_music_cue(source->sounds, loop_name, QA_AUDIO_Q3, NULL, NULL, &loop, error)) {
            qa_audio_stream_close(intro); return false;
        }
    }
    source->music_looping=loop!=NULL;
    qa_audio_music_start(source->music, intro, loop);
    bool ok = qa_audio_engine_music(source->frontend->audio, source->identity, source->seat, 1, source->music, error);
    if (ok) source->music_attached = true;
    return ok;
}
bool frontend_source_prepare_scene(qa_frontend *frontend,qa_application *application,uint32_t seat,
    const qa_q3_refdef *definition,qa_q3_scene_options *options,qa_error *error)
{
    if (!frontend || !application || application!=frontend->application || !definition || !options ||
        seat>=frontend->options.seats)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source scene preparation requires its actual application and physical seat");
    qa_application_map_view map;
    if (qa_application_map_read(application, &map)) {
        const qa_launch_snapshot *publication=qa_application_launch(application);
        const qa_product *product = qa_catalog_product(qa_launch_snapshot_catalog(publication), map.geometry);
        options->world_family = product && product->family == QA_GAME_Q2 ? QA_SCENE_Q2 :
            product && product->family == QA_GAME_Q3 ? QA_SCENE_Q3 : QA_SCENE_Q1;
    }
    options->split_screen = frontend->options.seats > 1;
    return frontend_tools_camera(frontend,seat,options->world.view.clip_enabled,&options->world.view,error) &&
        (options->world.no_world || frontend_event_world(frontend,seat,&options->world,error));
}
bool frontend_source_submit_scene(qa_frontend *frontend,uint32_t seat,qa_actor_owner owner,
    const qa_q3_scene_options *options,qa_scene_frame *frame,qa_error *error)
{
    if (!frontend || seat>=frontend->options.seats || !owner || !options || frame!=&frontend->frame)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source scene submission requires its actual owner, frame and physical seat");
    if (options->world.no_world) return true;
    return frontend_visuals_submit(frontend,seat,owner,&options->world,frame,error) &&
        frontend_particle_draw(frontend,&options->world.view,error) &&
        frontend_event_debug(frontend,&options->world.view,error) &&
        frontend_tools_debug(frontend,&options->world.view,error);
}
static bool prepare_view(void *context,const qa_q3_refdef *definition,qa_q3_scene_options *options,qa_error *error)
{
    frontend_source *source=context;
    if (!source->application || !source->leases)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source view preparation requires its live application lease");
    if (!frontend_source_prepare_scene(source->frontend,source->application,source->seat,definition,options,error)) return false;
    for (frontend_source_lease *lease=source->lease_list;lease;lease=lease->next)
        if (lease->equipment && !frontend_equipment_source_prepare_view(lease->equipment,definition,options,error)) return false;
    return true;
}
static bool submit_view(void *context,const qa_q3_scene_options *options,qa_scene_frame *frame,qa_error *error)
{
    frontend_source *source=context;
    if (!frontend_source_submit_scene(source->frontend,source->seat,source->owner,options,frame,error)) return false;
    for (frontend_source_lease *lease=source->lease_list;lease;lease=lease->next)
        if (lease->equipment && !frontend_equipment_source_submit(lease->equipment,options,frame,error)) return false;
    return true;
}
static void scene_cleared(void *context)
{
    frontend_source *source=context;
    for (frontend_source_lease *lease=source->lease_list;lease;lease=lease->next)
        frontend_equipment_source_clear(lease->equipment);
}
static void float_word(uint8_t *out, float value)
{
    uint32_t bits; memcpy(&bits, &value, sizeof(bits)); qa_store_u32le(out, bits);
}
static bool configuration(void *context, uint8_t out[11332], qa_error *error)
{
    frontend_source *source = context;
    qa_frontend *frontend = source->frontend;
    qa_display_info display;
    if (!qa_display_info_get(frontend->display, &display, error)) return false;
    memset(out, 0, 11332);
    const qa_gl_capabilities *caps = qa_gl_capabilities_get(frontend->gl);
    snprintf((char *)out, 1024, "%s", caps ? caps->renderer : "Quake Anthology CPU renderer");
    snprintf((char *)out + 1024, 1024, "%s", caps ? caps->vendor : "Quake Anthology");
    snprintf((char *)out + 2048, 1024, "%s", caps ? caps->version : "retained scene renderer");
    /* CPU textures use unsigned dimensions; the source ABI advertises the
     * signed representable limit. Its color output is RGBA8 and depth float. */
    qa_store_u32le(out + 11264, caps ? caps->maximum_texture_size : INT32_MAX);
    qa_store_u32le(out + 11268, caps ? caps->texture_units : 1);
    qa_store_u32le(out + 11272, caps ? caps->color_bits : 32);
    qa_store_u32le(out + 11276, caps ? caps->depth_bits : 64);
    qa_store_u32le(out + 11280, caps ? caps->stencil_bits : 8);
    qa_store_u32le(out + 11304, display.drawable_width);
    qa_store_u32le(out + 11308, display.drawable_height);
    float_word(out + 11312, display.drawable_height ? (float)display.drawable_width / (float)display.drawable_height : 1);
    qa_store_u32le(out + 11316, display.refresh_rate > 0 ? (uint32_t)display.refresh_rate : 0);
    qa_store_u32le(out + 11320, display.fullscreen != QA_DISPLAY_WINDOWED);
    qa_store_u32le(out + 11324, caps && caps->stereo);
    return true;
}
static bool update_screen(void *context, qa_error *error)
{
    qa_frontend *frontend = ((frontend_source *)context)->frontend;
    if (frontend->cpu) return qa_cpu_execute(frontend->cpu, &frontend->frame, error) && qa_cpu_present_frame(frontend->cpu, error);
    return qa_gl_execute(frontend->gl, &frontend->frame, error) && qa_gl_finish(frontend->gl, error) && qa_display_swap(frontend->display, error);
}
static void common_print(void *context, const char *text)
{
    frontend_source_lease *lease = context;
    qa_console *console = lease->console;
    qa_console *shared = qa_application_console(lease->source->application);
    if (console && qa_console_output_redirected(console)) qa_console_emit(console, &lease->command, text);
    else if (shared && qa_console_output_redirected(shared)) qa_console_emit(shared, &lease->command, text);
    else print_source(lease->source, text);
}
static uint32_t common_milliseconds(void *context)
{
    frontend_source_lease *lease = context;
    if (frontend_network_remote(lease->source->frontend))
        return (uint32_t)((lease->source->frontend->time_ns / UINT64_C(1000000)) & UINT32_MAX);
    return lease->common.milliseconds ? lease->common.milliseconds(lease->common.context) :
        (uint32_t)((uint64_t)milliseconds(lease->source) & UINT32_MAX);
}
static int32_t common_calendar(void *context, qa_q3_host_calendar *out)
{
    frontend_source_lease *lease = context;
    return lease->common.calendar ? lease->common.calendar(lease->common.context, out) : 0;
}
static bool common_arguments(void *context, qa_native_host_command_view *out, qa_error *error)
{
    frontend_source_lease *lease = context;
    return lease->common.arguments ? lease->common.arguments(lease->common.context, out, error) :
        frontend_fail(error, QA_ERROR_UNSUPPORTED, "source argument owner is unavailable");
}
static bool common_command(void *context, const char *text, qa_error *error)
{
    frontend_source_lease *lease = context;
    if (frontend_network_remote(lease->source->frontend))
        return frontend_network_client_command(lease->source->frontend, text, error);
    return lease->common.client_command ? lease->common.client_command(lease->common.context, text, error) :
        frontend_fail(error, QA_ERROR_UNSUPPORTED, "source client command route is unavailable");
}
static bool common_mods(void *context, qa_vfs_listing *out, qa_error *error)
{
    frontend_source_lease *lease = context;
    return lease->common.installed_mods ? lease->common.installed_mods(lease->common.context, out, error) :
        frontend_fail(error, QA_ERROR_UNSUPPORTED, "source installed mod listing owner is unavailable");
}
static bool common_clipboard(void *context, qa_buffer *out, qa_error *error)
{
    (void)context;
    char *text = SDL_GetClipboardText();
    if (!text) { qa_error_set(error, QA_ERROR_IO, 0, "reading clipboard: %s", SDL_GetError()); return false; }
    size_t length = strlen(text); uint8_t *copy = malloc(length + 1);
    if (!copy) { SDL_free(text); return frontend_fail(error, QA_ERROR_MEMORY, "retaining clipboard text"); }
    memcpy(copy, text, length + 1); SDL_free(text); *out = (qa_buffer){copy, length}; return true;
}
static bool source_idle(const frontend_source *source)
{
    if (!source || source->role_operations) return false;
    for (const frontend_source_lease *lease=source->lease_list;lease;lease=lease->next)
        if (lease->time_busy || !frontend_equipment_source_idle(lease->equipment) ||
            (lease->time_context.cvars && !qa_cvars_observer_idle(lease->time_context.cvars)) ||
            (lease->time_context.client_time_cvars && !qa_cvars_observer_idle(lease->time_context.client_time_cvars))) return false;
    for (const frontend_source_lease *lease=source->retired_leases;lease;lease=lease->next)
        if (lease->time_busy || !frontend_equipment_source_idle(lease->equipment)) return false;
    return (!source->presentation || qa_q3_presentation_idle(source->presentation)) &&
        (!source->assets || qa_q3_assets_idle(source->assets)) &&
        (!source->fonts || qa_font_library_idle(source->fonts)) &&
        (!source->materials || qa_material_library_idle(source->materials)) &&
        (!source->images || qa_scene_resources_idle(source->images)) &&
        (!source->frontend->audio || qa_audio_engine_round_ready(source->frontend->audio,NULL));
}
bool frontend_sources_idle(const qa_frontend *frontend)
{
    if (!frontend) return false;
    for (const frontend_source *source=frontend->sources;source;source=source->next)
        if (!source_idle(source)) return false;
    return true;
}
static bool source_free(frontend_source *source)
{
    if (!source) return true;
    qa_frontend *frontend = source->frontend;
    if (frontend->capture || source->retired_leases || !source_idle(source)) return false;
    qa_error error = {0};
    if (source->key_profile) {
        if (!frontend_key_profile_release(source->key_profile,&error)) return false;
        source->key_profile=NULL; source->keys=NULL;
    }
    if (source->presentation && !qa_q3_presentation_destroy(source->presentation, &error)) {
        fprintf(stderr, "source presentation retirement: %s\n", error.message);
        return false;
    }
    source->presentation=NULL;
    if (frontend->audio) {
        qa_audio_engine_remove_music(frontend->audio, source->identity);
        if (!qa_audio_engine_stop_owner(frontend->audio, source->identity, source->seat, &error))
            fprintf(stderr, "source audio retirement: %s\n", error.message);
    }
    qa_q3_presentation_assets_destroy(source->assets);
    qa_q3_key_destroy(source->keys);
    source->keys=NULL;
    if (!source->music_attached) qa_audio_music_destroy(source->music);
    qa_media_library_destroy(source->movies);
    qa_font_library_destroy(source->fonts);
    qa_audio_bank_destroy(source->sounds);
    qa_material_library_destroy(source->materials);
    qa_scene_resources_destroy(source->images);
    qa_vfs_destroy(source->mounts); free(source->music_intro); free(source->music_loop);
    free(source->restore_roles); free(source); return true;
}
static void release_source(void *context)
{
    frontend_source_lease *lease = context; frontend_source *source = lease->source;
    frontend_source_lease **held=&source->lease_list;
    while (*held && *held!=lease) held=&(*held)->next;
    if (*held) *held=lease->next;
    time_close(lease); lease->released=true;
    lease->next=source->retired_leases; source->retired_leases=lease;
    if (!--source->leases) {
        source->application = NULL;
        source->source_files = NULL;
    }
    source_retry_retirement(source);
}
static void source_retry_retirement(frontend_source *source)
{
    frontend_source_lease *retired=source->retired_leases;
    while (retired) {
        frontend_source_lease *next=retired->next;
        (void)lease_dispose(retired); retired=next;
    }
    if (source->leases || source->frontend->source_restoring || source->frontend->capture || !source_idle(source)) return;
    frontend_source **link = &source->frontend->sources;
    while (*link && *link != source) link = &(*link)->next;
    frontend_source *next=source->next;
    if (source_free(source) && *link) *link=next;
}
static bool construct_source(frontend_source *source, const qa_q3_host_options *host,
    frontend_key_profile *profile,bool restoring, qa_error *error)
{
    qa_frontend *frontend = source->frontend;
    source->construction_started = true;
    if (profile) {
        if (!frontend_key_profile_state(profile) || !frontend_key_profile_retain(profile,error)) return false;
        source->key_profile=profile; source->keys=frontend_key_profile_state(profile);
    }
    if (restoring) {
        if (!source->mounts || source->source_files != host->mounts)
            return frontend_fail(error, QA_ERROR_FORMAT, "source construction changed its preloaded content graph");
    } else {
        source->source_files = host->mounts;
        source->mounts = qa_vfs_clone(host->mounts, error);
    }
    source->images = source->mounts ? restoring ?
        qa_scene_resources_create_detached(source->mounts, error) : qa_scene_resources_create(source->mounts, error) : NULL;
    source->materials = source->images ? restoring ?
        qa_material_library_create_detached(source->images, error) :
        qa_material_library_create(source->images, frontend->order, error) : NULL;
    source->fonts = source->images ? qa_font_library_create(source->mounts, source->images, error) : NULL;
    source->movies = source->images ? qa_media_library_create(source->images, error) : NULL;
    qa_scene_image_options images = {.family = QA_SCENE_Q3, .wrap = QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true, .transparent_index = -1};
    bool ok = source->mounts && source->images && source->materials && source->fonts && source->movies &&
        (restoring || qa_material_library_load_scripts(source->materials, source->mounts, &images, error)) &&
        qa_audio_bank_create(source->mounts, &source->sounds, error) &&
        (profile || qa_q3_key_create(host->cvars, false, &source->keys, error));
    if (ok && frontend->audio) ok = qa_audio_music_create(qa_audio_engine_rate(frontend->audio), QA_AUDIO_Q3, false, &source->music, error);
    if (ok && !restoring) ok = frontend_material_remaps(frontend, source->materials, error);
    qa_q3_presentation_asset_options assets = {.provider = {source->mounts, source->images, source->materials, QA_SCENE_Q3},
        .sounds = source->sounds, .movies = source->movies, .context = source, .print = print_source};
    if (ok) ok = qa_q3_presentation_assets_create(&assets, &source->assets, error);
    qa_q3_presentation_options presentation = {.assets = source->assets, .audio = frontend->audio,
        .clock = {source, milliseconds}, .seat = source->seat, .owner = source->identity,
        .viewport = {0, 0, frontend->width, frontend->height}, .near_clip = 4, .far_clip = 16384,
        .identity_light = 1, .lod_scale = 5, .rail_core_width = 6, .rail_ring_width = 16, .rail_segment_length = 32,
        .context = source, .audio_actor = source_actor, .listener = listener, .music = music,
        .frame_number = frame_number, .milliseconds = source_milliseconds, .audio_bus = audio_bus,
        .prepare_view = prepare_view, .submit_view = submit_view, .scene_cleared=scene_cleared,
        .remap = source_remap, .print = print_source};
    if (ok) ok = qa_q3_presentation_create(&presentation, &source->presentation, error);
    source->constructed = ok;
    return ok;
}
bool frontend_source_identity_allocate(qa_frontend *frontend,uint64_t *out,qa_error *error)
{
    if (!frontend || !out || frontend->capture || frontend->source_restoring || frontend->round ||
        frontend->next_source_id >= UINT64_MAX-QA_FRONTEND_COMMAND_OWNER-1)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source frontend identity requires its genuine constructor namespace");
    *out=QA_FRONTEND_COMMAND_OWNER+(++frontend->next_source_id); return true;
}
static bool create_source(qa_frontend *frontend, qa_application *application, qa_actor_owner owner,
    uint32_t seat,uint32_t launch_seat, const qa_q3_host_options *host,frontend_key_profile *profile,
    frontend_source **out, qa_error *error)
{
    frontend_source *source = calloc(1, sizeof(*source));
    if (!source) return frontend_fail(error, QA_ERROR_MEMORY, "allocating source presentation owner");
    source->frontend = frontend; source->application = application; source->owner = owner; source->seat = seat;
    source->launch_seat=launch_seat;
    if (!frontend_source_identity_allocate(frontend,&source->identity,error)) { free(source); return false; }
    source->next=frontend->sources; frontend->sources=source;
    if (!construct_source(source,host,profile,false,error)) {
        frontend_source *next=source->next;
        if (source_free(source)) frontend->sources=next;
        return false;
    }
    *out=source;
    return true;
}
bool frontend_source_services(void *context, qa_application *application, qa_actor_owner owner,
    qa_qvm_role role, uint32_t seat, qa_q3_host_options *host, qa_error *error)
{
    qa_frontend *frontend = context;
    if (!frontend || !frontend_owners_idle(frontend))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source construction requires idle frontend parent and child owners");
    if (role == QA_QVM_GAME) return frontend_network_source_services(frontend, host, error);
    uint32_t ordinal;
    if (frontend->options.dedicated || !qa_application_constructor_seat_ordinal(application,owner,seat,&ordinal,error) ||
        ordinal >= frontend->options.seats)
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "source client presentation requires an active local seat");
    qa_application_q3_client_preparation preparation;
    if (!qa_application_q3_preconstruction_source_read(application,owner,role,seat,&preparation,error)) return false;
    frontend_config_source *configured=preparation.source_console?
        frontend_config_store_source(frontend->config_store,preparation.source_console):NULL;
    frontend_key_profile *profile=configured?frontend_config_source_keys(configured):NULL;
    if (configured && (frontend_config_source_cvars(configured)!=preparation.source_cvars || !profile ||
        !frontend_key_profile_state(profile)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client keys require the prepared actual GAME profile and registry");
    if (frontend->source_restoring && (!frontend->seats || !frontend->seats[ordinal].input || !frontend->seats[ordinal].console))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "restored source imports require actual stable input and console owners");
    if ((!frontend->source_restoring && !frontend_scene_sync(frontend, error)) ||
        !frontend_network_client_services(frontend, application, owner, role, seat, host, error)) return false;
    if (role == QA_QVM_CGAME && !frontend_network_remote(frontend) && !host->client.gamestate)
        host->client_time_from_game = true;
    frontend_source *source = frontend->sources;
    if (frontend->source_restoring) {
        for (; source; source = source->next) {
            bool found = false;
            for (size_t i = 0; i < source->restore_role_count; ++i)
                if (source->restore_roles[i].service_owner == host->service_owner) { found = true; break; }
            if (found) break;
        }
        if (!source || source->owner != owner || source->seat != ordinal || source->launch_seat!=seat || !host->service_owner)
            return frontend_fail(error, QA_ERROR_FORMAT, "source factory leaves admitted restored group identity");
        bool admitted = false;
        for (size_t i = 0; i < source->restore_role_count; ++i)
            if (source->restore_roles[i].service_owner == host->service_owner && source->restore_roles[i].role == role)
                admitted = true;
        if (!admitted || (source->source_files && source->source_files != host->mounts))
            return frontend_fail(error, QA_ERROR_FORMAT, "source role or mount view differs from its restored group");
        for (frontend_source_lease *prior = source->lease_list; prior; prior = prior->next)
            if (prior->service_owner == host->service_owner)
                return frontend_fail(error, QA_ERROR_FORMAT, "duplicate restored source role lease");
        if (source->construction_started && !source->constructed)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "partially constructed source group must be discarded");
        if (frontend->application != application || (source->application && source->application != application))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "restored source factory changed its application owner");
        source->application = application;
        if (!source->constructed && !construct_source(source, host,profile,true,error)) return false;
    } else {
        while (source && (source->owner != owner || source->seat != ordinal || source->launch_seat!=seat || source->source_files != host->mounts)) source = source->next;
    }
    bool created = source == NULL;
    if (created && !create_source(frontend, application, owner, ordinal,seat, host,profile,&source,error)) return false;
    if (source->key_profile!=profile)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client role changed its genuine shared GAME key profile");
    frontend_source_lease *lease = malloc(sizeof(*lease));
    if (!lease || source->leases == UINT_MAX) {
        free(lease);
        if (created) {
            frontend_source *next=source->next;
            if (source_free(source)) frontend->sources=next;
        }
        return frontend_fail(error, QA_ERROR_MEMORY, "retaining source frontend lease");
    }
    *lease = (frontend_source_lease){.source = source,.role=role,.service_owner=host->service_owner,.common = host->common,
        .console = host->console,.cvars=host->cvars, .command = host->command_context};
    frontend_source_lease **link = &source->lease_list;
    if (frontend->source_restoring) {
        size_t rank = 0;
        while (source->restore_roles[rank].service_owner != lease->service_owner) ++rank;
        while (*link) {
            size_t prior = 0;
            while (source->restore_roles[prior].service_owner != (*link)->service_owner) ++prior;
            if (prior > rank) break;
            link = &(*link)->next;
        }
    }
    lease->next = *link; *link = lease; ++source->leases;
    host->frontend_lifetime = lease; host->release_frontend = release_source;
    host->seat = frontend->seats[ordinal].input;
    host->console_field = qa_seat_console_field(frontend->seats[ordinal].console, false);
    host->keys = source->keys;
    host->scene_resources = source->images; host->scene_frame = &frontend->frame;
    host->scene_world = frontend->scene_world; host->sound_bank = source->sounds;
    host->presentation = (qa_q3_host_presentation_services){source, source->presentation,
        source->fonts, configuration, update_screen};
    host->common = (qa_q3_host_common_services){lease, common_print, common_milliseconds,
        common_calendar, host->common.arguments ? common_arguments : NULL,
        (host->common.client_command || frontend_network_remote(frontend)) ? common_command : NULL, host->common.installed_mods ? common_mods : NULL,
        common_clipboard};
    if (frontend->source_restoring) return true;
    return frontend_source_publish_world(frontend, error) &&
        qa_q3_presentation_frame(source->presentation, &frontend->frame,
            (qa_scene_rect){0, 0, frontend->width, frontend->height}, error);
}
static bool equipment_current(void *context,const qa_application_q3_client_context *view)
{
    const frontend_source_lease *lease=context;
    const frontend_source *source=lease?lease->source:NULL;
    if (!source || !view || lease->released || lease->role!=QA_QVM_CGAME ||
        source->application!=source->frontend->application || view->frontend_lifetime!=lease ||
        view->receiver!=source->owner || view->seat!=source->launch_seat ||
        view->service_owner!=lease->service_owner) return false;
    bool linked=false;
    for (const frontend_source_lease *row=source->lease_list;row;row=row->next)
        if (row==lease) { linked=true; break; }
    return linked && (frontend_network_remote(source->frontend)?
        frontend_network_q3_client_context_current(source->frontend,view):
        qa_application_q3_client_context_current(source->application,view));
}
static bool equipment_borrow(void *context,qa_application_q3_client_context *out,qa_error *error)
{
    frontend_source_lease *lease=context;
    if (!lease || !out || lease->released || lease->role!=QA_QVM_CGAME ||
        lease->time_busy==SIZE_MAX || lease->source->role_operations==SIZE_MAX)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Equipment requires its retained actual CGAME lease");
    frontend_source *source=lease->source;
    qa_application_q3_client_context view;
    if (!time_context_read(source->frontend,source->owner,source->launch_seat,&view,error)) return false;
    if (!equipment_current(lease,&view))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Equipment source context no longer owns its CGAME lease");
    ++lease->time_busy; ++source->role_operations; *out=view; return true;
}
static void equipment_release(void *context) { time_leave(context); }
bool frontend_source_client_prepare(void *context,qa_application *application,
    const qa_application_q3_client_preparation *preparation,qa_error *error)
{
    qa_frontend *f=context;
    qa_q3_host_options *host=preparation?preparation->services:NULL;
    frontend_source_lease *lease=NULL;
    for (frontend_source *source=f?f->sources:NULL;source;source=source->next)
        for (frontend_source_lease *row=source->lease_list;row;row=row->next)
            if (host && row==host->frontend_lifetime) lease=row;
    if (!f || !application || application!=f->application || !preparation || !host || !lease ||
        lease->released || lease->source->application!=application || lease->source->frontend!=f ||
        lease->source->owner!=preparation->receiver || lease->source->launch_seat!=preparation->seat ||
        lease->role!=preparation->role || lease->service_owner!=host->service_owner ||
        host->presentation.seat!=lease->source->presentation)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client preparation requires its actual constructed frontend role lease");
    if (preparation->role!=QA_QVM_CGAME) return true;
    if (!preparation->equipment_services || lease->equipment)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Equipment preparation requires its fresh actual CGAME output");
    frontend_equipment_source_options options={.frontend=f,.receiver=preparation->receiver,
        .seat=preparation->seat,.assets=lease->source->assets,.presentation=lease->source->presentation,
        .lease=lease,.borrow=equipment_borrow,.current=equipment_current,.release=equipment_release};
    if (!frontend_equipment_source_create(&options,&lease->equipment,error)) return false;
    frontend_equipment_source_services(lease->equipment,preparation->equipment_services);
    return true;
}
bool frontend_source_remap(qa_frontend *frontend, const char *original, const char *replacement, float offset, qa_error *error)
{
    for (frontend_source *source = frontend->sources; source; source = source->next)
        if (!qa_material_remap(source->materials, original, replacement, offset, error)) return false;
    return true;
}
qa_q3_presentation_assets *frontend_source_assets(qa_frontend *frontend, const qa_command_context *context)
{
    if (!frontend || !context) return NULL;
    qa_actor_owner owner = (qa_actor_owner)context->owner;
    if (!context->owner) {
        qa_application_presentation_view active;
        if (!qa_application_presentation_read(frontend->application, context->seat, &active)) return NULL;
        owner = active.source_hud ? active.hud : active.source_menu ? active.menu : 0;
    } else if (context->owner > UINT32_MAX) return NULL;
    if (!owner) return NULL;
    qa_command_context captured = *context;
    captured.owner = owner;
    qa_vfs *files = qa_application_context_files(frontend->application, &captured, NULL);
    if (!files) return NULL;
    for (frontend_source *source = frontend->sources; source; source = source->next)
        if (source->owner == owner && source->launch_seat == context->seat && source->source_files == files) return source->assets;
    return NULL;
}
const qa_scene_resources *frontend_source_images_at(qa_frontend *frontend, size_t index)
{
    if (!frontend) return NULL;
    frontend_source *source = frontend->sources;
    while (source && index) { source = source->next; --index; }
    return source ? source->images : NULL;
}
bool frontend_source_rebind_ready(const qa_frontend *owned, const qa_frontend *destination, qa_error *error)
{
    if (!owned || !destination || owned->stepping || destination->stepping || !owned->application || owned->source_restoring)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "source lease publication requires idle frontend owners");
    if (!qa_application_guest_context_rebind_ready(owned->application, &owned->frame, error)) return false;
    for (const frontend_source *source = owned->sources; source; source = source->next) {
        uint32_t ordinal;
        if (source->frontend != owned || source->application != owned->application || !source->leases ||
            !source->identity || source->seat >= owned->options.seats || !source->source_files ||
            !qa_application_constructor_seat_ordinal(owned->application,source->owner,source->launch_seat,&ordinal,error) ||
            ordinal!=source->seat ||
            !qa_application_provider_instance(owned->application, source->owner))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "source lease belongs to another frontend publication");
        size_t held=0;
        for (const frontend_source_lease *lease=source->lease_list;lease;lease=lease->next) {
            if (lease->source!=source || (lease->role!=QA_QVM_CGAME && lease->role!=QA_QVM_UI))
                return frontend_fail(error,QA_ERROR_ARGUMENT,"source role lease does not match its installed owner");
            if (!frontend_equipment_source_rebind_ready(lease->equipment,owned,error)) return false;
            ++held;
        }
        if (held!=source->leases) return frontend_fail(error,QA_ERROR_ARGUMENT,"source lease list differs from active ownership");
        if (!qa_q3_presentation_frontend_rebind_ready(source->presentation, &owned->frame,
                owned->audio, source->identity, error)) return false;
    }
    return true;
}
void frontend_source_rebind(qa_frontend *owned, qa_frontend *destination)
{
    for (frontend_source *source = owned->sources; source; source = source->next) {
        qa_q3_presentation_frontend_rebind(source->presentation, &owned->frame, &destination->frame,
            owned->audio, source->identity);
        if (source->music_attached)
            source->music = qa_audio_engine_bus_music(owned->audio, source->identity);
        source->frontend = destination;
        for (frontend_source_lease *lease=source->lease_list;lease;lease=lease->next)
            frontend_equipment_source_rebind(lease->equipment,destination);
    }
}
bool frontend_source_retire_world(qa_frontend *frontend, qa_error *error)
{
    for (frontend_source *source = frontend->sources; source; source = source->next) {
        if (!qa_q3_presentation_retire_world(source->presentation, error)) return false;
        if (source->music_attached) {
            qa_audio_engine_remove_music(frontend->audio, source->identity);
            source->music = NULL; source->music_attached = false;
        } else if (source->music) qa_audio_music_stop(source->music);
        free(source->music_intro); free(source->music_loop);
        source->music_intro=source->music_loop=NULL; source->music_looping=false;
        source->has_listener = false;
    }
    return true;
}
void frontend_source_audio_stopped(qa_frontend *frontend)
{
    for (frontend_source *source=frontend->sources;source;source=source->next) {
        if (source->music_attached) { source->music=NULL; source->music_attached=false; }
        else if (source->music) qa_audio_music_stop(source->music);
        free(source->music_intro); free(source->music_loop);
        source->music_intro=source->music_loop=NULL; source->music_looping=false;
    }
}
bool frontend_source_round_ready(const qa_frontend *frontend, qa_actor_owner owner, qa_error *error)
{
    if (!frontend || frontend->stepping || frontend->source_restoring || !frontend->application ||
        !qa_application_q3_round_callback_ready(frontend->application, owner, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Source round requires completed installed frontend leases");
    qa_collision_geometry *geometry = frontend->scene_world ?
        qa_world_geometry(qa_application_world(frontend->application)) : NULL;
    for (const frontend_source *source = frontend->sources; source; source = source->next) {
        uint32_t ordinal;
        if (!source->constructed || source->frontend != frontend ||
            source->application != frontend->application || !source->leases || !source->identity ||
            source->seat >= frontend->options.seats || !source->source_files ||
            !qa_application_constructor_seat_ordinal(frontend->application,source->owner,source->launch_seat,&ordinal,error) ||
            ordinal!=source->seat ||
            !qa_application_provider_instance(frontend->application, source->owner))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Source round lease leaves its actual frontend owner");
        size_t held = 0;
        for (const frontend_source_lease *lease = source->lease_list; lease; lease = lease->next) {
            if (lease->source != source || !lease->service_owner ||
                (lease->role != QA_QVM_CGAME && lease->role != QA_QVM_UI))
                return frontend_fail(error, QA_ERROR_ARGUMENT, "Source round has an invalid installed role lease");
            ++held;
        }
        if (held != source->leases || !qa_q3_presentation_round_ready(source->presentation,
                &frontend->frame, frontend->scene_world, geometry, error))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Source round presentation is not at its retained owner boundary");
    }
    return true;
}
bool frontend_source_reset_round(qa_frontend *frontend, qa_actor_owner owner, qa_error *error)
{
    if (!frontend_source_round_ready(frontend, owner, error)) return false;
    for (frontend_source *source = frontend->sources; source; source = source->next) {
        if (!qa_q3_presentation_clear(source->presentation, error)) return false;
        source->has_listener = false;
        if (source->music_attached) {
            source->music = NULL; source->music_attached = false;
        } else if (source->music) qa_audio_music_stop(source->music);
        free(source->music_intro); free(source->music_loop);
        source->music_intro=source->music_loop=NULL; source->music_looping=false;
    }
    return true;
}
bool frontend_source_publish_world(qa_frontend *frontend, qa_error *error)
{
    qa_bsp_view bsp;
    if (!frontend->scene_world || !frontend->map_resource) return true;
    if (!qa_bsp_open(qa_resource_bytes(frontend->map_resource), &bsp, error)) return false;
    for (frontend_source *source = frontend->sources; source; source = source->next)
        if (!qa_q3_presentation_world(source->presentation, frontend->scene_world,
            qa_world_geometry(qa_application_world(source->application)), bsp.lumps[QA_BSP_ENTITIES].bytes, error)) return false;
    return true;
}
bool frontend_source_frame(qa_frontend *frontend, uint32_t seat, qa_scene_rect rect, qa_error *error)
{
    if (!frontend || frontend->capture || !frontend_sources_idle(frontend))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source frame requires idle retained parent and child owners");
    for (frontend_source *source = frontend->sources; source; source = source->next)
        if (source->seat == seat) {
            source->has_listener = false;
            if (!qa_q3_presentation_frame(source->presentation, &frontend->frame, rect, error)) return false;
        }
    return true;
}
bool frontend_source_listener(qa_frontend *frontend, uint32_t seat, qa_audio_listener *out)
{
    for (frontend_source *source = frontend->sources; source; source = source->next)
        if (source->seat == seat && source->has_listener) { *out = source->listener; return true; }
    return false;
}
bool frontend_before_world_change(void *context, qa_application *application, qa_error *error)
{
    qa_frontend *frontend = context; (void)application;
    if (!frontend_owners_idle(frontend))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"World retirement requires idle frontend child owners");
    return frontend_campaign_ready(frontend) && frontend_tools_before_world_change(frontend, error) &&
        frontend_campaign_destroy(frontend,error);
}
bool frontend_world_change_ready(void *context, qa_application *application, qa_error *error)
{
    qa_frontend *frontend = context; (void)application;
    if (!frontend_cinematic_capture_ready(frontend))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Finish or skip the cinematic before changing worlds");
    return frontend_owners_idle(frontend) && frontend_campaign_ready(frontend) ?
        frontend_tools_world_change_ready(frontend,error) && frontend_network_world_change_ready(frontend,error) :
        frontend_fail(error,QA_ERROR_ARGUMENT,"World change requires idle frontend child owners");
}
bool frontend_world_retired(void *context, qa_application *application, qa_error *error)
{
    qa_frontend *frontend = context; (void)application;
    if (!frontend_owners_idle(frontend))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"World retirement requires idle frontend child owners");
    qa_scene_frame_reset(&frontend->frame, frontend->frame_number);
    frontend_native_q2_retire_world(frontend);
    for (unsigned i = 0; i < frontend->options.seats && !frontend->options.dedicated; ++i) {
        if (!qa_ui_rankings_reset_binding(frontend->seats[i].rankings, error)) return false;
        frontend_player_retire(&frontend->seats[i]);
    }
    if (!frontend_shader_retire(frontend, error)) return false;
    if (!frontend_equipment_retire(frontend,error)) return false;
    frontend_visuals_destroy(frontend);
    if (!frontend_source_retire_world(frontend, error)) return false;
    frontend_particle_retire(frontend);
    frontend_qc_rerelease_retire_world(frontend);
    frontend_event_retire(frontend);
    frontend_audio_retire_round_aliases(frontend);
    frontend->silent_audio_remainder = 0;
    return !frontend->audio || qa_audio_engine_reset_round(frontend->audio, error);
}

size_t frontend_source_group_count(const qa_frontend *frontend)
{
    size_t count=0; if (frontend) for (const frontend_source *source=frontend->sources;source;source=source->next) ++count;
    return count;
}
bool frontend_source_cgame_recipient(const qa_frontend *frontend,uint32_t seat,qa_actor_owner *out,qa_error *error)
{
    if (!frontend || !frontend->application || !out || seat>=frontend->options.seats ||
        frontend->capture || frontend->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CGAME recipient requires its installed physical frontend seat");
    qa_actor_owner receiver=0;
    for (const frontend_source *source=frontend->sources;source;source=source->next) {
        if (source->seat!=seat || !source->leases) continue;
        for (const frontend_source_lease *lease=source->lease_list;lease;lease=lease->next) {
            if (lease->role!=QA_QVM_CGAME) continue;
            if (!source->constructed || source->frontend!=frontend || source->application!=frontend->application ||
                lease->source!=source || lease->released || !lease->service_owner || !source->owner || receiver)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"CGAME recipient leaves its unique retained source role");
            receiver=source->owner;
        }
    }
    *out=receiver; return true;
}
bool frontend_source_audio_view(const qa_frontend *frontend,const qa_audio_asset *asset,qa_vfs **out)
{
    if (!frontend || !asset || !out) return false;
    qa_resource *resource=qa_audio_asset_resource(asset);
    if (!resource) return false;
    for (const frontend_source *source=frontend->sources;source;source=source->next)
        if (source->sounds && qa_audio_bank_get(source->sounds,qa_resource_id(resource),qa_audio_asset_family(asset))==asset) {
            *out=source->mounts; return *out!=NULL;
        }
    return false;
}
bool frontend_source_group_read(const qa_frontend *frontend, size_t index, frontend_source_group_view *out)
{
    if (!frontend || !out || frontend->stepping) return false;
    const frontend_source *source=frontend->sources;
    while (source && index--) source=source->next;
    if (!source || !source->constructed || source->frontend!=frontend || source->application!=frontend->application) return false;
    frontend_source_group_view view={.owner=source->owner,.seat=source->seat,.launch_seat=source->launch_seat,.identity=source->identity,
        .source_files=source->source_files,.mounts=source->mounts,.images=source->images,.materials=source->materials,
        .fonts=source->fonts,.sounds=source->sounds,.movies=source->movies,.keys=source->keys,
        .assets=source->assets,.presentation=source->presentation,.listener=source->listener,
        .has_listener=source->has_listener,.music_attached=source->music_attached};
    unsigned held=0;
    for (const frontend_source_lease *lease=source->lease_list;lease;lease=lease->next) {
        if (lease->source!=source || (lease->role!=QA_QVM_CGAME && lease->role!=QA_QVM_UI) || held==UINT_MAX) return false;
        ++view.roles[lease->role]; ++held;
    }
    if (held!=source->leases) return false;
    view.music=source->music_attached?qa_audio_engine_bus_music(frontend->audio,source->identity):source->music;
    *out=view; return true;
}
bool frontend_source_group_q3_ready(const qa_frontend *f,size_t index,
    const qa_q3_presentation_options *p,const qa_q3_presentation_asset_options *a,qa_error *error)
{
    const frontend_source *source=f?f->sources:NULL;
    while (source && index--) source=source->next;
    if (!f || !f->application || f->stepping || !source || !source->constructed ||
        source->frontend!=f || source->application!=f->application || !source->leases || !p || !a ||
        p->assets!=source->assets || p->audio!=f->audio || p->clock.context!=source || p->clock.sample!=milliseconds ||
        p->seat!=source->seat || p->owner!=source->identity || p->context!=source ||
        p->near_clip!=4 || p->far_clip!=16384 || p->identity_light!=1 || p->lod_scale!=5 || p->lod_bias || p->shadow_mode ||
        p->rail_core_width!=6 || p->rail_ring_width!=16 || p->rail_segment_length!=32 ||
        p->audio_actor!=source_actor || p->listener!=listener || p->music!=music ||
        p->frame_number!=frame_number || p->milliseconds!=source_milliseconds || p->audio_bus!=audio_bus ||
        p->system_movie || p->prepare_picture || p->video_frame || p->video_context ||
        p->prepare_view!=prepare_view || p->submit_view!=submit_view || p->scene_cleared!=scene_cleared ||
        p->remap!=source_remap || p->print!=print_source ||
        a->provider.mounts!=source->mounts || a->provider.images!=source->images ||
        a->provider.materials!=source->materials || a->provider.family!=QA_SCENE_Q3 ||
        a->sounds!=source->sounds || a->movies!=source->movies || a->zero_sound ||
        a->context!=source || a->select || a->print!=print_source)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q3 source policy differs from its genuine installed frontend group");
    return true;
}
bool frontend_source_group_role_read(const qa_frontend *frontend, size_t group, size_t index,
    frontend_source_role_identity *out)
{
    if (!frontend || !out || frontend->stepping) return false;
    const frontend_source *source = frontend->sources;
    while (source && group--) source = source->next;
    if (!source || !source->constructed || source->frontend != frontend || source->application != frontend->application)
        return false;
    const frontend_source_lease *lease = source->lease_list;
    while (lease && index--) lease = lease->next;
    if (!lease || lease->source != source || !lease->service_owner) return false;
    *out = (frontend_source_role_identity){lease->role, lease->service_owner}; return true;
}
bool frontend_source_registry_scope_read(const qa_frontend *frontend,const qa_cvars *registry,
    qa_application_console_scope *out)
{
    if (!frontend || !frontend->application || !registry || !out) return false;
    bool found=false; qa_application_console_scope scope={0};
    for (const frontend_source *source=frontend->sources;source;source=source->next) {
        if (!source->constructed || source->frontend!=frontend || source->application!=frontend->application ||
            !qa_application_provider_instance(frontend->application,source->owner)) continue;
        for (const frontend_source_lease *lease=source->lease_list;lease;lease=lease->next) {
            if (lease->released || lease->source!=source || lease->cvars!=registry ||
                (lease->role!=QA_QVM_CGAME && lease->role!=QA_QVM_UI)) continue;
            qa_application_console_scope row={source->owner,lease->role==QA_QVM_CGAME?
                QA_APPLICATION_CONSOLE_Q3_CGAME:QA_APPLICATION_CONSOLE_Q3_UI,source->launch_seat};
            if (!found || row.provider<scope.provider || (row.provider==scope.provider &&
                (row.kind<scope.kind || (row.kind==scope.kind && row.seat<scope.seat)))) scope=row;
            found=true;
        }
    }
    if (found) *out=scope;
    return found;
}
bool frontend_source_role_media_read(const qa_frontend *frontend,qa_actor_owner receiver,
    qa_qvm_role role,uint32_t launch_seat,uint64_t service_owner,qa_vfs **out)
{
    if (!frontend || !frontend->application || !receiver || !service_owner || !out ||
        (role!=QA_QVM_CGAME && role!=QA_QVM_UI)) return false;
    qa_vfs *view=NULL;
    for (const frontend_source *source=frontend->sources;source;source=source->next) {
        if (!source->constructed || source->frontend!=frontend || source->application!=frontend->application ||
            source->owner!=receiver || source->launch_seat!=launch_seat || !source->mounts) continue;
        for (const frontend_source_lease *lease=source->lease_list;lease;lease=lease->next) {
            if (lease->released || lease->source!=source || lease->role!=role || lease->service_owner!=service_owner) continue;
            if (view) return false;
            view=source->mounts;
        }
    }
    if (view) *out=view;
    return view!=NULL;
}
bool frontend_source_role_media_current(const qa_frontend *frontend,qa_actor_owner receiver,
    qa_qvm_role role,uint32_t launch_seat,uint64_t service_owner,const qa_vfs *retained)
{
    qa_vfs *actual=NULL;
    return retained && frontend_source_role_media_read(frontend,receiver,role,launch_seat,service_owner,&actual) &&
        actual==retained;
}
bool frontend_source_prepare_groups(qa_frontend *frontend, uint64_t next_source_id,
    const frontend_source_group_plan *plans, size_t count, qa_error *error)
{
    if (!frontend || !frontend->application || frontend->stepping || frontend->sources || frontend->source_restoring ||
        (count && !plans) || next_source_id > UINT64_MAX - QA_FRONTEND_COMMAND_OWNER - 1)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "restored source admission requires an empty idle frontend");
    qa_application_content_graph *graph = qa_application_content_graph_read(frontend->application);
    if (!graph) return frontend_fail(error, QA_ERROR_ARGUMENT, "source admission requires its actual restored content graph");
    for (size_t i = 0; i < count; ++i) {
        const frontend_source_group_plan *plan = &plans[i];
        if (!plan->owner || plan->seat >= frontend->options.seats || frontend->options.dedicated ||
            plan->identity <= QA_FRONTEND_COMMAND_OWNER ||
            plan->identity - QA_FRONTEND_COMMAND_OWNER > next_source_id || !plan->roles ||
            !plan->role_count || plan->role_count > UINT_MAX || !plan->mounts_view || !plan->source_view ||
            plan->mounts_view == plan->source_view || !qa_application_content_view(graph, plan->mounts_view) ||
            !qa_application_content_view(graph, plan->source_view) ||
            qa_vfs_resources(qa_application_content_view(graph, plan->mounts_view)) !=
                qa_vfs_resources(qa_application_content_view(graph, plan->source_view)) ||
            plan->role_count > SIZE_MAX / sizeof(*plan->roles))
            return frontend_fail(error, QA_ERROR_FORMAT, "invalid restored source group identity");
        for (size_t prior = 0; prior < i; ++prior)
            if (plans[prior].identity == plan->identity || plans[prior].mounts_view == plan->mounts_view ||
                (plans[prior].owner == plan->owner && plans[prior].seat == plan->seat &&
                 plans[prior].source_view == plan->source_view))
                return frontend_fail(error, QA_ERROR_FORMAT, "duplicate restored source group identity");
        for (size_t j = 0; j < plan->role_count; ++j) {
            const frontend_source_role_identity *role = &plan->roles[j];
            if ((role->role != QA_QVM_CGAME && role->role != QA_QVM_UI) ||
                !role->service_owner || role->service_owner > UINT32_MAX)
                return frontend_fail(error, QA_ERROR_FORMAT, "invalid restored source service owner");
            for (size_t a = 0; a <= i; ++a)
                for (size_t b = 0; b < (a == i ? j : plans[a].role_count); ++b)
                    if (plans[a].roles[b].service_owner == role->service_owner)
                        return frontend_fail(error, QA_ERROR_FORMAT, "duplicate restored source service owner");
        }
    }
    frontend_source *head = NULL, **tail = &head;
    for (size_t i = 0; i < count; ++i) {
        frontend_source *source = calloc(1, sizeof(*source));
        if (!source) goto memory;
        *tail = source; tail = &source->next;
        source->frontend = frontend; source->owner = plans[i].owner; source->seat = plans[i].seat;
        source->launch_seat=plans[i].launch_seat;
        source->identity = plans[i].identity; source->restore_role_count = plans[i].role_count;
        source->restore_roles = malloc(plans[i].role_count * sizeof(*source->restore_roles));
        if (!source->restore_roles) goto memory;
        memcpy(source->restore_roles, plans[i].roles, plans[i].role_count * sizeof(*source->restore_roles));
    }
    frontend->sources = head; frontend->next_source_id = next_source_id; frontend->source_restoring = true;
    frontend_source *source = head;
    for (size_t i = 0; i < count; ++i, source = source->next) {
        source->source_files = qa_application_content_view(graph, plans[i].source_view);
        if (!qa_application_content_claim_view(graph, plans[i].mounts_view, &source->mounts, error)) return false;
    }
    return true;
memory:
    while (head) { frontend_source *next = head->next; free(head->restore_roles); free(head); head = next; }
    return frontend_fail(error, QA_ERROR_MEMORY, "retaining restored source group admission");
}
bool frontend_source_complete_groups(const qa_frontend *frontend, qa_error *error)
{
    if (!frontend || frontend->stepping || !frontend->source_restoring)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "restored source groups require idle admitted construction");
    for (const frontend_source *source = frontend->sources; source; source = source->next) {
        if (!source->constructed || source->frontend != frontend || source->application != frontend->application ||
            !source->source_files || source->leases != source->restore_role_count)
            return frontend_fail(error, QA_ERROR_FORMAT, "restored source group was not completely constructed");
        const frontend_source_lease *lease = source->lease_list;
        for (size_t i = 0; i < source->restore_role_count; ++i, lease = lease->next) {
            if (!lease || lease->source != source || lease->role != source->restore_roles[i].role ||
                lease->service_owner != source->restore_roles[i].service_owner)
                return frontend_fail(error, QA_ERROR_FORMAT, "restored source role ownership differs from admitted plan");
        }
        if (lease) return frontend_fail(error, QA_ERROR_FORMAT, "restored source group has excess role ownership");
    }
    return true;
}
void frontend_source_finish_groups(qa_frontend *frontend)
{
    for (frontend_source *source = frontend->sources; source; source = source->next) {
        free(source->restore_roles); source->restore_roles = NULL; source->restore_role_count = 0;
    }
    frontend->source_restoring = false;
}
bool frontend_source_discard_unbound(qa_frontend *frontend, qa_error *error)
{
    if (!frontend || frontend->stepping || frontend->capture || !frontend_sources_idle(frontend))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "source group cleanup requires idle frontend");
    for (frontend_source *source = frontend->sources; source; source = source->next)
        if (source->leases)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "source guest leases must retire before group cleanup");
    while (frontend->sources) {
        frontend_source *source=frontend->sources,*next=source->next;
        if (!source_free(source))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Source group retains an active child owner");
        frontend->sources=next;
    }
    frontend->source_restoring = false; return true;
}

bool frontend_source_identity_used(const qa_frontend *frontend, uint64_t identity)
{
    if (!frontend) return false;
    for (const frontend_source *source = frontend->sources; source; source = source->next)
        if (source->identity == identity) return true;
    return false;
}

typedef struct source_role_saved {
    frontend_source_lease *lease;
    qa_application_q3_client_context identity;
    char *system_info, *disconnect_reason;
    uint32_t time_dialect;
    const char *names[6];
    size_t name_count;
    qa_buffer owned_equipment;
    qa_bytes equipment;
    bool admitted, time_present, time_alias, bound, disconnect_pending,has_equipment;
} source_role_saved;
typedef struct source_group_saved {
    frontend_source *source;
    qa_audio_listener listener;
    uint64_t key_profile;
    uint8_t keys[34];
    qa_buffer owned_music;
    qa_bytes music;
    char *music_intro, *music_loop;
    source_role_saved *roles;
    size_t role_count;
    bool has_listener, has_music, music_attached, music_looping;
} source_group_saved;
static void source_saved_free(source_group_saved *groups,size_t count)
{
    if (!groups) return;
    for (size_t i=0;i<count;++i) {
        for (size_t j=0;groups[i].roles && j<groups[i].role_count;++j) {
            free(groups[i].roles[j].system_info); free(groups[i].roles[j].disconnect_reason);
            qa_buffer_free(&groups[i].roles[j].owned_equipment);
        }
        free(groups[i].roles); free(groups[i].music_intro); free(groups[i].music_loop);
        qa_buffer_free(&groups[i].owned_music);
    }
    free(groups);
}
static bool source_blob(qa_source_save_io *io,qa_bytes *bytes)
{
    size_t size=bytes->size;
    if (!qa_source_save_count(io,&size,io->direction==QA_SOURCE_SAVE_READ?io->input.size-io->offset:SIZE_MAX)) return false;
    if (io->direction==QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io,(void *)bytes->data,size);
    if (size>io->input.size-io->offset) return false;
    *bytes=(qa_bytes){io->input.data+io->offset,size}; io->offset+=size; return true;
}
static bool source_listener_fields(qa_source_save_io *io,qa_frontend *f,source_group_saved *group)
{
    qa_audio_listener *listener=&group->listener;
    if (!qa_source_save_bool(io,&group->has_listener) || !qa_source_save_u32(io,&listener->seat) ||
        !qa_source_save_u64(io,&listener->actor) || !qa_source_save_vec3(io,&listener->origin)) return false;
    for (size_t i=0;i<3;++i) if (!qa_source_save_vec3(io,listener->axis+i)) return false;
    if (!qa_source_save_f32(io,&listener->gain) || !qa_source_save_bool(io,&listener->underwater)) return false;
    if (group->has_listener && (listener->seat!=group->source->seat || !qa_vec_finite(listener->origin) ||
        !qa_vec_finite(listener->axis[0]) || !qa_vec_finite(listener->axis[1]) || !qa_vec_finite(listener->axis[2]) ||
        !isfinite(listener->gain) || listener->gain<0 ||
        (listener->actor!=QA_AUDIO_NO_ACTOR && !frontend_audio_id_read(f,listener->actor,NULL,NULL))))
        return frontend_fail(io->error,QA_ERROR_FORMAT,"Source listener leaves its actual seat/audio actor owner");
    return true;
}
static bool source_role_fields(qa_source_save_io *io,qa_frontend *f,source_role_saved *row)
{
    frontend_source_lease *lease=row->lease;
    uint32_t role=lease->role; uint64_t service=lease->service_owner;
    if (!qa_source_save_u32(io,&role) || role!=(uint32_t)lease->role ||
        !qa_source_save_u64(io,&service) || service!=lease->service_owner ||
        !qa_source_save_bool(io,&row->admitted)) return false;
    if (row->admitted) {
        if (lease->role!=QA_QVM_CGAME || !frontend_save_provider(io,f->application,&row->identity.source_owner) ||
            !qa_source_save_actor(io,&row->identity.source_actor) ||
            !qa_source_save_u32(io,&row->identity.source_client) ||
            !qa_source_save_bool(io,&row->identity.native_source) ||
            !qa_source_save_bool(io,&row->time_present)) return false;
        if (row->time_present && (!frontend_save_provider(io,f->application,&row->identity.client_time_owner) ||
            !qa_source_save_bool(io,&row->time_alias) || !qa_source_save_u32(io,&row->time_dialect) ||
            row->time_dialect>QA_CONSOLE_Q3)) return false;
    }
    if (!frontend_save_text(io,&row->system_info) || !qa_source_save_bool(io,&row->bound) ||
        !qa_source_save_count(io,&row->name_count,6)) return false;
    const char *names[6]; size_t count=row->time_present?time_names((qa_console_dialect)row->time_dialect,names,true):0;
    uint32_t previous=0;
    for (size_t i=0;i<row->name_count;++i) {
        uint32_t name=0;
        if (io->direction==QA_SOURCE_SAVE_WRITE) {
            while (name<count && strcmp(row->names[i],names[name])) ++name;
        }
        if (!qa_source_save_u32(io,&name) || name>=count || (i && name<=previous)) return false;
        row->names[i]=names[name]; previous=name;
    }
    if (row->bound && (!row->admitted || !row->time_present || row->time_alias || !row->name_count)) return false;
    if (!row->bound && row->name_count) return false;
    if (!qa_source_save_bool(io,&row->disconnect_pending) || !frontend_save_text(io,&row->disconnect_reason)) return false;
    if ((row->disconnect_pending || row->system_info) && !row->admitted) return false;
    if (row->disconnect_pending != (row->disconnect_reason!=NULL)) return false;
    if (!qa_source_save_bool(io,&row->has_equipment) || row->has_equipment!=(lease->equipment!=NULL) ||
        (row->has_equipment && (!source_blob(io,&row->equipment) || !row->equipment.size))) return false;
    return true;
}
static bool source_saved_fields(qa_source_save_io *io,qa_frontend *f,source_group_saved *groups,size_t count)
{
    for (size_t i=0;i<count;++i) {
        source_group_saved *group=groups+i; frontend_source *source=group->source;
        qa_actor_owner owner=source->owner; uint32_t seat=source->seat,launch_seat=source->launch_seat; uint64_t identity=source->identity;
        if (!frontend_save_provider(io,f->application,&owner) || owner!=source->owner ||
            !qa_source_save_u32(io,&seat) || seat!=source->seat ||
            !qa_source_save_u32(io,&launch_seat) || launch_seat!=source->launch_seat ||
            !qa_source_save_u64(io,&identity) || identity!=source->identity ||
            !qa_source_save_u64(io,&group->key_profile) ||
            group->key_profile!=frontend_key_profile_id(source->key_profile) ||
            (group->key_profile && (frontend_keys_profile(f->keys,group->key_profile)!=source->key_profile ||
                frontend_key_profile_state(source->key_profile)!=source->keys)) ||
            (!group->key_profile && !qa_source_save_bytes(io,group->keys,sizeof(group->keys))) ||
            !source_listener_fields(io,f,group) || !qa_source_save_bool(io,&group->has_music) ||
            !qa_source_save_bool(io,&group->music_attached) || (group->music_attached && !group->has_music) ||
            (group->has_music && (!source_blob(io,&group->music) || !group->music.size)) ||
            !frontend_save_text(io,&group->music_intro) || !frontend_save_text(io,&group->music_loop) ||
            !qa_source_save_bool(io,&group->music_looping) ||
            ((group->music_intro!=NULL)!=(group->music_loop!=NULL)) ||
            (group->music_intro && (!*group->music_intro || !group->has_music)) ||
            (group->music_looping && !group->music_intro)) return false;
        size_t actual=source->leases;
        if (!qa_source_save_count(io,&group->role_count,actual) || group->role_count!=actual) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) {
            group->roles=calloc(actual,sizeof(*group->roles));
            if (actual && !group->roles) return frontend_fail(io->error,QA_ERROR_MEMORY,"Retaining source role continuation");
        }
        frontend_source_lease *lease=source->lease_list;
        for (size_t j=0;j<actual;++j,lease=lease->next) {
            if (!lease) return false;
            group->roles[j].lease=lease;
            if (!source_role_fields(io,f,group->roles+j)) return false;
        }
        if (lease) return false;
    }
    return true;
}
static bool source_header(qa_source_save_io *io,size_t *count)
{
    uint8_t magic[4]={'Q','F','S','O'}; uint32_t version=5;
    return qa_source_save_bytes(io,magic,sizeof(magic)) && !memcmp(magic,"QFSO",4) &&
        qa_source_save_u32(io,&version) && version==5 &&
        qa_source_save_count(io,count,SIZE_MAX/sizeof(source_group_saved));
}
bool frontend_source_checkpoint(qa_frontend *f,qa_buffer *out,qa_error *error)
{
    if (!f || !f->application || f->stepping || !f->capture || !frontend_sources_idle(f) ||
        !out || out->data || out->size)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source continuation requires its held actual frontend capture");
    size_t count=frontend_source_group_count(f);
    source_group_saved *groups=count?calloc(count,sizeof(*groups)):NULL;
    if (count && !groups) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining source continuation roster");
    bool ok=true; frontend_source *source=f->sources;
    for (size_t i=0;ok && i<count;++i,source=source->next) {
        source_group_saved *group=groups+i;
        if (!source || !source->constructed || source->application!=f->application || !source->leases) { ok=false; break; }
        group->source=source; group->listener=source->listener; group->has_listener=source->has_listener;
        group->key_profile=frontend_key_profile_id(source->key_profile);
        if (!group->key_profile) qa_q3_key_capture(source->keys,group->keys);
        qa_audio_music *actual=source->music_attached?qa_audio_engine_bus_music(f->audio,source->identity):source->music;
        group->has_music=actual!=NULL; group->music_attached=source->music_attached;
        group->music_intro=source->music_intro; group->music_loop=source->music_loop;
        group->music_looping=source->music_looping;
        if (source->music_attached && !qa_audio_engine_music_ready(f->audio,source->identity,source->seat,1)) {
            ok=frontend_fail(error,QA_ERROR_FORMAT,"Source music no longer owns its actual seat route"); break;
        }
        if (group->has_music) {
            ok=qa_audio_music_checkpoint(actual,&group->owned_music,error);
            group->music=(qa_bytes){group->owned_music.data,group->owned_music.size};
        }
        group->role_count=source->leases; group->roles=calloc(group->role_count,sizeof(*group->roles));
        if (!group->roles) { ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual source role states"); break; }
        frontend_source_lease *lease=source->lease_list;
        for (size_t j=0;ok && j<group->role_count;++j,lease=lease->next) {
            if (!lease) { ok=false; break; }
            source_role_saved *row=group->roles+j; row->lease=lease;
            row->admitted=lease->time_context.frontend_lifetime!=NULL;
            if (row->admitted && !time_current(lease)) { ok=false; break; }
            row->identity=lease->time_context; row->time_present=row->identity.client_time_cvars!=NULL;
            row->time_alias=row->time_present && row->identity.client_time_cvars==row->identity.cvars;
            row->time_dialect=row->time_present?qa_cvars_dialect(row->identity.client_time_cvars):0;
            row->bound=lease->time_bound; row->name_count=lease->time_count;
            memcpy(row->names,lease->time_names,sizeof(row->names));
            row->system_info=lease->system_info; row->disconnect_reason=lease->disconnect_reason;
            row->disconnect_pending=lease->disconnect_pending;
            row->has_equipment=lease->equipment!=NULL;
            if (row->has_equipment) {
                ok=frontend_equipment_source_checkpoint(lease->equipment,&row->owned_equipment,error);
                row->equipment=(qa_bytes){row->owned_equipment.data,row->owned_equipment.size};
            }
        }
    }
    qa_source_save_io io={0};
    if (ok) ok=qa_source_save_writer(&io,qa_application_session(f->application),error) && source_header(&io,&count) &&
        source_saved_fields(&io,f,groups,count) && qa_source_save_finish(&io,out);
    /* Capture rows borrow the live role's text; only decoder rows own copies. */
    for (size_t i=0;groups && i<count;++i) {
        groups[i].music_intro=groups[i].music_loop=NULL;
        for (size_t j=0;j<groups[i].role_count && groups[i].roles;++j)
            groups[i].roles[j].system_info=groups[i].roles[j].disconnect_reason=NULL;
    }
    qa_source_save_dispose(&io); source_saved_free(groups,count);
    if (!ok && (!error || error->code==QA_OK)) frontend_fail(error,QA_ERROR_FORMAT,"Source continuation leaves genuine owner roster");
    return ok;
}
bool frontend_source_restore(qa_frontend *f,qa_bytes bytes,qa_error *error)
{
    if (!f || !f->application || f->stepping || !f->source_restoring || !frontend_sources_idle(f) ||
        !frontend_source_complete_groups(f,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source continuation requires complete idle candidate role owners");
    size_t actual=frontend_source_group_count(f),count=0; qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,qa_application_session(f->application),bytes,error) && source_header(&io,&count) && count==actual;
    source_group_saved *groups=ok && count?calloc(count,sizeof(*groups)):NULL;
    if (ok && count && !groups) ok=frontend_fail(error,QA_ERROR_MEMORY,"Retaining candidate source continuation roster");
    frontend_source *source=f->sources;
    for (size_t i=0;ok && i<count;++i,source=source->next) groups[i].source=source;
    if (ok) ok=source_saved_fields(&io,f,groups,count) && qa_source_save_finish(&io,NULL);
    /* The complete envelope precedes clock/registry qualification and every
     * key, observer, music, or listener import into the candidate. */
    for (size_t i=0;ok && i<count;++i) {
        source_group_saved *group=groups+i;
        if (group->music_attached && !qa_audio_engine_music_ready(f->audio,group->source->identity,group->source->seat,1)) {
            ok=frontend_fail(error,QA_ERROR_FORMAT,"Restored source music differs from its actual seat route"); break;
        }
        for (size_t j=0;ok && j<group->role_count;++j) {
            source_role_saved *row=group->roles+j; frontend_source_lease *lease=row->lease;
            if (lease->time_bound || lease->system_info || lease->disconnect_reason || lease->time_busy) { ok=false; break; }
            if (!row->admitted) continue;
            qa_application_q3_client_context view;
            if (!time_context_read(f,group->source->owner,group->source->launch_seat,&view,error)) { ok=false; break; }
            if (view.frontend_lifetime!=lease || view.service_owner!=lease->service_owner ||
                view.source_owner!=row->identity.source_owner || !qa_actor_id_equal(view.source_actor,row->identity.source_actor) ||
                view.source_client!=row->identity.source_client || view.native_source!=row->identity.native_source ||
                (view.client_time_cvars!=NULL)!=row->time_present || (row->time_present &&
                (view.client_time_owner!=row->identity.client_time_owner || qa_cvars_dialect(view.client_time_cvars)!=row->time_dialect ||
                (view.client_time_cvars==view.cvars)!=row->time_alias)) || (row->bound && !view.initialized)) { ok=false; break; }
            row->identity=view;
            for (size_t k=0;k<row->name_count;++k)
                if (!qa_cvars_find(view.client_time_cvars,row->names[k]) || !qa_cvars_find(view.cvars,row->names[k])) { ok=false; break; }
        }
    }
    for (size_t i=0;ok && i<count;++i) {
        source_group_saved *group=groups+i; source=group->source;
        qa_audio_music *music_owner=NULL;
        if (group->has_music && !group->music_attached) ok=qa_audio_music_restore(group->music,&music_owner,error);
        else if (group->music_attached) {
            qa_buffer bus={0}; music_owner=qa_audio_engine_bus_music(f->audio,source->identity);
            ok=qa_audio_music_checkpoint(music_owner,&bus,error) && bus.size==group->music.size &&
                !memcmp(bus.data,group->music.data,bus.size); qa_buffer_free(&bus);
        }
        if (!ok) break;
        if (!group->key_profile && !qa_q3_key_restore(source->keys,(qa_bytes){group->keys,sizeof(group->keys)},error)) {
            if (!group->music_attached) qa_audio_music_destroy(music_owner); ok=false; break;
        }
        if (!source->music_attached) qa_audio_music_destroy(source->music);
        source->music=music_owner; source->music_attached=group->music_attached;
        free(source->music_intro); free(source->music_loop);
        source->music_intro=group->music_intro; source->music_loop=group->music_loop;
        group->music_intro=group->music_loop=NULL; source->music_looping=group->music_looping;
        source->listener=group->listener; source->has_listener=group->has_listener;
        for (size_t j=0;ok && j<group->role_count;++j) {
            source_role_saved *row=group->roles+j; frontend_source_lease *lease=row->lease;
            lease->time_context=row->identity; lease->time_count=row->name_count;
            memcpy(lease->time_names,row->names,sizeof(lease->time_names));
            lease->system_info=row->system_info; row->system_info=NULL;
            lease->disconnect_reason=row->disconnect_reason; row->disconnect_reason=NULL;
            lease->disconnect_pending=row->disconnect_pending;
            if (row->has_equipment) ok=frontend_equipment_source_restore(lease->equipment,&row->identity,row->equipment,error);
            if (ok && row->bound) ok=time_subscribe(lease,true,error);
        }
    }
    qa_source_save_dispose(&io); source_saved_free(groups,groups?count:0);
    if (!ok && (!error || error->code==QA_OK)) frontend_fail(error,QA_ERROR_FORMAT,"Source continuation differs from its actual candidate owners");
    return ok;
}
