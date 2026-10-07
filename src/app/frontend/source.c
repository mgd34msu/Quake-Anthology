#include "source_cinematics.h"
#include "source_renderer_runtime.h"
#include "renderer_materials.h"
#include "source_acoustics.h"
#include "source_client_registry.h"
#include "video_guests.h"
#include "source_companion.h"
#include "qa/application_q3_collision.h"
#include "qa/application_q3_body_entry.h"
#include "qa/application_q3_components.h"
#include "qa/application_native_q3_wire.h"
#include "component_scene.h"
#include "remote_q1_effects.h"
#include "q1_sky.h"
#include "internal.h"
#include "qa/binary.h"
#include "qa/q3_key.h"
#include "qa/audio_save.h"
#include "source_restore.h"
#include "view_bindings.h"
#include "view_settings.h"
#include "capture.h"
#include "round.h"
#include "campaign.h"
#include "campaign_cinematic.h"
#include "qc_rerelease_events.h"
#include "qc_messages.h"
#include "qa/q3_assets_save.h"
#include "qa/q3_assets_custody.h"
#include "qa/material_library_save.h"
#include "qa/scene_resource_save.h"
#include "qa/font_save.h"
#include "qa/persistence_content.h"
#include "qa/application_q3_client.h"
#include "qa/application_network_q3_status.h"
#include "network_presentation.h"
#include "qa/application_character_selection.h"
#include "qa/console_cvar_observer.h"
#include "save_private.h"
#include "network_q3_restart.h"
#include "network_local_groups.h"
#include "equipment_source.h"
#include "equipment_q3.h"
#include "equipment_gear.h"
#include "selected_character.h"
#include "selected_effects.h"
#include "source_effects.h"
#include "particle_delivery.h"
#include "config_store.h"
#include "shared_resource_policy.h"
#include "shared_register.h"
#include "shared_render_controls.h"
#include "q3_render_policy.h"
#include "material_movies.h"
#include "material_movie_bindings.h"
#include "qa/material_source_scratch.h"
#include "source_acoustics.h"
#include "q3_color_policy.h"
#include "qa/render_controls.h"
#include "visual_access.h"
#include "music_sources.h"
#include "qa/audio_music_prepare.h"
#include "remote_config.h"
#include "client_registry.h"
#include "native_q3_client.h"
#include "qa/application_q3_factory.h"
#include "qa/application_q3_scene_world.h"
#include "qa/q3_presentation_save.h"
#include "qa/scene_world_save.h"
#include <limits.h>
#include <stdio.h>

typedef struct frontend_source_lease frontend_source_lease;
typedef struct source_render_scope source_render_scope;
typedef struct source_body_draw source_body_draw;
typedef struct source_companion_packet {
    struct source_companion_packet *next;
    frontend_source_companion_packet view;
} source_companion_packet;
typedef struct source_companion {
    frontend_source_companion_view view;
    qa_scene_frame frame;
    source_companion_packet *first,*last;
    bool entered,completed;
} source_companion;
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
    source_render_scope *render_scope;
    qa_q3_registry_retirement *world_retirement;
    source_companion *companion;
    frontend_source_role_identity *restore_roles;
    size_t restore_role_count;
    bool constructed, construction_started;
    qa_vfs *mounts;
    const qa_vfs *source_files;
    qa_launch_instance_lease *music_metadata;
    qa_catalog *music_catalog;
    qa_product_id music_product;
    qa_scene_resources *images;
    qa_material_library *materials;
    qa_font_library *fonts;
    qa_audio_bank *sounds;
    qa_audio_music *music;
    char *music_intro, *music_loop;
    bool music_looping, music_pending;
    qa_media_library *movies;
    frontend_material_movies *shader_movies;
    qa_q3_key *keys;
    frontend_key_profile *key_profile;
    const frontend_key_profile *retiring_client_profile;
    qa_console *retiring_client_console;
    qa_cvars *retiring_client_cvars;
    qa_q3_presentation_assets *assets;
    qa_q3_presentation *presentation;
    qa_resource *map_resource;
    qa_collision_geometry *geometry;
    qa_scene_world *world;
    bool private_map;
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
    frontend_client_registry *registry;
    frontend_config_host_cvars namespaces;
    qa_command_context command;
    frontend_equipment_source *equipment;
    source_body_draw *body_draw;
    qa_application_q3_client_context time_context;
    const char *time_names[6];
    qa_cvar_observer_token time_owner_tokens[6], time_mirror_tokens[6];
    size_t time_count, time_busy;
    char *system_info;
    char *disconnect_reason;
    const qa_q3_host *status_host;
    bool status_visible;
    bool disconnect_pending;
    bool time_bound, released;
};
struct source_render_scope {
    frontend_source_lease *lease;
    const qa_q3_host *host;
    const qa_qvm_call *call;
    const qa_q3_refdef *definition;
    frontend_source_effects *effects;
};
static bool render_enter(void *,const qa_q3_host *,const qa_qvm_call *,
    const qa_q3_refdef *,void **,qa_error *);
static void render_leave(void *,void *,bool);
static void body_scene_clear(frontend_source_lease *);
static bool body_scene_prepare(frontend_source_lease *,qa_q3_scene_options *,qa_error *);
static void body_scene_completed(frontend_source_lease *);
static bool body_scene_submit(frontend_source_lease *,const qa_q3_scene_options *,qa_scene_frame *,qa_error *);
static void source_retry_retirement(frontend_source *);
static bool source_publish_backend(frontend_source *,qa_error *);
static bool companion_capture(qa_frontend *,uint32_t,qa_scene_rect,qa_error *);
static void companion_packet_free(source_companion_packet *packet)
{
    if (!packet) return;
    free((void *)packet->view.entities); free((void *)packet->view.entity_actors);
    free((void *)packet->view.entity_views); free((void *)packet->view.polygons);
    free((void *)packet->view.polygon_actors); free((void *)packet->view.polygon_views);
    free((void *)packet->view.vertices); free((void *)packet->view.lights);
    free((void *)packet->view.light_actors); free((void *)packet->view.light_views);
    free((void *)packet->view.weapons); free(packet);
}
static void companion_clear(source_companion *capture)
{
    while (capture && capture->first) {
        source_companion_packet *packet=capture->first; capture->first=packet->next;
        companion_packet_free(packet);
    }
    if (capture) { capture->last=NULL; capture->view.packet_count=0; capture->completed=false; }
}
bool frontend_source_client_registry_read(const qa_frontend *f,uint32_t physical,
    frontend_source_client_registry *out,bool *present,qa_error *error)
{
    if (!f || !f->application || physical>=f->options.seats || !out || !present)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source CLIENT registry requires its actual physical recipient");
    *present=false; *out=(frontend_source_client_registry){0};
    uint32_t seat; qa_application_presentation_view selected;
    if (!frontend_seat_launch_id_read(f,physical,&seat) ||
        !qa_application_presentation_read(f->application,seat,&selected) || !selected.source_hud) return true;
    for (size_t i=0;;++i) {
        qa_application_startup_source tuple; bool found;
        if (!qa_application_console_source_at(f->application,i,&tuple,&found,error)) return false;
        if (!found) break;
        qa_application_console_scope scope=tuple.scope;
        if (scope.provider!=selected.hud ||
            scope.seat!=seat || (scope.kind!=QA_APPLICATION_CONSOLE_Q3_CGAME &&
                scope.kind!=QA_APPLICATION_CONSOLE_NATIVE_Q2 && scope.kind!=QA_APPLICATION_CONSOLE_CLIENT)) continue;
        if (*present && out->cvars!=tuple.cvars)
            return frontend_fail(error,QA_ERROR_FORMAT,"Source recipient has multiple physical CLIENT registries");
        if (!tuple.console || !tuple.cvars)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Source CLIENT tuple lost its own registry");
        *out=(frontend_source_client_registry){.publication=qa_application_launch(f->application),
            .console=tuple.console,.cvars=tuple.cvars,.receiver=scope.provider,.physical_seat=physical,.launch_seat=seat,.kind=scope.kind};
        *present=true;
    }
    return true;
}
bool frontend_source_client_registry_current(const qa_frontend *f,const frontend_source_client_registry *view)
{
    frontend_source_client_registry actual; bool present=false;
    return view && frontend_source_client_registry_read(f,view->physical_seat,&actual,&present,NULL) && present &&
        actual.publication==view->publication && actual.console==view->console && actual.cvars==view->cvars &&
        actual.receiver==view->receiver && actual.launch_seat==view->launch_seat && actual.kind==view->kind;
}
static bool status_visible(void *context,const qa_q3_host *host,bool *out,qa_error *error)
{
    const frontend_source_lease *lease=context;
    qa_q3_host_client_context actual;
    if (!lease || !out || lease->released || lease->role!=QA_QVM_CGAME ||
        !qa_q3_host_client_context_read(host,&actual) || actual.frontend_lifetime!=lease ||
        actual.role!=lease->role || actual.owner!=lease->source->owner ||
        actual.service_owner!=lease->service_owner || actual.console!=lease->console ||
        actual.cvars!=lease->cvars || actual.session!=qa_application_session(lease->source->application) ||
        lease->source->application!=lease->source->frontend->application)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Status permission lost its actual CGAME host namespace");
    bool linked=false;
    for (const frontend_source_lease *row=lease->source->lease_list;row;row=row->next)
        if (row==lease) { linked=true; break; }
    if (!linked || (lease->status_host ? lease->status_host!=host || !lease->source->role_operations :
        !qa_application_q3_configuration_host_entered(lease->source->application,&lease->namespaces.source,host)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Status permission lacks its retained Draw or entered source scope");
    *out=lease->status_visible;
    qa_actor_id actor;
    if(*out && lease->source->frontend->qc_messages &&
        frontend_seat_actor_read(lease->source->frontend,lease->source->seat,&actor)) {
        qa_application_qc_client_presentation qc; bool found=false;
        if(!frontend_qc_messages_client_vitals(lease->source->frontend->qc_messages,actor,&qc,&found,error))return false;
        if(found)*out=false;
    }
    return true;
}
static bool lease_dispose(frontend_source_lease *lease)
{
    if (lease->time_busy || !frontend_equipment_source_idle(lease->equipment)) return false;
    frontend_source_lease **link=&lease->source->retired_leases;
    while (*link && *link!=lease) link=&(*link)->next;
    if (*link!=lease) return false;
    qa_error error={0};
    if (!frontend_equipment_source_destroy(lease->equipment,&error)) return false;
    lease->equipment=NULL;
    if (!frontend_client_registry_release(&lease->registry,&error)) return false;
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
            if (ok && error) *error=cleanup;
            ok=false;
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
bool frontend_source_field_of_view(qa_frontend *f,double value,qa_error *error)
{
    if (!f || !f->application) return frontend_fail(error,QA_ERROR_ARGUMENT,"Client FOV needs its actual application");
    char text[64]; snprintf(text,sizeof(text),"%.17g",value);
    for (frontend_source *source=f->sources;source;source=source->next) {
        const qa_launch_instance *held=qa_launch_instance_lease_view(source->music_metadata);
        for (frontend_source_lease *lease=source->lease_list;lease;lease=lease->next) {
            if (lease->released || lease->role!=QA_QVM_CGAME || !lease->time_bound) continue;
            if (source->application!=f->application || source->frontend!=f || !held ||
                lease->source!=source || !lease->registry ||
                !frontend_client_registry_matches(lease->registry,held,source->launch_seat) ||
                lease->cvars!=frontend_client_registry_cvars(lease->registry) ||
                lease->time_busy==SIZE_MAX || source->role_operations==SIZE_MAX)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Client FOV left its retained CGAME namespace");
            if (!qa_cvars_find(lease->cvars,"cg_fov")) continue;
            ++lease->time_busy; ++source->role_operations;
            bool ok=qa_cvars_set(lease->cvars,"cg_fov",text,true,error);
            bool retained=!lease->released && lease->source==source && source->application==f->application &&
                lease->cvars==frontend_client_registry_cvars(lease->registry) &&
                frontend_client_registry_matches(lease->registry,held,source->launch_seat);
            time_leave(lease);
            if (!ok || !retained) return ok?frontend_fail(error,QA_ERROR_ARGUMENT,"Client FOV retired during publication"):false;
        }
    }
    return true;
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
            bool ok=time_subscribe(lease,restoring,error);
            double preference; bool explicit_override;
            if (ok && !restoring && f->view_settings &&
                frontend_view_settings_read(f->view_settings,&preference,&explicit_override) && explicit_override &&
                qa_cvars_find(lease->cvars,"cg_fov")) {
                char text[64]; snprintf(text,sizeof(text),"%.17g",preference);
                ok=time_write(lease,lease->cvars,"cg_fov",text,error);
            }
            time_leave(lease);
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
        !text)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Q3 effect requires its actual frontend receiver lifetime");
    bool handled=false;
    if (!frontend_native_q3_effect(f,application,receiver,seat,effect,text,&handled,error)) return false;
    if (handled) return true;
    if (!qa_application_constructor_seat_ordinal(application,receiver,seat,&ordinal,error) ||
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
    return (double)source->frontend->wall_time_ns / 1000000.0;
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
static bool model_initialize(void *context,const qa_q3_model_opening *opening,
    const qa_model *native,qa_scene_model *root,qa_error *error)
{
    frontend_source *source=context;
    if (!source || !source->constructed || !source->leases ||
        source->application!=source->frontend->application)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Model admission lost its actual Source group");
    if (!frontend_visual_registered_model_initialize(source->frontend,opening,native,root,error)) return false;
    return source->constructed && source->leases && source->application==source->frontend->application;
}
static bool source_remap(void *context, const char *original, const char *replacement, float offset, qa_error *error)
{
    frontend_source *source=context;
    if (!source || !source->constructed || !source->leases || source->application!=source->frontend->application)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Shader remap lost its actual Source material owner");
    qa_material_source_remap_status status;
    if (!qa_material_remap_source(source->materials,original,replacement,offset,&status,error)) return false;
    if (status!=QA_MATERIAL_SOURCE_REMAP_APPLIED) {
        char warning[1200];
        snprintf(warning,sizeof(warning),status==QA_MATERIAL_SOURCE_REMAP_ORIGINAL_DEFAULT?
            "WARNING: R_RemapShader: shader %s not found\n":"WARNING: R_RemapShader: new shader %s not found\n",
            status==QA_MATERIAL_SOURCE_REMAP_ORIGINAL_DEFAULT?original:replacement);
        print_source(source,warning);
    }
    return source->leases && source->application==source->frontend->application;
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
static qa_audio_family music_family(const frontend_source *source)
{
    const qa_product *product=qa_catalog_product(source->music_catalog,source->music_product);
    return product && product->family==QA_GAME_Q1?QA_AUDIO_Q1:
        product && product->family==QA_GAME_Q2?QA_AUDIO_Q2:QA_AUDIO_Q3;
}
static bool music_published(const frontend_source *source)
{
    const qa_launch_instance *held=source?qa_launch_instance_lease_view(source->music_metadata):NULL;
    const qa_launch_snapshot *publication=source?qa_application_launch(source->application):NULL;
    const qa_launch_snapshot *previous=source?qa_application_startup_publication_previous(source->application,publication):NULL;
    if (previous) publication=previous;
    const qa_launch_instance *actual=held && publication?
        qa_launch_snapshot_find(publication,held->selection.instance):NULL;
    const char *instance=source?qa_application_provider_instance(source->application,source->owner):NULL;
    return actual && held && actual->storage==held->storage && instance &&
        !strcmp(instance,held->selection.instance);
}
static bool music_origin_current(void *context,const frontend_music_origin *origin)
{
    frontend_source *source=context;
    const qa_launch_instance *descriptor=source?qa_launch_instance_lease_view(source->music_metadata):NULL;
    bool linked=false;
    for (const frontend_source *row=source && source->frontend?source->frontend->sources:NULL;row;row=row->next)
        if (row==source) { linked=true; break; }
    return linked && source->constructed && music_published(source) && source->application==source->frontend->application && descriptor &&
        origin && origin->kind==FRONTEND_MUSIC_SOURCE && origin->context==source &&
        origin->bus==source->identity && origin->physical_seat==source->seat && origin->receiver==source->owner &&
        origin->descriptor && origin->descriptor->storage==descriptor->storage &&
        origin->catalog==source->music_catalog && origin->product==source->music_product &&
        origin->files==source->source_files && origin->music==source->music &&
        (!qa_audio_engine_bus_music(source->frontend->audio,source->identity) ||
         qa_audio_engine_bus_music(source->frontend->audio,source->identity)==source->music);
}
static bool music_origin_stop(void *context,qa_error *error)
{
    frontend_source *source=context;
    if (!source || !source->frontend || !source->music)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Music stop lost its retained Source player");
    qa_audio_engine_remove_music(source->frontend->audio,source->identity);
    if (qa_audio_engine_bus_music(source->frontend->audio,source->identity))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Music stop retained its actual engine bus");
    qa_audio_music_stop(source->music); source->music_attached=false;
    free(source->music_intro); free(source->music_loop);
    source->music_intro=source->music_loop=NULL; source->music_looping=false;
    return true;
}
static frontend_music_origin music_origin(frontend_source *source)
{
    return (frontend_music_origin){.kind=FRONTEND_MUSIC_SOURCE,.bus=source->identity,
        .physical_seat=source->seat,.receiver=source->owner,
        .descriptor=qa_launch_instance_lease_view(source->music_metadata),.catalog=source->music_catalog,
        .product=source->music_product,.files=source->source_files,.music=source->music,.context=source,
        .current=music_origin_current,.stop=music_origin_stop};
}
static bool music(void *context, const char *intro_name, const char *loop_name, qa_error *error)
{
    frontend_source *source = context;
    if (!source->frontend->audio) return frontend_fail(error, QA_ERROR_UNSUPPORTED, "source music output is disabled");
    qa_audio_music *attached=qa_audio_engine_bus_music(source->frontend->audio,source->identity);
    if (attached && attached!=source->music)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source music bus contains another retained player");
    source->music_attached=attached!=NULL;
    if (!source->music && !qa_audio_music_create(qa_audio_engine_rate(source->frontend->audio), music_family(source), true, &source->music, error)) return false;
    frontend_music_origin origin=music_origin(source);
    bool published=music_published(source);
    if (published && !frontend_music_sources_explicit_selected(source->frontend->music_sources,&origin)) {
        if (attached || !qa_audio_music_idle(source->music))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Source music selection retains its previous playback");
        qa_audio_music *fresh=NULL;
        if (!qa_audio_music_create(qa_audio_engine_rate(source->frontend->audio),music_family(source),true,&fresh,error)) return false;
        qa_audio_music_release(source->music); source->music=fresh;
        free(source->music_intro); free(source->music_loop); source->music_intro=source->music_loop=NULL;
        source->music_looping=false; origin=music_origin(source);
    }
    qa_audio_music_controls *controls=frontend_music_sources_controls(source->frontend->music_sources);
    if (!controls || (!qa_audio_music_controls_is(source->music,controls) &&
        !qa_audio_music_controls_bind(source->music,controls,error))) return false;
    if (published && !frontend_music_sources_explicit_begin(source->frontend->music_sources,&origin,error)) return false;
    bool enabled=false;
    if (!qa_audio_music_controls_enabled(controls,&enabled)) return false;
    if (intro_name && *intro_name && !enabled) return true;
    source->music_pending=!published;
    const char *requested_loop=loop_name?loop_name:"";
    if (intro_name && source->music_intro && source->music_loop && source->music_looping &&
        !strcmp(source->music_intro,intro_name) && !strcmp(source->music_loop,requested_loop) &&
        qa_audio_music_playing(source->music)) return true;
    qa_audio_music_stop(source->music);
    free(source->music_intro); free(source->music_loop);
    source->music_intro=source->music_loop=NULL; source->music_looping=false;
    if (!intro_name || !*intro_name)
        return !published || frontend_music_sources_explicit(source->frontend->music_sources,&origin,"","",false,error);
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
    if (!qa_audio_bank_music_cue(source->sounds, intro_name, music_family(source), NULL, NULL, &intro, error)) return false;
    if (!intro) return !published || frontend_music_sources_explicit(source->frontend->music_sources,&origin,
        source->music_intro,source->music_loop,false,error);
    loop=intro;
    if (loop_name && *loop_name) {
        if (!strcmp(intro_name, loop_name)) loop = intro;
        else if (!qa_audio_bank_music_cue(source->sounds, loop_name, music_family(source), NULL, NULL, &loop, error)) {
            qa_audio_stream_close(intro); return false;
        }
    }
    source->music_looping=loop!=NULL;
    qa_audio_music_start(source->music, intro, loop);
    if (!published) return true;
    bool retained=!source->music_attached;
    if (retained && !qa_audio_music_retain(source->music,error)) return false;
    bool ok = qa_audio_engine_music(source->frontend->audio, source->identity, source->seat, 1, source->music, error);
    if (!ok && retained) qa_audio_music_release(source->music);
    if (ok) {
        source->music_attached=true;
        ok=frontend_music_sources_explicit(source->frontend->music_sources,&origin,
            source->music_intro,source->music_loop,source->music_looping,error);
    }
    return ok;
}
bool frontend_source_publish_music(qa_frontend *frontend,qa_error *error)
{
    for (frontend_source *source=frontend->sources;source;source=source->next) {
        if (!source->music_pending) continue;
        if (!music_published(source))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Pending Source cue has no genuine published receiver");
        frontend_music_origin origin=music_origin(source);
        if (!frontend_music_sources_explicit_begin(frontend->music_sources,&origin,error)) return false;
        bool attach=qa_audio_music_playing(source->music) &&
            !qa_audio_engine_bus_music(frontend->audio,source->identity);
        if (attach && !qa_audio_music_retain(source->music,error)) return false;
        if (attach && !qa_audio_engine_music(frontend->audio,source->identity,source->seat,1,source->music,error)) {
            qa_audio_music_release(source->music); return false;
        }
        source->music_attached=qa_audio_engine_bus_music(frontend->audio,source->identity)==source->music;
        if (!frontend_music_sources_explicit(frontend->music_sources,&origin,
            source->music_intro?source->music_intro:"",source->music_loop?source->music_loop:"",
            source->music_looping,error)) return false;
        source->music_pending=false;
    }
    return true;
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
    if (!frontend_tools_camera(frontend,seat,options->world.view.clip_enabled,&options->world.view,error) ||
        (!options->world.no_world && !frontend_event_world(frontend,seat,&options->world,error))) return false;
    if (!options->world.no_world && options->world_family==QA_SCENE_Q3 && frontend->q1_sky) {
        qa_actor_id recipient;
        if (frontend_seat_actor_read(frontend,seat,&recipient)) {
            frontend_q1_sky_view sky;
            if (!frontend_q1_sky_view_read(frontend->q1_sky,recipient,&sky,error)) return false;
            if (sky.boxed) {
                options->world.override_sky=true;
                for (unsigned face=0;face<6;++face) options->world.sky_images[face]=sky.images[face];
            }
        }
    }
    return true;
}
bool frontend_source_submit_scene(qa_frontend *frontend,uint32_t seat,qa_actor_owner owner,
    const qa_q3_scene_options *options,qa_scene_frame *frame,qa_error *error)
{
    if (!frontend || seat>=frontend->options.seats || !owner || !options || frame!=&frontend->frame)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source scene submission requires its actual owner, frame and physical seat");
    if (options->world.no_world) return true;
    return frontend_visuals_submit(frontend,seat,owner,&options->world,frame,error) &&
        frontend_particle_draw(frontend,seat,&options->world,error) &&
        frontend_event_debug(frontend,&options->world.view,error) &&
        frontend_tools_debug(frontend,&options->world.view,error);
}
static qa_material_source_scratch *source_scratch(frontend_source *source,qa_error *error)
{
    qa_frontend *f=source->frontend;
    qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    if (!controls) { frontend_fail(error,QA_ERROR_ARGUMENT,"Source draw lacks its actual physical renderer"); return NULL; }
    return qa_render_controls_source_scratch(controls,error);
}
static qa_material_source_scratch *source_state(void *context,qa_error *error)
{
    frontend_source *source=context;
    if (source && source->constructed && source->leases && source->frontend &&
        source->application==source->frontend->application)
        for (frontend_source *row=source->frontend->sources;row;row=row->next)
            if (row==source) return source_scratch(source,error);
    frontend_fail(error,QA_ERROR_ARGUMENT,"Source renderer state lost its retained physical namespace");
    return NULL;
}
static bool diagnostics_read(void *context,qa_scene_source_diagnostics *out,qa_error *error)
{
    frontend_source *source=context;
    if (!source || !source->constructed || !source->leases || source->application!=source->frontend->application)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source issue diagnostics lost their retained renderer namespace");
    bool linked=false;
    for (frontend_source *row=source->frontend->sources;row;row=row->next)
        if (row==source) { linked=true; break; }
    return linked && frontend_q3_material_diagnostics_read(source->frontend,out,error);
}
static bool prepare_picture(void *context,qa_material_context *material,qa_error *error)
{
    frontend_source *source=context;
    if (!source || !source->constructed || !source->leases || source->application!=source->frontend->application)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Picture scratch lost its actual Source presentation");
    if (source->companion && source->companion->entered) {
        material->source_scratch=NULL;
        return frontend_q3_material_diagnostics_read(source->frontend,&material->source_diagnostics,error);
    }
    material->source_scratch=source_scratch(source,error);
    material->source_diagnostics_read=diagnostics_read; material->source_diagnostics_context=source;
    return material->source_scratch && frontend_q3_material_diagnostics_read(source->frontend,&material->source_diagnostics,error);
}
static bool render_current(const frontend_source *source,const source_render_scope *scope)
{
    if (!source || !scope || source->render_scope!=scope || !source->role_operations ||
        !source->application || source->application!=source->frontend->application ||
        scope->lease->source!=source || scope->lease->released || !scope->lease->time_busy) return false;
    bool linked=false;
    for (const frontend_source_lease *row=source->lease_list;row;row=row->next)
        if (row==scope->lease) { linked=true; break; }
    return linked && qa_q3_host_render_scope_current(scope->host,scope->call,
        scope->lease,scope->lease->service_owner,scope->lease->role,source->presentation);
}
static bool prepare_view(void *context,const qa_q3_refdef *definition,qa_q3_scene_options *options,qa_error *error)
{
    frontend_source *source=context;
    source_render_scope *scope=source->render_scope;
    if (!render_current(source,scope) || scope->definition!=definition)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source view preparation requires its live application lease");
    qa_actor_id viewer;
    if(!options->world.no_world && !(source->companion && source->companion->entered) &&
        source->frontend->qc_messages && frontend_seat_actor_read(source->frontend,source->seat,&viewer)) {
        qa_application_camera_view camera; bool found=false;
        if(!frontend_qc_messages_client_camera(source->frontend->qc_messages,viewer,&camera,&found,error))return false;
        if(found) {
            options->weapon_camera=definition;
            options->world.view.origin=qa_vec_add(camera.origin,camera.view_offset);
            frontend_camera_axes(camera.angles,options->world.view.axis);
        }
    }
    if (!frontend_source_prepare_scene(source->frontend,source->application,source->seat,definition,options,error)) return false;
    if (scope->effects && !frontend_source_effects_prepare(scope->effects,definition,options,error)) return false;
    if (scope->lease->equipment &&
        !frontend_equipment_source_prepare_view(scope->lease->equipment,definition,options,error)) return false;
    if (!render_current(source,scope))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source role retired during scene preparation");
    if (!frontend_q3_scene_policy_read(source->frontend,options,error)) return false;
    options->world.source_scratch=source_scratch(source,error);
    options->world.source_diagnostics_read=diagnostics_read; options->world.source_diagnostics_context=source;
    if (!body_scene_prepare(scope->lease,options,error)) return false;
    if (!options->world.source_scratch) return false;
    if (source->companion && source->companion->entered) {
        options->world.no_world=true;
        options->world.source_scratch=NULL;
    }
    if (scope->lease->role==QA_QVM_CGAME &&
        !frontend_q3_shadow_mode_read(scope->lease->cvars,&options->shadow_mode,error)) return false;
    return render_current(source,scope) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Source role retired during renderer settings observation");
}
static bool submit_view(void *context,const qa_q3_scene_options *options,qa_scene_frame *frame,qa_error *error)
{
    frontend_source *source=context;
    source_render_scope *scope=source->render_scope;
    if (!render_current(source,scope))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source scene submission lost its entered renderer lease");
    if (source->companion && source->companion->entered)
        return frame==&source->companion->frame ||
            frontend_fail(error,QA_ERROR_ARGUMENT,"Companion scene left its genuine private output frame");
    if (!frontend_source_submit_scene(source->frontend,source->seat,source->owner,options,frame,error)) return false;
    if (!body_scene_submit(scope->lease,options,frame,error)) return false;
    if (scope->lease->equipment &&
        !frontend_equipment_source_submit(scope->lease->equipment,options,frame,error)) return false;
    if (scope->effects && !frontend_source_effects_submit(scope->effects,options,frame,error)) return false;
    return render_current(source,scope) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Source role retired during scene submission");
}
static void scene_cleared(void *context)
{
    frontend_source *source=context;
    for (frontend_source_lease *lease=source->lease_list;lease;lease=lease->next) {
        frontend_equipment_source_clear(lease->equipment);
        body_scene_clear(lease);
    }
}
static bool scene_completed(void *context,const qa_q3_refdef *definition,const qa_q3_scene_options *options,
    const qa_q3_ref_entity *entities,size_t entity_count,const qa_q3_scene_polygon *polygons,size_t polygon_count,
    const qa_scene_vertex *vertices,size_t vertex_count,const qa_scene_light *lights,size_t light_count,qa_error *error)
{
    frontend_source *source=context; source_render_scope *scope=source->render_scope;
    (void)options;
    if (!render_current(source,scope) || scope->definition!=definition)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source scene completion lost its actual reached render scope");
    if (source->companion && source->companion->entered) {
        source_companion *capture=source->companion;
        if (entity_count>SIZE_MAX/sizeof(*entities) || (entity_count && !entities) ||
            polygon_count>SIZE_MAX/sizeof(*polygons) || (polygon_count && !polygons) ||
            vertex_count>SIZE_MAX/sizeof(*vertices) || (vertex_count && !vertices) ||
            light_count>SIZE_MAX/sizeof(*lights) || (light_count && !lights) ||
            capture->view.packet_count==SIZE_MAX)
            return frontend_fail(error,QA_ERROR_FORMAT,"Companion packet exceeds its genuine Source spans");
        for(size_t i=0;i<polygon_count;++i)
            if(polygons[i].first>vertex_count || polygons[i].count>vertex_count-polygons[i].first)
                return frontend_fail(error,QA_ERROR_FORMAT,"Companion polygon exceeds its actual vertex span");
        source_companion_packet *packet=calloc(1,sizeof(*packet));
        if (!packet) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining genuine companion Draw packet");
        qa_q3_ref_entity *refs=entity_count?malloc(entity_count*sizeof(*refs)):NULL;
        qa_actor_id *actors=entity_count?calloc(entity_count,sizeof(*actors)):NULL;
        bool *views=entity_count?calloc(entity_count,sizeof(*views)):NULL;
        qa_q3_scene_polygon *polys=polygon_count?malloc(polygon_count*sizeof(*polys)):NULL;
        qa_actor_id *poly_actors=polygon_count?calloc(polygon_count,sizeof(*poly_actors)):NULL;
        bool *poly_views=polygon_count?calloc(polygon_count,sizeof(*poly_views)):NULL;
        qa_scene_vertex *verts=vertex_count?malloc(vertex_count*sizeof(*verts)):NULL;
        qa_scene_light *lit=light_count?malloc(light_count*sizeof(*lit)):NULL;
        qa_actor_id *lit_actors=light_count?calloc(light_count,sizeof(*lit_actors)):NULL;
        bool *lit_views=light_count?calloc(light_count,sizeof(*lit_views)):NULL;
        packet->view=(frontend_source_companion_packet){.definition=*definition,.entities=refs,
            .entity_actors=actors,.entity_views=views,.entity_count=entity_count,
            .polygons=polys,.polygon_actors=poly_actors,.polygon_views=poly_views,.polygon_count=polygon_count,
            .vertices=verts,.vertex_count=vertex_count,.lights=lit,.light_actors=lit_actors,
            .light_views=lit_views,.light_count=light_count};
        if ((entity_count && (!refs || !actors || !views)) ||
            (polygon_count && (!polys || !poly_actors || !poly_views)) ||
            (vertex_count && !verts) || (light_count && (!lit || !lit_actors || !lit_views))) {
            companion_packet_free(packet);
            return frontend_fail(error,QA_ERROR_MEMORY,"Retaining genuine companion scene values");
        }
        if (entity_count) memcpy(refs,entities,entity_count*sizeof(*refs));
        if (polygon_count) memcpy(polys,polygons,polygon_count*sizeof(*polys));
        if (vertex_count) memcpy(verts,vertices,vertex_count*sizeof(*verts));
        if (light_count) memcpy(lit,lights,light_count*sizeof(*lit));
        if (scope->lease->equipment &&
            (!frontend_equipment_source_scene_actors(scope->lease->equipment,entity_count,actors,error) ||
             !frontend_equipment_source_scene_views(scope->lease->equipment,entity_count,views,error) ||
             !frontend_equipment_source_scene_polygons(scope->lease->equipment,polygon_count,poly_actors,poly_views,error) ||
             !frontend_equipment_source_scene_lights(scope->lease->equipment,light_count,lit_actors,lit_views,error))) {
            companion_packet_free(packet); return false;
        }
        const qa_application_q3_equipment_source_weapon *weapons=NULL;
        size_t weapon_count=0;
        if (scope->lease->equipment && !frontend_equipment_source_scene_weapons(scope->lease->equipment,
            &weapons,&weapon_count,error)) { companion_packet_free(packet); return false; }
        if (weapon_count>SIZE_MAX/sizeof(*weapons) || (weapon_count && !weapons)) {
            companion_packet_free(packet);
            return frontend_fail(error,QA_ERROR_FORMAT,"Companion completed weapon scopes exceed their actual span");
        }
        qa_application_q3_equipment_source_weapon *held=weapon_count?malloc(weapon_count*sizeof(*held)):NULL;
        if (weapon_count && !held) {
            companion_packet_free(packet);
            return frontend_fail(error,QA_ERROR_MEMORY,"Retaining completed companion weapon scopes");
        }
        if (weapon_count) memcpy(held,weapons,weapon_count*sizeof(*held));
        packet->view.weapons=held; packet->view.weapon_count=weapon_count;
        if (capture->last) capture->last->next=packet; else capture->first=packet;
        capture->last=packet; ++capture->view.packet_count;
    }
    body_scene_completed(scope->lease);
    scene_cleared(source); return true;
}
static void float_word(uint8_t *out, float value)
{
    uint32_t bits; memcpy(&bits, &value, sizeof(bits)); qa_store_u32le(out, bits);
}
bool frontend_q3_configuration(qa_frontend *frontend,uint8_t out[11332],qa_error *error)
{
    qa_display_info display;
    if (!qa_display_info_get(frontend->display, &display, error)) return false;
    memset(out, 0, 11332);
    const qa_gl_capabilities *caps = qa_gl_capabilities_get(frontend->gl);
    qa_cpu_capabilities cpu={0}; qa_q3_color_device color;
    int32_t hardware=0; uint32_t maximum=0;
    if ((!caps && !qa_cpu_capabilities_read(frontend->cpu,&cpu,error)) ||
        !frontend_q3_source_color_device_read(frontend,&color,error) ||
        !frontend_q3_renderer_hardware_read(frontend,&hardware,&maximum,error)) return false;
    snprintf((char *)out, 1024, "%s", caps ? caps->renderer : "Quake Anthology CPU renderer");
    snprintf((char *)out + 1024, 1024, "%s", caps ? caps->vendor : "Quake Anthology");
    snprintf((char *)out + 2048, 1024, "%s", caps ? caps->version : "retained scene renderer");
    /* CPU has no native texture-size limit declaration; the ABI converts
     * that absent value to zero. Buffer bit counts come from the real surface. */
    qa_store_u32le(out + 11264, maximum);
    qa_store_u32le(out + 11268, caps ? caps->texture_units : 1);
    qa_store_u32le(out + 11272, caps ? caps->color_bits : cpu.color_bits);
    qa_store_u32le(out + 11276, caps ? caps->depth_bits : cpu.depth_bits);
    qa_store_u32le(out + 11280, caps ? caps->stencil_bits : cpu.stencil_bits);
    qa_store_u32le(out + 11288,(uint32_t)hardware);
    qa_store_u32le(out + 11292, color.hardware_gamma);
    uint32_t width=frontend->cpu?frontend->width:display.drawable_width;
    uint32_t height=frontend->cpu?frontend->height:display.drawable_height;
    qa_store_u32le(out + 11304, width);
    qa_store_u32le(out + 11308, height);
    float_word(out + 11312, height ? (float)width / (float)height : 1);
    qa_store_u32le(out + 11316, display.refresh_rate > 0 ? (uint32_t)display.refresh_rate : 0);
    qa_store_u32le(out + 11320, display.fullscreen != QA_DISPLAY_WINDOWED);
    qa_store_u32le(out + 11324, caps && caps->stereo);
    return true;
}
static bool end_registration(void *context,qa_error *error)
{
    frontend_source *source=context;
    if(!source || !source->frontend || source->application!=source->frontend->application ||
        source->frontend->source_restoring || source->frontend->capture || source->frontend->resource_inventory)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source registration completion lost its actual constructor renderer");
    return frontend_source_renderer_end_registration(source->frontend,error);
}
static bool configuration(void *context,uint8_t out[11332],qa_error *error)
{
    frontend_source *source=context;
    return frontend_q3_configuration(source->frontend,out,error);
}

static bool update_screen(void *context, qa_error *error)
{
    frontend_source *source=context;
    qa_frontend *frontend=source->frontend;
    if (source->companion && source->companion->entered) {
        bool drawn=false;
        return qa_application_q3_source_loading_screen(source->application,source->owner,
            source->launch_seat,&drawn,error);
    }
    if (!frontend_q3_source_output(frontend,source->materials,frontend_viewport(frontend,source->seat),error)) return false;
    bool drawn=false;
    if (!qa_application_q3_source_loading_screen(source->application,source->owner,
        source->launch_seat,&drawn,error)) return false;
    frontend->frame.source_backend=true;
    if (!frontend_render_controls_live(frontend,error)) return false;
    if (frontend->frame.source_backend && frontend->frame.source_skip_backend) {
        if (frontend->frame.source_pending &&
            !qa_material_source_frame_end(frontend->frame.source_pending,&frontend->frame,false,error)) return false;
        return true;
    }
    if (frontend->cpu) return qa_cpu_execute(frontend->cpu, &frontend->frame, error) && qa_cpu_present_frame(frontend->cpu, error);
    return qa_gl_execute(frontend->gl, &frontend->frame, error) && qa_gl_swap(frontend->gl, error);
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
    return (uint32_t)(lease->source->frontend->wall_time_ns / UINT64_C(1000000));
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
        (!source->world || qa_scene_world_idle(source->world)) &&
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
static bool shader_movies_current(void *context,const frontend_material_movie_source *view);
static bool source_free(frontend_source *source)
{
    if (!source) return true;
    qa_frontend *frontend = source->frontend;
    qa_error retirement_error={0};
    if (frontend->capture || frontend->resource_inventory || source->retired_leases) return false;
    if (source->world_retirement && !qa_q3_presentation_retire_world_dispose(&source->world_retirement,&retirement_error)) return false;
    if (!source_idle(source) ||
        !frontend_selected_effects_idle(frontend)) return false;
    qa_error error = {0};
    if (source->companion) {
        if (source->companion->entered || source->companion->frame.source_pending) return false;
        companion_clear(source->companion); qa_scene_frame_destroy(&source->companion->frame);
        free(source->companion); source->companion=NULL;
    }
    if (frontend->music_sources && !frontend_music_sources_explicit_retire(frontend->music_sources,source,&error)) return false;
    if (source->presentation &&
        !frontend_selected_effects_retire_parent(frontend,source->presentation,&error)) return false;
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
    if (source->shader_movies && source->movies && !source->frontend->source_restoring) {
        frontend_material_movie_source expected={.frontend=source->frontend,.files=source->mounts,.images=source->images,
            .materials=source->materials,.media=source->movies,.context=source,.current=shader_movies_current};
        if (!frontend_renderer_materials_adopt_movies(source->frontend,&expected,&source->shader_movies,&source->movies,&error)) return false;
    }
    if (!frontend_material_movies_destroy(&source->shader_movies,&error)) return false;
    if (source->assets && !qa_q3_assets_services_retire(source->assets,&error)) return false;
    qa_q3_presentation_assets_destroy(source->assets); source->assets=NULL;
    qa_scene_world_destroy(source->world);
    qa_collision_destroy(source->geometry);
    qa_resource_release(source->map_resource);
    qa_q3_key_destroy(source->keys);
    source->keys=NULL;
    qa_audio_music_destroy(source->music);
    qa_media_library_destroy(source->movies);
    qa_font_library_destroy(source->fonts);
    qa_audio_bank_destroy(source->sounds);
    qa_material_library_destroy(source->materials);
    qa_scene_resources_destroy(source->images);
    qa_vfs_destroy(source->mounts); free(source->music_intro); free(source->music_loop);
    qa_launch_instance_lease_release(source->music_metadata);
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
static bool retired_client_group(const frontend_source *source,
    const qa_application_startup_source *held,const frontend_key_profile *profile)
{
    return source->owner==held->scope.provider && source->launch_seat==held->scope.seat &&
        (source->key_profile==profile || source->keys==frontend_key_profile_state(profile) ||
            (source->retiring_client_profile==profile && source->retiring_client_console==held->console &&
                source->retiring_client_cvars==held->cvars));
}
static bool retired_client_lease(const frontend_source_lease *lease,
    const qa_application_startup_source *held)
{
    return lease->console==held->console && lease->cvars==held->cvars;
}
bool frontend_source_retire_client_configuration(qa_frontend *f,qa_application *app,
    const qa_application_startup_source *held,const frontend_key_profile *profile,qa_error *error)
{
    if(!f || !app || app!=f->application || !held || !profile || !frontend_key_profile_state(profile) ||
        frontend_keys_profile(f->keys,frontend_key_profile_id(profile))!=profile || f->capture || f->resource_inventory || f->source_restoring ||
        !frontend_seat_callbacks_returned(f) || !qa_application_q3_client_configuration_retiring(app,held))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Old CLIENT drain requires its entered exact physical configuration loan");
    /* Preflight every matching group before disposing any retained child. */
    for(const frontend_source *source=f->sources;source;source=source->next) {
        if(source->owner!=held->scope.provider || source->launch_seat!=held->scope.seat) continue;
        bool matching=retired_client_group(source,held,profile);
        for(const frontend_source_lease *lease=source->lease_list;lease;lease=lease->next)
            if(matching || lease->cvars==held->cvars)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Old CLIENT drain retains an active source role lease");
        for(const frontend_source_lease *lease=source->retired_leases;lease;lease=lease->next) {
            if(!retired_client_lease(lease,held)) {
                if(matching || lease->cvars==held->cvars)
                    return frontend_fail(error,QA_ERROR_ARGUMENT,"Old CLIENT group also retains a different physical namespace");
                continue;
            }
            if(!matching) return frontend_fail(error,QA_ERROR_ARGUMENT,"Old CLIENT role differs from its retained key profile group");
        }
        if(matching && (source->leases || !source_idle(source)))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Old CLIENT group retains an entered source borrower");
    }
    frontend_source *source=f->sources;
    while(source) {
        frontend_source *next=source->next;
        if(retired_client_group(source,held,profile)) {
            source->retiring_client_profile=profile;
            source->retiring_client_console=held->console;
            source->retiring_client_cvars=held->cvars;
            source_retry_retirement(source);
        }
        source=next;
    }
    if(!qa_application_q3_client_configuration_retiring(app,held))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Old CLIENT configuration loan changed during checked drain");
    for(const frontend_source *remaining=f->sources;remaining;remaining=remaining->next) {
        if(remaining->owner!=held->scope.provider || remaining->launch_seat!=held->scope.seat) continue;
        if(retired_client_group(remaining,held,profile))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Old CLIENT drain retained its source key profile group");
        for(const frontend_source_lease *lease=remaining->lease_list;lease;lease=lease->next)
            if(lease->cvars==held->cvars)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Old CLIENT drain retained an active registry alias");
        for(const frontend_source_lease *lease=remaining->retired_leases;lease;lease=lease->next)
            if(lease->cvars==held->cvars)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Old CLIENT drain retained a checked retirement alias");
    }
    return true;
}
static bool shader_movies_current(void *context,const frontend_material_movie_source *view)
{
    frontend_source *source=context;
    bool linked=false;
    for (const frontend_source *row=source && source->frontend?source->frontend->sources:NULL;row;row=row->next)
        if (row==source) { linked=true; break; }
    return linked && source->construction_started && source->application==source->frontend->application &&
        view && view->frontend==source->frontend && view->context==source && view->files==source->mounts &&
        view->images==source->images && view->materials==source->materials && view->media==source->movies;
}
bool frontend_source_movie_source_read(qa_frontend *f,size_t index,frontend_material_movie_source *out,qa_error *error)
{
    frontend_source *source=f?f->sources:NULL;
    while (source && index--) source=source->next;
    frontend_material_movie_source view={.frontend=f,.files=source?source->mounts:NULL,.images=source?source->images:NULL,
        .materials=source?source->materials:NULL,.media=source?source->movies:NULL,.context=source,.current=shader_movies_current};
    if (!out || !shader_movies_current(source,&view))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Movie binding lacks its retained Source provider");
    *out=view; return true;
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
    if (!restoring && !frontend_q3_source_color_ensure(frontend,error)) return false;
    source->images = source->mounts ? restoring ?
        qa_scene_resources_create_detached(source->mounts, error) : qa_scene_resources_create(source->mounts, error) : NULL;
    if (source->images && !restoring && !frontend_image_policy_initialize(frontend,source->images,error)) return false;
    source->materials = source->images ? restoring ?
        qa_material_library_create_detached(source->images, error) :
        qa_material_library_create(source->images, frontend->order, error) : NULL;
    source->fonts = source->images ? qa_font_library_create(source->mounts, source->images, error) : NULL;
    source->movies = source->images ? qa_media_library_create(source->images, error) : NULL;
    if (!restoring && source->materials) {
        frontend_material_movie_source movie={.frontend=frontend,.files=source->mounts,.images=source->images,
            .materials=source->materials,.media=source->movies,.context=source,.current=shader_movies_current};
        if (!frontend_q3_material_profile_initialize(frontend,source->materials,error) ||
            !qa_material_library_set_source_upload(source->materials,frontend_q3_source_upload_read,frontend,error) ||
            !frontend_material_movies_create(&movie,&source->shader_movies,error) ||
            !frontend_source_cinematics_ensure(frontend,source->images,error) ||
            !frontend_material_movies_cinematic_attach(source->shader_movies,frontend->source_cinematics,source->seat,source->identity,error)) return false;
    }
    qa_scene_image_options images = {.family = QA_SCENE_Q3, .wrap = QA_SCENE_REPEAT,
        .filter = QA_SCENE_LINEAR_MIPMAP_LINEAR, .mipmap = true, .transparent_index = -1};
    bool ok = source->mounts && source->images && source->materials && source->fonts && source->movies &&
        (restoring || (qa_material_library_load_scripts(source->materials, source->mounts, &images, error) &&
            qa_material_library_source_shaders_initialize(source->materials,&images,error))) &&
        qa_audio_bank_create(source->mounts, &source->sounds, error) &&
        (profile || qa_q3_key_create(host->cvars, false, &source->keys, error));
    if (ok && frontend->audio) ok = qa_audio_music_create(qa_audio_engine_rate(frontend->audio), music_family(source), true, &source->music, error);
    if (ok && !restoring) ok = frontend_material_remaps(frontend, source->materials, error);
    qa_q3_presentation_asset_options assets = {.provider = {source->mounts, source->images, source->materials, QA_SCENE_Q3},
        .sounds = source->sounds, .movies = source->movies, .context = source, .print = print_source, .model_initialize = model_initialize};
    if (ok) ok = qa_q3_presentation_assets_create(&assets, &source->assets, error);
    qa_q3_presentation_options presentation = {.assets = source->assets, .audio = frontend->audio,
        .clock = {source, milliseconds}, .seat = source->seat, .owner = source->identity,
        .viewport = {0, 0, frontend->width, frontend->height}, .near_clip = 4, .far_clip = 16384,
        .identity_light = 1, .lod_scale = 5, .rail_core_width = 6, .rail_ring_width = 16, .rail_segment_length = 32,
        .context = source, .source_state=source_state, .audio_actor = source_actor, .listener = listener, .music = music,
        .frame_number = frame_number, .milliseconds = source_milliseconds, .audio_bus = audio_bus,
        .prepare_view = prepare_view, .submit_view = submit_view, .scene_cleared=scene_cleared,.scene_completed=scene_completed,
        .prepare_picture=prepare_picture,.video_frame=frontend_material_movies_frontend_resolve,.video_context=frontend,
        .remap = source_remap, .print = print_source};
    if (ok && source->shader_movies) ok=frontend_material_movies_cinematic_read(source->shader_movies,&presentation.cinematics,error);
    if (ok && !restoring) ok=frontend_q3_renderer_options_read(frontend,&presentation,error);
    if (ok && qa_cvars_find(host->cvars,"cg_shadows"))
        ok=frontend_q3_shadow_mode_read(host->cvars,&presentation.shadow_mode,error);
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
static bool music_metadata_prepare(frontend_source *source,
    const qa_application_q3_client_preparation *preparation,qa_error *error)
{
    const qa_launch_instance *descriptor=preparation?preparation->receiver_descriptor:NULL;
    if (!descriptor || !preparation->services || !preparation->receiver_catalog || !preparation->receiver_product ||
        preparation->receiver!=source->owner || preparation->seat!=source->launch_seat ||
        descriptor->content!=preparation->services->mounts ||
        qa_launch_instance_catalog(descriptor)!=preparation->receiver_catalog ||
        descriptor->selection.product!=preparation->receiver_product->id)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source soundtrack lacks its actual first role declaration");
    const qa_launch_instance *held=qa_launch_instance_lease_view(source->music_metadata);
    if (held) return held->storage==descriptor->storage && source->music_catalog==preparation->receiver_catalog &&
        source->music_product==preparation->receiver_product->id ? true :
        frontend_fail(error,QA_ERROR_ARGUMENT,"Source roles have different soundtrack declarations");
    if (!qa_launch_instance_retain_metadata(descriptor,&source->music_metadata,error)) return false;
    source->music_catalog=preparation->receiver_catalog; source->music_product=preparation->receiver_product->id;
    return true;
}
static bool create_source(qa_frontend *frontend, qa_application *application, qa_actor_owner owner,
    uint32_t seat,uint32_t launch_seat, const qa_q3_host_options *host,frontend_key_profile *profile,
    const qa_application_q3_client_preparation *preparation,frontend_source **out, qa_error *error)
{
    frontend_source *source = calloc(1, sizeof(*source));
    if (!source) return frontend_fail(error, QA_ERROR_MEMORY, "allocating source presentation owner");
    source->frontend = frontend; source->application = application; source->owner = owner; source->seat = seat;
    source->launch_seat=launch_seat;
    if (!frontend_source_identity_allocate(frontend,&source->identity,error)) { free(source); return false; }
    source->next=frontend->sources; frontend->sources=source;
    if (!music_metadata_prepare(source,preparation,error) || !construct_source(source,host,profile,false,error)) {
        frontend_source *next=source->next;
        if (source_free(source)) frontend->sources=next;
        return false;
    }
    *out=source;
    return true;
}
static bool source_map_prepare(frontend_source *source,const qa_resource *map,bool world,qa_error *error)
{
    if (!map || !source->mounts || qa_resource_pool_find(qa_vfs_resources(source->mounts),qa_resource_id(map))!=map)
        return frontend_fail(error,QA_ERROR_FORMAT,"Private source map leaves its actual retained content pool");
    if (source->map_resource)
        return source->map_resource==map && source->geometry && (!world || source->world) ? true :
            frontend_fail(error,QA_ERROR_ARGUMENT,"Private source constructor changed its retained map owner");
    qa_bsp_view bsp;
    if (!qa_bsp_open(qa_resource_bytes(map),&bsp,error)) return false;
    if (bsp.family!=QA_BSP_Q3)
        return frontend_fail(error,QA_ERROR_FORMAT,"Private Q3 receiver requires its actual Q3 collision map");
    source->map_resource=(qa_resource *)map; qa_resource_retain(source->map_resource);
    if (!qa_collision_create(&bsp,&source->geometry,error) ||
        !qa_collision_bind_resource(source->geometry,source->map_resource,error)) return false;
    if (!world) return true;
    qa_scene_world_options options={.images={.family=QA_SCENE_Q3,.wrap=QA_SCENE_REPEAT,
        .filter=QA_SCENE_LINEAR_MIPMAP_LINEAR,.mipmap=true,.transparent_index=255},
        .subdivisions=64,.q1_water_alpha=1,.q2_light_modulate=1,.q3_overbright=1};
    if (!frontend_q3_world_policy_initialize(source->frontend,&options,error)) return false;
    return qa_scene_world_create(&bsp,source->images,source->materials,&options,&source->world,error) &&
        qa_scene_world_source_resource_bind(source->world,source->map_resource,error) &&
        frontend_material_remaps(source->frontend,source->materials,error);
}
static qa_collision_geometry *source_map_geometry(void *context)
{
    const frontend_source *source=context;
    return source && source->constructed && source->leases && source->application==source->frontend->application ?
        source->geometry:NULL;
}
static bool source_map_load(void *context,const char *path,qa_error *error)
{
    frontend_source *source=context;
    if (!path || !source_map_geometry(source) || !source->map_resource)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Collision load differs from the prepared private source map");
    const char *map=qa_resource_path(source->map_resource);
    if (!map) return frontend_fail(error,QA_ERROR_FORMAT,"Private source map has no retained resource path");
    if (!strncmp(path,"maps/",5)) path+=5;
    if (!strncmp(map,"maps/",5)) map+=5;
    size_t requested=strlen(path),selected=strlen(map);
    if (requested>4 && !strcmp(path+requested-4,".bsp")) requested-=4;
    if (selected>4 && !strcmp(map+selected-4,".bsp")) selected-=4;
    if (requested!=selected || memcmp(path,map,selected))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Collision load differs from the prepared private source map");
    return true;
}
bool frontend_source_services(void *context, qa_application *application, qa_actor_owner owner,
    qa_qvm_role role, uint32_t seat, qa_q3_host_options *host, qa_error *error)
{
    qa_frontend *frontend = context;
    if (!frontend || !frontend_owners_idle(frontend))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source construction requires idle frontend parent and child owners");
    if (role == QA_QVM_GAME) {
        if (application == frontend->application &&
            qa_application_native_q3_wire_preconstruction_current(application,owner,seat,host))
            return frontend_network_source_services(frontend,host,error);
        qa_application_q3_client_preparation preparation;
        if(application!=frontend->application || !host ||
            !qa_application_q3_preconstruction_source_read(application,owner,role,seat,&preparation,error)) return false;
        frontend_config_source *configured=preparation.source_cvars?
            frontend_config_store_source(frontend->config_store,preparation.source_cvars):NULL;
        if(!configured || frontend_config_source_cvars(configured)!=host->cvars || preparation.source_cvars!=host->cvars ||
            !frontend_config_source_host_cvars(configured,&host->cvar_namespaces,error))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"GAME host namespaces require their actual retained configuration source");
        host->cvar_entry=(qa_q3_host_cvar_entry_services){host->cvar_namespaces.context,
            frontend_config_source_cvar_entered};
        return frontend_network_source_services(frontend,host,error);
    }
    uint32_t ordinal;
    if (frontend->options.dedicated || !qa_application_constructor_seat_ordinal(application,owner,seat,&ordinal,error) ||
        ordinal >= frontend->options.seats)
        return frontend_fail(error, QA_ERROR_UNSUPPORTED, "source client presentation requires an active local seat");
    qa_application_q3_client_preparation preparation;
    if (!qa_application_q3_preconstruction_source_read(application,owner,role,seat,&preparation,error)) return false;
    qa_application_startup_source client_tuple;
    if (!qa_application_q3_client_configuration_read(application,owner,role,seat,&client_tuple,error)) return false;
    if (client_tuple.console!=host->console)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client constructor changed its retained physical console");
    frontend_remote_config *client_config=frontend_config_store_client(frontend->config_store,client_tuple.cvars);
    frontend_remote_config_view client_view;
    if (client_config && (!frontend_remote_config_read(client_config,&client_view) || !client_view.ready ||
        client_view.physical_seat!=ordinal || client_view.scope.provider!=client_tuple.scope.provider ||
        client_view.scope.kind!=client_tuple.scope.kind || client_view.scope.seat!=seat ||
        client_view.console!=client_tuple.console || client_view.cvars!=client_tuple.cvars ||
        !frontend_remote_config_current(client_config,&client_view)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client constructor lost its prepared physical configuration");
    frontend_config_source *configured=preparation.source_cvars?
        frontend_config_store_source(frontend->config_store,preparation.source_cvars):NULL;
    frontend_key_profile *profile=client_config?client_view.keys:configured?frontend_config_source_keys(configured):NULL;
    if ((!client_config && frontend_network_remote(frontend)) ||
        (configured && !client_config && frontend_config_source_cvars(configured)!=preparation.source_cvars) ||
        ((client_config || configured) && (!profile || !frontend_key_profile_state(profile))))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client keys require their prepared physical configuration and registry");
    if (frontend->source_restoring && (!frontend->seats || !frontend->seats[ordinal].input || !frontend->seats[ordinal].console))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "restored source imports require actual stable input and console owners");
    if (!frontend->source_restoring && !frontend_network_remote(frontend) && !frontend_scene_sync(frontend, error)) return false;
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
        if (!music_metadata_prepare(source,&preparation,error) ||
            (!source->constructed && !construct_source(source, host,profile,true,error))) return false;
    } else {
        while (source && (source->owner != owner || source->seat != ordinal || source->launch_seat!=seat || source->source_files != host->mounts)) source = source->next;
    }
    bool created = source == NULL;
    if (created && !create_source(frontend, application, owner, ordinal,seat, host,profile,&preparation,&source,error)) return false;
    if (!music_metadata_prepare(source,&preparation,error)) return false;
    if (!frontend->source_restoring) {
        bool private_map=frontend_network_remote(frontend);
        if (!created && source->private_map!=private_map)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Source constructor changed its private collision ownership policy");
        source->private_map=private_map;
    }
    if (!frontend->source_restoring && frontend_network_remote(frontend)) {
        const qa_resource *map=NULL; bool present=false;
        bool ok=frontend_network_client_map_read(frontend,application,owner,role,seat,host->mounts,&map,&present,error) &&
            (!present || source_map_prepare(source,map,true,error));
        if (ok && !present && source->map_resource)
            ok=frontend_fail(error,QA_ERROR_ARGUMENT,"Private source constructor lost its prepared map receipt");
        if (!ok) {
            if (created) {
                frontend_source *next=source->next;
                if (source_free(source)) frontend->sources=next;
            }
            return false;
        }
    }
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
        .console = host->console,.cvars=host->cvars, .command = host->command_context,.status_visible=true};
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
    host->render=(qa_q3_host_render_services){lease,render_enter,render_leave};
    host->keys = source->keys;
    if (client_config || configured) {
        bool acquired=client_config?frontend_remote_config_acquire(client_config,&lease->registry,error):
            frontend_config_source_acquire_seat_registry(configured,seat,&lease->registry,error);
        if (!acquired) return false;
        host->cvars=frontend_client_registry_cvars(lease->registry); lease->cvars=host->cvars;
        if (!host->cvars) return frontend_fail(error,QA_ERROR_ARGUMENT,"Client constructor lost its canonical prepared registry");
    }
    if (!frontend_network_client_services(frontend,application,owner,role,seat,host,error)) return false;
    lease->common=host->common; lease->cvars=host->cvars;
    qa_application_startup_source parent_game;
    if(!qa_application_q3_client_configuration_read(application,owner,role,seat,&client_tuple,error) ||
        client_tuple.console!=host->console || client_tuple.cvars!=host->cvars ||
        (!client_config && (!configured || !frontend_config_source_tuple(configured,&parent_game))) ||
        !frontend_config_host_cvars_prepare(&lease->namespaces,frontend->config_store,application,
            &client_tuple,client_config?NULL:&parent_game,&host->cvar_namespaces,error))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT host namespaces require the adopted physical tuple and actual GAME parent");
    host->input=(qa_q3_host_input_services){.context=&lease->namespaces,
        .bindings=frontend_config_host_bindings};
    host->cvar_entry=(qa_q3_host_cvar_entry_services){&lease->namespaces,
        frontend_config_host_cvar_entered};
    if (role==QA_QVM_CGAME)
        host->cvar_status=(qa_q3_host_cvar_status_services){lease,status_visible};
    if (role==QA_QVM_CGAME && !frontend_network_remote(frontend) && !host->client.gamestate)
        host->client_time_from_game=true;
    host->seat = frontend->seats[ordinal].input;
    host->console_field = qa_seat_console_field(frontend->seats[ordinal].console, false);
    host->scene_resources = source->images; host->scene_frame = &frontend->frame;
    host->scene_world = source->private_map?source->world:frontend->scene_world;
    host->sound_bank = source->sounds;
    if (source->private_map)
        host->collision=(qa_q3_host_collision_services){source,source_map_geometry,source_map_load};
    host->presentation = (qa_q3_host_presentation_services){.context=source,.seat=source->presentation,
        .fonts=source->fonts,.configuration=configuration,.update_screen=update_screen,.end_registration=end_registration};
    host->common = (qa_q3_host_common_services){lease, common_print, common_milliseconds,
        common_calendar, host->common.arguments ? common_arguments : NULL,
        (host->common.client_command || frontend_network_remote(frontend)) ? common_command : NULL, host->common.installed_mods ? common_mods : NULL,
        common_clipboard};
    if (frontend->source_restoring) return true;
    return source_publish_backend(source, error) &&
        qa_q3_presentation_frame(source->presentation, &frontend->frame,
            (qa_scene_rect){0, 0, frontend->width, frontend->height}, error);
}
typedef struct source_body_component {
    qa_application_q3_component_draw draw;
    qa_application_q3_component_body_lease *lease;
    qa_application_q3_component_bodies bodies;
} source_body_component;
typedef struct source_body_ref {
    qa_actor_id actor;
    qa_application_q3_body_part part;
    uint32_t helper;
    qa_q3_ref_entity ref;
    const qa_resource *resource;
    bool base;
} source_body_ref;
struct source_body_draw {
    frontend_source_lease *lease;
    qa_application_q3_body_entry entry;
    source_body_component *components;
    size_t count;
    source_body_ref *refs;
    size_t ref_count;
    uint32_t first_order,component_order;
    qa_scene_light *lights,*projected_lights;
    bool components_submitted;
};
static bool body_current(void *context,const qa_application_q3_body_draw *view)
{
    frontend_source_lease *lease=context;
    source_body_draw *draw=view?view->token:NULL;
    if (!lease || !draw || draw!=lease->body_draw || draw->lease!=lease || lease->released ||
        !lease->time_busy || !lease->source->role_operations ||
        !qa_application_q3_body_entry_current(lease->source->application,&draw->entry)) return false;
    for (size_t i=0;i<draw->count;++i)
        if (!qa_application_q3_component_draw_current(lease->source->application,&draw->components[i].draw) ||
            (draw->components[i].draw.bodies && !qa_application_q3_component_bodies_current(&draw->components[i].bodies))) return false;
    return true;
}
static bool body_prepare(void *context,qa_actor_owner receiver,uint32_t seat,
    qa_application_q3_body_draw *out,qa_error *error)
{
    frontend_source_lease *lease=context; frontend_source *source=lease?lease->source:NULL;
    if (!source || !out || out->token || lease->body_draw || lease->released || lease->role!=QA_QVM_CGAME ||
        receiver!=source->owner || seat!=source->launch_seat || source->frontend->capture || source->frontend->resource_inventory ||
        lease->time_busy==SIZE_MAX || source->role_operations==SIZE_MAX)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Body Draw requires its actual retained CGAME role");
    source_body_draw *draw=calloc(1,sizeof(*draw));
    if (!draw) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual Source body Draw");
    draw->lease=lease; lease->body_draw=draw; out->token=draw;
    ++lease->time_busy; ++source->role_operations;
    if (!qa_application_q3_body_entry_read(source->application,receiver,seat,&draw->entry,error) ||
        draw->entry.context.frontend_lifetime!=lease || draw->entry.context.service_owner!=lease->service_owner)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Body Draw entry differs from its actual Source host");
    size_t count=qa_application_q3_component_scene_count(source->application);
    if (count && !draw->entry.local_source)
        return frontend_fail(error,QA_ERROR_UNSUPPORTED,"Remote Source component Draw needs its genuine received publication");
    if (count) {
        qa_application_camera_view camera;
        if (!qa_application_control_camera(source->application,draw->entry.source.source_actor,&camera) ||
            draw->entry.source.source_frame.elapsed_ns/UINT64_C(1000000)>INT32_MAX ||
            count>SIZE_MAX/sizeof(*draw->components))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Component Draw lacks its actual source viewer camera and clock");
        draw->components=calloc(count,sizeof(*draw->components));
        if (!draw->components) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual component Draw roster");
        qa_vec3 origin=qa_vec_add(camera.origin,camera.view_offset),axis[3];
        frontend_camera_axes(camera.angles,axis);
        for (size_t i=0;i<count;++i) {
            source_body_component *child=draw->components+i;
            if (!qa_application_q3_component_draw_prepare(source->application,i,source->seat,camera.actor,&origin,axis,
                draw->entry.source.source_milliseconds,(int32_t)(draw->entry.source.source_frame.elapsed_ns/UINT64_C(1000000)),
                draw->entry.source.source_frame.number,&child->draw,error)) return false;
            ++draw->count;
            if (child->draw.bodies && !qa_application_q3_component_bodies_borrow(child->draw.bodies,&child->lease,&child->bodies,error)) return false;
            if (child->draw.bodies && child->bodies.assets!=child->draw.assets)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Component body Draw changed its private numeric namespace");
            if (child->bodies.count) out->capture_active=true;
        }
    }
    return body_current(lease,out) || frontend_fail(error,QA_ERROR_ARGUMENT,"Source body Draw changed during preparation");
}
static bool body_actor(void *context,const qa_application_q3_body_draw *view,qa_actor_id actor,
    bool *hidden,bool *selected,qa_error *error)
{
    if (!hidden || !selected || !body_current(context,view))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Body selection left its genuine whole Draw receipt");
    *hidden=false; *selected=false; source_body_draw *draw=view->token;
    for (size_t i=0;i<draw->count;++i) for (size_t j=0;j<draw->components[i].bodies.count;++j) {
        qa_application_q3_component_actor body;
        if (!qa_application_q3_component_body_at(&draw->components[i].bodies,j,&body))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Component body roster changed inside Source Draw");
        if (qa_actor_id_equal(body.actor,actor)) *selected=true;
    }
    return true;
}
static bool body_submit(void *context,const qa_application_q3_body_draw *view,qa_actor_id actor,
    qa_application_q3_body_part part,uint32_t helper,const qa_q3_ref_entity *ref,bool base,bool *handled,qa_error *error)
{
    bool hidden,selected;
    if (!ref || !handled || !body_actor(context,view,actor,&hidden,&selected,error)) return false;
    *handled=false;
    if (!selected) return true;
    source_body_draw *draw=view->token;
    qa_q3_model_opening opening;
    if (ref->kind!=QA_Q3_REF_MODEL || ref->model<=0 ||
        !qa_q3_assets_model_opening(draw->lease->source->assets,(size_t)(ref->model-1),
            QA_Q3_MODEL_PRIMARY_OPENING,&opening,error)) return false;
    if (draw->ref_count==SIZE_MAX/sizeof(*draw->refs))
        return frontend_fail(error,QA_ERROR_MEMORY,"Source body capture extent overflow");
    source_body_ref *refs=realloc(draw->refs,(draw->ref_count+1)*sizeof(*refs));
    if (!refs) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining actual primary body pose");
    draw->refs=refs; refs[draw->ref_count++]=(source_body_ref){actor,part,helper,*ref,opening.resource,base};
    *handled=true; return body_current(context,view);
}
static void body_release(void *context,qa_application_q3_body_draw *view)
{
    frontend_source_lease *lease=context; source_body_draw *draw=view?view->token:NULL;
    if (!draw) return;
    for (size_t i=draw->count;i;--i) qa_application_q3_component_bodies_return(&draw->components[i-1].lease);
    if (lease->body_draw==draw) lease->body_draw=NULL;
    free(draw->components); free(draw->refs); free(draw->lights); free(draw->projected_lights); free(draw); view->token=NULL;
    time_leave(lease);
}
static void body_scene_clear(frontend_source_lease *lease)
{
    if (lease->body_draw) lease->body_draw->ref_count=0;
}
static void body_scene_completed(frontend_source_lease *lease)
{ if (lease->body_draw) lease->body_draw->components_submitted=true; }
static bool body_scene_prepare(frontend_source_lease *lease,qa_q3_scene_options *options,qa_error *error)
{
    source_body_draw *draw=lease->body_draw;
    if (!draw) return true;
    qa_application_q3_body_draw view={.token=draw};
    if (!body_current(lease,&view) || options->first_entity>=1022 || draw->ref_count>1022-options->first_entity)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Body scene order left its actual Draw inventory");
    draw->first_order=options->first_entity; options->first_entity+=(uint32_t)draw->ref_count;
    draw->component_order=options->first_entity;
    size_t entities=0,lights=0;
    if (!draw->components_submitted) for (size_t i=0;i<draw->count;++i) {
        const qa_application_q3_component_draw *child=&draw->components[i].draw;
        size_t packets;
        if (!frontend_component_scene_packet_count(lease->source->frontend,child->frontend_identity,child->sequence,&packets,error)) return false;
        for (size_t j=0;j<packets;++j) {
            frontend_component_scene_packet packet;
            if (!frontend_component_scene_packet_read(lease->source->frontend,child->frontend_identity,child->sequence,j,&packet,error)) return false;
            if (packet.entity_count>SIZE_MAX-entities || packet.light_count>SIZE_MAX-lights)
                return frontend_fail(error,QA_ERROR_MEMORY,"Component scene extent overflow");
            entities+=packet.entity_count; lights+=packet.light_count;
        }
    }
    if (entities>1022-options->first_entity || lights>SIZE_MAX-options->world.light_count ||
        lights+options->world.light_count>SIZE_MAX/sizeof(*draw->lights))
        return frontend_fail(error,QA_ERROR_MEMORY,"Component scene leaves primary submission limits");
    options->first_entity+=(uint32_t)entities;
    if (!lights) return true;
    size_t count=options->world.light_count+lights;
    qa_scene_light *merged=malloc(count*sizeof(*merged));
    qa_scene_light *projected=malloc(32*sizeof(*projected));
    if (!merged || !projected) { free(merged); free(projected); return frontend_fail(error,QA_ERROR_MEMORY,"Retaining component light submission"); }
    size_t used=options->world.light_count;
    if (used) memcpy(merged,options->world.lights,used*sizeof(*merged));
    size_t projected_count=options->world.projected_light_count;
    if (projected_count>32) { free(merged); free(projected); return frontend_fail(error,QA_ERROR_FORMAT,"Primary projected lights exceed Source limit"); }
    if (projected_count) memcpy(projected,options->world.projected_lights,projected_count*sizeof(*projected));
    for (size_t i=0;i<draw->count;++i) {
        const qa_application_q3_component_draw *child=&draw->components[i].draw;
        size_t packets;
        if (!frontend_component_scene_packet_count(lease->source->frontend,child->frontend_identity,child->sequence,&packets,error)) { free(merged); free(projected); return false; }
        for (size_t j=0;j<packets;++j) {
            frontend_component_scene_packet packet;
            if (!frontend_component_scene_packet_read(lease->source->frontend,child->frontend_identity,child->sequence,j,&packet,error)) { free(merged); free(projected); return false; }
            if (packet.light_count) memcpy(merged+used,packet.lights,packet.light_count*sizeof(*merged));
            used+=packet.light_count;
            for (size_t k=0;k<packet.light_count && projected_count<32;++k) projected[projected_count++]=packet.lights[k];
        }
    }
    free(draw->lights); free(draw->projected_lights); draw->lights=merged; draw->projected_lights=projected;
    options->world.lights=merged; options->world.light_count=count;
    options->world.projected_lights=projected; options->world.projected_light_count=projected_count;
    return body_current(lease,&view);
}
static bool body_scene_submit(frontend_source_lease *lease,const qa_q3_scene_options *options,qa_scene_frame *frame,qa_error *error)
{
    source_body_draw *draw=lease->body_draw;
    if (!draw) return true;
    qa_application_q3_body_draw view={.token=draw};
    if (!body_current(lease,&view)) return frontend_fail(error,QA_ERROR_ARGUMENT,"Body scene lost its retained component receipts");
    for (size_t i=0;i<draw->ref_count;++i) {
        const source_body_ref *primary=draw->refs+i;
        bool visible=true;
        for (size_t j=0;j<draw->count;++j) for (size_t k=0;k<draw->components[j].bodies.count;++k) {
            qa_application_q3_component_actor body;
            if (!qa_application_q3_component_body_at(&draw->components[j].bodies,k,&body)) return false;
            if (!qa_actor_id_equal(primary->actor,body.actor)) continue;
            if (!body.count) visible=false;
            for (size_t m=0;m<body.count;++m)
                if ((primary->part==QA_APPLICATION_Q3_BODY || body.parts[m].part==QA_APPLICATION_Q3_BODY ||
                    body.parts[m].part==primary->part) && !body.parts[m].base) visible=false;
        }
        uint32_t order=draw->first_order+(uint32_t)i;
        if ((!primary->base || visible) && !qa_q3_presentation_source_body_pass(lease->source->presentation,
            &primary->ref,lease->source->assets,&primary->ref,draw->entry.source.source_milliseconds,
            options,order,frame,error)) return false;
        bool posed=false;
        for (size_t j=0;j<i;++j)
            if (qa_actor_id_equal(draw->refs[j].actor,primary->actor) && draw->refs[j].part==primary->part &&
                draw->refs[j].resource==primary->resource) { posed=true; break; }
        if (posed) continue;
        for (size_t j=0;j<draw->count;++j) for (size_t k=0;k<draw->components[j].bodies.count;++k) {
            qa_application_q3_component_actor body;
            if (!qa_application_q3_component_body_at(&draw->components[j].bodies,k,&body)) return false;
            if (!qa_actor_id_equal(primary->actor,body.actor)) continue;
            for (size_t m=0;m<body.count;++m) {
                const qa_application_q3_component_part *part=body.parts+m;
                if (primary->part!=QA_APPLICATION_Q3_BODY && part->part!=QA_APPLICATION_Q3_BODY && part->part!=primary->part) continue;
                for (size_t n=0;n<part->count;++n) {
                    size_t occurrence=0; bool seen=false,equal;
                    for (size_t x=0;x<=m;++x) {
                        const qa_application_q3_component_part *prior=body.parts+x;
                        if (prior->helper!=part->helper) continue;
                        size_t limit=x==m?n:prior->count;
                        for (size_t y=0;y<limit;++y) {
                            if (!qa_q3_presentation_source_body_material_equal(lease->source->presentation,
                                draw->components[j].bodies.assets,part->passes+n,prior->passes+y,&equal,error)) return false;
                            if (equal) ++occurrence;
                        }
                    }
                    for (size_t x=0;x<m && !seen;++x) {
                        const qa_application_q3_component_part *prior=body.parts+x;
                        if (prior->helper==part->helper || (primary->part!=QA_APPLICATION_Q3_BODY &&
                            prior->part!=QA_APPLICATION_Q3_BODY && prior->part!=primary->part)) continue;
                        size_t matches=0;
                        for (size_t y=0;y<prior->count;++y) {
                            if (!qa_q3_presentation_source_body_material_equal(lease->source->presentation,
                                draw->components[j].bodies.assets,part->passes+n,prior->passes+y,&equal,error)) return false;
                            if (equal) ++matches;
                        }
                        if (matches>occurrence) seen=true;
                    }
                    if (!seen && !qa_q3_presentation_source_body_pass(lease->source->presentation,&primary->ref,
                        draw->components[j].bodies.assets,part->passes+n,draw->components[j].bodies.time_ms,
                        options,order,frame,error)) return false;
                }
            }
        }
    }
    if (!draw->components_submitted) {
        uint32_t order=draw->component_order;
        for (size_t i=0;i<draw->count;++i) {
            const source_body_component *child=draw->components+i;
            size_t packets;
            if (!frontend_component_scene_packet_count(lease->source->frontend,child->draw.frontend_identity,child->draw.sequence,&packets,error)) return false;
            for (size_t j=0;j<packets;++j) {
                frontend_component_scene_packet packet;
                if (!frontend_component_scene_packet_read(lease->source->frontend,child->draw.frontend_identity,child->draw.sequence,j,&packet,error)) return false;
                for (size_t k=0;k<packet.entity_count;++k,++order)
                    if (!qa_q3_presentation_source_component_entity(lease->source->presentation,child->draw.assets,
                        packet.entities+k,packet.definition.time,options,order,frame,error)) return false;
                for (size_t k=0;k<packet.polygon_count;++k) {
                    const qa_q3_scene_polygon *polygon=packet.polygons+k;
                    if (polygon->first>packet.vertex_count || polygon->count>packet.vertex_count-polygon->first)
                        return frontend_fail(error,QA_ERROR_FORMAT,"Component polygon leaves its retained vertices");
                    if (!qa_q3_presentation_source_component_poly(lease->source->presentation,child->draw.assets,
                        polygon->shader,packet.vertices+polygon->first,polygon->count,&polygon->fog,packet.definition.time,options,frame,error)) return false;
                }
            }
        }
    }
    return body_current(lease,&view);
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
static bool render_enter(void *context,const qa_q3_host *host,const qa_qvm_call *call,
    const qa_q3_refdef *definition,void **out,qa_error *error)
{
    frontend_source_lease *lease=context;
    frontend_source *source=lease?lease->source:NULL;
    if (!source || !out || *out || !definition || lease->released || source->render_scope ||
        source->application!=source->frontend->application || lease->time_busy==SIZE_MAX ||
        source->role_operations==SIZE_MAX || !qa_q3_host_render_scope_current(host,call,
            lease,lease->service_owner,lease->role,source->presentation))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source rendering requires its actual entered host and role lease");
    bool linked=false;
    for (frontend_source_lease *row=source->lease_list;row;row=row->next)
        if (row==lease) { linked=true; break; }
    if (!linked) return frontend_fail(error,QA_ERROR_ARGUMENT,"Source rendering lost its linked role owner");
    source_render_scope *scope=calloc(1,sizeof(*scope));
    if (!scope) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining entered source renderer scope");
    *scope=(source_render_scope){lease,host,call,definition,NULL};
    ++lease->time_busy; ++source->role_operations;
    source->render_scope=scope; *out=scope;
    if (lease->role!=QA_QVM_CGAME || source->private_map || (definition->flags&1)) return true;
    qa_application_q3_client_context client;
    if (!time_context_read(source->frontend,source->owner,source->launch_seat,&client,error)) return false;
    if (!equipment_current(lease,&client))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source effects lost their genuine CGAME context");
    if (!frontend_source_effects_begin(source->frontend,host,call,&client,source->presentation,
        source->seat,definition,&scope->effects,error)) return false;
    return render_current(source,scope) ||
        frontend_fail(error,QA_ERROR_ARGUMENT,"Source role retired during render entry");
}
static void render_leave(void *context,void *token,bool rendered)
{
    (void)rendered;
    frontend_source_lease *lease=context;
    source_render_scope *scope=token;
    if (!scope) return;
    frontend_source_effects_end(scope->effects);
    lease->source->render_scope=NULL;
    free(scope); time_leave(lease);
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
    if (lease->registry) {
        qa_application_startup_source client_tuple;
        if (!qa_application_q3_client_configuration_read(application,preparation->receiver,preparation->role,preparation->seat,
            &client_tuple,error)) return false;
        frontend_remote_config *client_config=frontend_config_store_client(f->config_store,client_tuple.cvars);
        frontend_remote_config_view client_view;
        frontend_config_source *configured=preparation->source_cvars?
            frontend_config_store_source(f->config_store,preparation->source_cvars):NULL;
        if ((!client_config && !configured) || host->cvars!=frontend_client_registry_cvars(lease->registry) ||
            lease->cvars!=host->cvars)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Client preparation changed its canonical source seat registry");
        if (client_config && (!frontend_remote_config_read(client_config,&client_view) || !client_view.ready ||
            client_view.console!=host->console || client_tuple.console!=host->console ||
            client_view.cvars!=host->cvars || client_tuple.cvars!=host->cvars ||
            client_view.scope.provider!=preparation->receiver || client_view.scope.seat!=preparation->seat ||
            client_view.scope.kind!=client_tuple.scope.kind || client_view.physical_seat!=lease->source->seat ||
            client_view.keys!=lease->source->key_profile || !frontend_remote_config_current(client_config,&client_view)))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Client preparation changed its decoded physical configuration");
        if (!client_config && preparation->restoring && !frontend_config_source_restore_seat_registry(configured,
            preparation->seat,lease->registry,NULL,error)) return false;
    }
    if (preparation->role!=QA_QVM_CGAME) return true;
    if (!preparation->body_services) return frontend_fail(error,QA_ERROR_ARGUMENT,"CGAME preparation omitted real Body services output");
    *preparation->body_services=(qa_application_q3_body_services){lease,body_prepare,body_current,body_actor,body_submit,body_release};
    if (!preparation->equipment_services || lease->equipment)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Equipment preparation requires its fresh actual CGAME output");
    frontend_equipment_source_options options={.frontend=f,.receiver=preparation->receiver,
        .seat=preparation->seat,.physical_seat=lease->source->seat,
        .assets=lease->source->assets,.presentation=lease->source->presentation,
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
    if (!owned || !destination || owned->stepping || destination->stepping || owned->resource_inventory ||
        destination->resource_inventory || !owned->application || owned->source_restoring)
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
        source->frontend = destination;
        for (frontend_source_lease *lease=source->lease_list;lease;lease=lease->next)
            frontend_equipment_source_rebind(lease->equipment,destination);
    }
}
static qa_application_q3_scene_role source_scene_role(const frontend_source_lease *lease)
{
    return (qa_application_q3_scene_role){.receiver=lease->source->owner,.role=lease->role,
        .seat=lease->source->launch_seat,.service_owner=lease->service_owner,.frontend_lifetime=lease};
}
static bool source_worlds_ready(qa_frontend *frontend,qa_scene_world *destination,
    bool restored,qa_error *error)
{
    if (!frontend || !frontend->application || frontend->capture || frontend->resource_inventory)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source world exchange requires idle installed frontend owners");
    for (frontend_source *source=frontend->sources;source;source=source->next) {
        qa_q3_presentation_binding binding;
        if (!source->constructed || source->frontend!=frontend || source->application!=frontend->application ||
            source->role_operations || !qa_q3_presentation_idle(source->presentation))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Source world exchange leaves its actual backend owner");
        if (!qa_q3_presentation_binding_read(source->presentation,&binding,error)) return false;
        if (binding.options.context!=source || binding.options.owner!=source->identity ||
            binding.options.seat!=source->seat || binding.options.assets!=source->assets ||
            (!restored && !source->private_map && destination && binding.world && binding.world!=destination) ||
            (restored && source->private_map && binding.world!=source->world))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Source backend retains a different renderer world");
        qa_scene_world *target=restored?binding.world:source->private_map?source->world:destination;
        size_t held=0;
        for (frontend_source_lease *lease=source->lease_list;lease;lease=lease->next) {
            if (lease->source!=source || lease->released || lease->time_busy || !lease->service_owner ||
                (lease->role!=QA_QVM_CGAME && lease->role!=QA_QVM_UI))
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Source world exchange leaves its linked role lease");
            qa_application_q3_scene_role role=source_scene_role(lease);
            const qa_scene_world *current=NULL;
            if (!qa_application_q3_scene_world_read(source->application,&role,&current,error)) return false;
            if (current && current!=binding.world && current!=target)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Source host retains an unrelated renderer world");
            if (!qa_application_q3_scene_world_rebind_ready(source->application,&role,current,target,error)) return false;
            ++held;
        }
        if (held!=source->leases)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Source world exchange differs from its physical role roster");
    }
    return true;
}
static void source_worlds_bind(qa_frontend *frontend,qa_scene_world *destination,bool restored)
{
    for (frontend_source *source=frontend->sources;source;source=source->next) {
        qa_q3_presentation_binding binding;
        if (restored) (void)qa_q3_presentation_binding_read(source->presentation,&binding,NULL);
        for (frontend_source_lease *lease=source->lease_list;lease;lease=lease->next) {
            qa_application_q3_scene_role role=source_scene_role(lease);
            qa_application_q3_scene_world_rebind(source->application,&role,restored?binding.world:
                source->private_map?source->world:destination);
        }
    }
}
bool frontend_source_worlds_rebind_restored(qa_frontend *frontend,qa_error *error)
{
    if (!source_worlds_ready(frontend,frontend?frontend->scene_world:NULL,true,error)) return false;
    source_worlds_bind(frontend,NULL,true); return true;
}
bool frontend_source_retire_world(qa_frontend *frontend, qa_error *error)
{
    for (frontend_source *source=frontend?frontend->sources:NULL;source;source=source->next)
        if (source->world_retirement && !qa_q3_presentation_retire_world_dispose(&source->world_retirement,error)) return false;
    if (!source_worlds_ready(frontend,NULL,false,error) || !frontend_selected_effects_idle(frontend)) return false;
    for (frontend_source *source=frontend->sources;source;source=source->next)
        if (!source->private_map &&
            !frontend_selected_effects_retire_parent(frontend,source->presentation,error)) return false;
    bool ready=true;
    for (frontend_source *source=frontend->sources;ready && source;source=source->next) {
        if (source->private_map) continue;
        if (source->world_retirement &&
            !qa_q3_presentation_retire_world_dispose(&source->world_retirement,error)) return false;
        ready=qa_q3_presentation_retire_world_prepare(source->presentation,&source->world_retirement,error);
    }
    for (frontend_source *source=frontend->sources;ready && source;source=source->next)
        if (!source->private_map) ready=qa_q3_presentation_retire_world_ready(source->world_retirement,error);
    if (!ready) {
        qa_error original=error?*error:(qa_error){0};
        for (frontend_source *source=frontend->sources;source;source=source->next) {
            qa_error cleanup={0};
            if (source->world_retirement)
                (void)qa_q3_presentation_retire_world_dispose(&source->world_retirement,&cleanup);
        }
        if (error) *error=original;
        return false;
    }
    source_worlds_bind(frontend,NULL,false);
    for (frontend_source *source=frontend->sources;source;source=source->next) {
        if (source->private_map) continue;
        qa_q3_presentation_assets *next=NULL;
        qa_q3_presentation_retire_world_commit(&source->world_retirement,&next);
        qa_q3_presentation_assets *previous=source->assets;
        source->assets=next; qa_q3_assets_release(previous);
    }
    for (frontend_source *source = frontend->sources; source; source = source->next) {
        if (source->private_map) continue;
        if (source->music_attached) {
            qa_audio_engine_remove_music(frontend->audio, source->identity);
            source->music_attached = false;
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
        if (source->music_attached) { source->music_attached=false; }
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
                &frontend->frame, source->private_map?source->world:frontend->scene_world,
                source->private_map?source->geometry:geometry, error))
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
            source->music_attached = false;
        } else if (source->music) qa_audio_music_stop(source->music);
        free(source->music_intro); free(source->music_loop);
        source->music_intro=source->music_loop=NULL; source->music_looping=false;
    }
    return true;
}
static bool source_publish_backend(frontend_source *source,qa_error *error)
{
    qa_frontend *frontend=source->frontend;
    qa_bsp_view bsp;
    qa_scene_world *world=source->private_map?source->world:frontend->scene_world;
    const qa_resource *map=source->private_map?source->map_resource:frontend->map_resource;
    qa_collision_geometry *geometry=source->private_map?source->geometry:
        qa_world_geometry(qa_application_world(source->application));
    if (!world || !map) return source->private_map?
        qa_q3_presentation_world(source->presentation,NULL,NULL,(qa_bytes){0},error):true;
    if (!qa_bsp_open(qa_resource_bytes(map), &bsp, error)) return false;
    return qa_q3_presentation_world(source->presentation,world,geometry,bsp.lumps[QA_BSP_ENTITIES].bytes,error);
}
bool frontend_source_publish_world(qa_frontend *frontend, qa_error *error)
{
    if (!source_worlds_ready(frontend,frontend?frontend->scene_world:NULL,false,error)) return false;
    for (frontend_source *source = frontend->sources; source; source = source->next)
        if (!source_publish_backend(source,error)) return false;
    source_worlds_bind(frontend,frontend->scene_world,false);
    return true;
}
static frontend_source_lease *companion_lease(const qa_frontend *f,
    const qa_application_q3_arsenal_client *receipt)
{
    if (!f || !receipt) return NULL;
    for (frontend_source *source=f->sources;source;source=source->next)
        for (frontend_source_lease *lease=source->lease_list;lease;lease=lease->next)
            if (receipt->client.context.frontend_lifetime==lease && !lease->released &&
                source->constructed && source->frontend==f && source->application==f->application &&
                source->owner==receipt->client.source.receiver && source->launch_seat==receipt->client.source.seat &&
                lease->role==QA_QVM_CGAME && lease->source==source &&
                lease->service_owner==receipt->client.context.service_owner &&
                lease->console==receipt->client.context.console && lease->cvars==receipt->client.context.cvars)
                return lease;
    return NULL;
}
static bool companion_capture(qa_frontend *f,uint32_t physical,qa_scene_rect rect,qa_error *error)
{
    for (frontend_source *row=f->sources;row;row=row->next)
        if (row->seat==physical && row->companion) {
            if (row->companion->entered || row->companion->frame.source_pending)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Companion output still owns its previous Draw queue");
            companion_clear(row->companion);
        }
    qa_actor_id actor; uint32_t seat;
    if (!frontend_seat_launch_id_read(f,physical,&seat) ||
        !qa_application_player_actor(f->application,seat,&actor)) return true;
    qa_application_q3_arsenal_client receipt; bool present=false;
    if (!qa_application_q3_arsenal_client_read(f->application,actor,seat,&receipt,&present,error)) return false;
    if (!present) return true;
    frontend_source_lease *lease=companion_lease(f,&receipt);
    if (!lease || lease->source->seat!=physical || lease->status_host || lease->time_busy ||
        lease->source->role_operations || !qa_application_q3_arsenal_client_current(f->application,&receipt))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Companion Draw lost its genuine physical CGAME lease");
    frontend_source *source=lease->source;
    qa_q3_presentation_binding previous;
    if (!qa_q3_presentation_binding_read(source->presentation,&previous,error)) return false;
    if (!source->companion) {
        source->companion=calloc(1,sizeof(*source->companion));
        if (!source->companion) return frontend_fail(error,QA_ERROR_MEMORY,"Creating private companion output");
        qa_scene_frame_init(&source->companion->frame,source->identity);
    }
    source_companion *capture=source->companion;
    qa_scene_frame_reset(&capture->frame,f->frame_number);
    if (!qa_scene_frame_material_order(&capture->frame,f->order,error) ||
        !qa_q3_presentation_frame(source->presentation,&capture->frame,rect,error)) return false;
    capture->view=(frontend_source_companion_view){.receipt=receipt,.assets=source->assets,
        .frame_sequence=f->frame_number,.source_time_ms=receipt.client.source.source_milliseconds};
    bool backend=f->frame.source_backend,skip=f->frame.source_skip_backend,clear=f->frame.source_clear_draw_buffer;
    bool visible=lease->status_visible;
    lease->status_visible=false; lease->status_host=receipt.client.host;
    ++lease->time_busy; ++source->role_operations; capture->entered=true;
    qa_error draw_error={0},close_error={0};
    bool drawn=qa_application_q3_arsenal_client_draw(f->application,&receipt,&draw_error);
    capture->entered=false;
    bool closed=true;
    if (capture->frame.source_pending)
        closed=qa_material_source_frame_end(capture->frame.source_pending,&capture->frame,false,&close_error);
    if (closed && !qa_q3_presentation_frame(source->presentation,previous.frame,previous.options.viewport,&close_error)) closed=false;
    f->frame.source_backend=backend; f->frame.source_skip_backend=skip; f->frame.source_clear_draw_buffer=clear;
    lease->status_visible=visible;
    if (closed && (!qa_application_q3_arsenal_client_current(f->application,&receipt) ||
        !qa_q3_host_cvar_cache_refresh(receipt.client.host,&close_error))) {
        closed=false;
        if (!close_error.code) frontend_fail(&close_error,QA_ERROR_ARGUMENT,"Companion Draw changed its actual host on return");
    }
    lease->status_host=NULL; time_leave(lease);
    capture->completed=drawn && closed;
    if (!capture->completed) {
        if (error) *error=drawn?close_error:draw_error;
        return false;
    }
    return true;
}
bool frontend_source_companion_current(const qa_frontend *f,const frontend_source_companion_view *view)
{
    frontend_source_lease *lease=view?companion_lease(f,&view->receipt):NULL;
    source_companion *capture=lease?lease->source->companion:NULL;
    return capture && capture->completed && !capture->entered && !capture->frame.source_pending &&
        capture->view.frame_sequence==f->frame_number && view->frame_sequence==capture->view.frame_sequence &&
        view->assets==capture->view.assets && view->assets==lease->source->assets &&
        view->source_time_ms==capture->view.source_time_ms && view->packet_count==capture->view.packet_count &&
        capture->view.receipt.client.host==view->receipt.client.host &&
        qa_actor_id_equal(capture->view.receipt.actor,view->receipt.actor) &&
        qa_application_q3_arsenal_client_current(f->application,&view->receipt);
}
bool frontend_source_companion_presentation_read(const qa_frontend *f,const frontend_source_companion_view *view,
    qa_q3_presentation **out,qa_error *error)
{
    if (out) *out=NULL;
    frontend_source_lease *lease=view?companion_lease(f,&view->receipt):NULL;
    qa_q3_presentation_binding binding;
    if (!out || !lease || !frontend_source_companion_current(f,view) ||
        !qa_q3_presentation_binding_read(lease->source->presentation,&binding,error) ||
        binding.options.assets!=view->assets || binding.frame!=&f->frame ||
        binding.options.seat!=lease->source->seat)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Companion receiver lost its genuine returned presentation and physical frame");
    *out=lease->source->presentation;
    return true;
}
bool frontend_source_companion_read(const qa_frontend *f,uint32_t physical,qa_actor_id actor,
    frontend_source_companion_view *out,bool *present,qa_error *error)
{
    if (!f || !out || !present || physical>=f->options.seats)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Companion output requires its actual physical recipient");
    *present=false; *out=(frontend_source_companion_view){0};
    for (frontend_source *source=f->sources;source;source=source->next) {
        source_companion *capture=source->companion;
        if (source->seat!=physical || !capture || !capture->completed ||
            !qa_actor_id_equal(capture->view.receipt.actor,actor)) continue;
        if (*present || !frontend_source_companion_current(f,&capture->view))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Companion output has a stale or ambiguous completed Source packet");
        *out=capture->view; *present=true;
    }
    return true;
}
bool frontend_source_companion_packet_read(const qa_frontend *f,const frontend_source_companion_view *view,
    size_t ordinal,frontend_source_companion_packet *out,qa_error *error)
{
    if (!out || !frontend_source_companion_current(f,view))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Companion packet lost its completed Source receipt");
    frontend_source_lease *lease=companion_lease(f,&view->receipt);
    const source_companion_packet *packet=lease->source->companion->first;
    while (packet && ordinal--) packet=packet->next;
    if (!packet) return frontend_fail(error,QA_ERROR_ARGUMENT,"Companion packet ordinal exceeds actual completed scenes");
    *out=packet->view; return true;
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
    return companion_capture(frontend,seat,rect,error);
}
static bool present_without_cgame_lease(qa_frontend *f,uint32_t seat,uint32_t real_time,uint32_t client_time,qa_error *error)
{
    qa_error draw={0},flush={0};
    bool drawn=qa_application_present(f->application,seat,real_time,client_time,&draw);
    qa_render_controls *controls=f->cpu?qa_cpu_render_controls(f->cpu):f->gl?qa_gl_render_controls(f->gl):NULL;
    qa_material_source_scratch *scratch=controls?qa_render_controls_source_scratch(controls,&flush):NULL;
    bool flushed=scratch && qa_material_source_frame_end(scratch,&f->frame,drawn,&flush);
    if (!drawn || !flushed) { if (error) *error=drawn?flush:draw; return false; }
    return true;
}
static bool component_pictures(frontend_source_lease *lease,qa_error *error)
{
    qa_frontend *f=lease->source->frontend; qa_actor_id viewer;
    if (!frontend_seat_actor_read(f,lease->source->seat,&viewer)) return true;
    for(size_t i=0;i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view row;
        if (!frontend_component_scene_read(f,i,&row,error)) return false;
        if (row.retired || row.origin!=APPLICATION_Q3_COMPONENT_SCENE_LOCAL || !row.begun ||
            row.physical_seat!=lease->source->seat || !qa_actor_id_equal(row.viewer,viewer)) continue;
        if (!qa_application_q3_component_scene_hud(f->application,row.identity,row.physical_seat,
            viewer,row.sequence,error)) return false;
        if (!frontend_component_scene_pictures(f,row.identity,row.sequence,lease->source->presentation,&f->frame,error)) return false;
    }
    return true;
}
bool frontend_source_present(qa_frontend *f,uint32_t physical,uint32_t seat,
    uint32_t real_time,uint32_t client_time,bool *hud_drawn,qa_error *error)
{
    qa_application_presentation_view selected;
    if (!f || physical>=f->options.seats || f->capture || !frontend_sources_idle(f) ||
        !qa_application_presentation_read(f->application,seat,&selected))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source Draw requires its returned physical presentation seat");
    frontend_source_lease *lease=NULL;
    for (frontend_source *source=f->sources;source;source=source->next)
        if (source->owner==selected.hud && source->seat==physical && source->launch_seat==seat)
            for (frontend_source_lease *row=source->lease_list;row;row=row->next)
                if (row->role==QA_QVM_CGAME && !row->released) {
                    if (lease) return frontend_fail(error,QA_ERROR_ARGUMENT,"Source Draw has ambiguous CGAME leases");
                    lease=row;
                }
    if (!lease) return present_without_cgame_lease(f,seat,real_time,client_time,error);
    bool loading=false;
    if (!qa_application_q3_role_loading(f->application,lease->source->owner,QA_QVM_CGAME,seat,&loading,error)) return false;
    if (loading) return present_without_cgame_lease(f,seat,real_time,client_time,error);
    qa_application_q3_client_host local={0};
    qa_application_network_q3_cgame_host remote={0};
    qa_q3_host *host=NULL; qa_q3_host_client_context context; bool present=false;
    bool network=frontend_network_remote(f);
    if (network) {
        frontend_network_client_domain domain;
        if (!frontend_network_client_domain_read(f,&domain,error) ||
            !qa_application_network_q3_cgame_host_read(f->application,&domain.source,&remote,&present,error)) return false;
        host=remote.host; context=remote.context;
    } else {
        if (!qa_application_q3_client_host_read(f->application,lease->source->owner,seat,&local,&present,error)) return false;
        host=local.host; context=local.context;
    }
    if (!present || !host || context.frontend_lifetime!=lease || context.service_owner!=lease->service_owner ||
        context.console!=lease->console || context.cvars!=lease->cvars || context.owner!=lease->source->owner ||
        context.role!=QA_QVM_CGAME || lease->status_host || lease->time_busy==SIZE_MAX ||
        lease->source->role_operations==SIZE_MAX)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Source Draw differs from its retained actual CGAME host");
    if (!frontend_q3_source_output(f,lease->source->materials,frontend_viewport(f,physical),error)) return false;
    bool previous=lease->status_visible;
    lease->status_visible=selected.hud==lease->source->owner; lease->status_host=host;
    ++lease->time_busy; ++lease->source->role_operations;
    qa_error draw_error={0},refresh_error={0};
    bool drawn=qa_application_present(f->application,seat,real_time,client_time,&draw_error);
    if (drawn) drawn=component_pictures(lease,&draw_error);
    qa_error flush_error={0};
    qa_material_source_scratch *scratch=source_scratch(lease->source,&flush_error);
    bool flushed=scratch && qa_material_source_frame_end(scratch,&f->frame,drawn,&flush_error);
    if (!f->frame.source_pending && !frontend_component_scene_pictures_finish(f,physical,drawn && flushed,&flush_error)) flushed=false;
    if (!flushed && drawn) { drawn=false; draw_error=flush_error; }
    lease->status_visible=previous;
    bool retained=network?qa_application_network_q3_cgame_host_current(f->application,&remote):
        qa_application_q3_client_host_current(f->application,&local);
    bool refreshed=retained && qa_q3_host_cvar_cache_refresh(host,&refresh_error);
    if (!retained) frontend_fail(&refresh_error,QA_ERROR_ARGUMENT,"Status restoration lost its actual CGAME host receipt");
    lease->status_host=NULL; time_leave(lease);
    if (!drawn && !refreshed) {
        qa_error_set(error,draw_error.code?draw_error.code:QA_ERROR_ARGUMENT,draw_error.offset,
            "CGAME Draw: %.100s; status restoration: %.100s",draw_error.message,refresh_error.message);
        return false;
    }
    if (!drawn || !refreshed) { if (error) *error=drawn?refresh_error:draw_error; return false; }
    *hud_drawn=true;
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
    if (!frontend_network_local_groups_retire(frontend,error)) return false;
    if (frontend->music_sources && !frontend_music_sources_world_retire(frontend->music_sources,error)) return false;
    return frontend_campaign_ready(frontend) && frontend_tools_before_world_change(frontend, error) &&
        frontend_campaign_destroy(frontend,error);
}
bool frontend_world_change_ready(void *context, qa_application *application, qa_error *error)
{
    qa_frontend *frontend = context; (void)application;
    if (!frontend_cinematic_capture_ready(frontend))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Finish or skip the cinematic before changing worlds");
    return frontend_owners_idle(frontend) && frontend_campaign_ready(frontend) ?
        frontend_tools_world_change_ready(frontend,error) && frontend_network_world_change_ready(frontend,error) &&
        (!frontend->audio || qa_audio_engine_acoustics_release(frontend->audio,error)) :
        frontend_fail(error,QA_ERROR_ARGUMENT,"World change requires idle frontend child owners");
}
bool frontend_world_retired(void *context, qa_application *application, qa_error *error)
{
    qa_frontend *frontend = context; (void)application;
    if (!frontend_owners_idle(frontend))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"World retirement requires idle frontend child owners");
    if (!frontend_selected_effects_retire(frontend,error)) return false;
    qa_scene_frame_reset(&frontend->frame, frontend->frame_number);
    if (!frontend_native_q3_retire_world(frontend,error)) return false;
    frontend_native_q2_retire_world(frontend);
    for (unsigned i = 0; i < frontend->options.seats && !frontend->options.dedicated; ++i) {
        if (frontend->seats[i].rankings &&
            !qa_ui_rankings_reset_binding(frontend->seats[i].rankings, error)) return false;
        frontend_player_retire(&frontend->seats[i]);
    }
    if (!frontend_shader_retire(frontend, error)) return false;
    if (!frontend_equipment_retire(frontend,error)) return false;
    if (!frontend_equipment_q3_retire(frontend,error)) return false;
    if (!frontend_equipment_gear_retire(frontend,error)) return false;
    if (!frontend_selected_character_retire(frontend,error)) return false;
    frontend_visuals_destroy(frontend);
    if (!frontend_source_retire_world(frontend, error)) return false;
    frontend_particle_retire(frontend);
    frontend_qc_rerelease_retire_world(frontend);
    if (!frontend_event_retire_checked(frontend,error)) return false;
    frontend_audio_retire_round_aliases(frontend);
    frontend->silent_audio_remainder = 0;
    if (frontend->music_sources && !frontend_music_sources_output(frontend->music_sources,FRONTEND_MUSIC_MENU,error)) return false;
    for (size_t i=0;i<frontend_remote_q1_count(frontend);++i) {
        frontend_remote_q1 *row=frontend_remote_q1_at(frontend,i);
        if (!row || !remote_q1_effects_audio_detach(row,error)) return false;
    }
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
    uint32_t launch_seat; qa_application_presentation_view selected;
    if (!frontend_seat_launch_id_read(frontend,seat,&launch_seat) ||
        !qa_application_presentation_read(frontend->application,launch_seat,&selected)) {
        *out=0; return true;
    }
    qa_actor_owner receiver=0;
    for (const frontend_source *source=frontend->sources;source;source=source->next) {
        if (source->seat!=seat || source->launch_seat!=launch_seat || source->owner!=selected.hud || !source->leases) continue;
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
static bool source_group_read(const qa_frontend *frontend,size_t index,frontend_source_group_view *out,
    const frontend_video_guests *video)
{
    if (!frontend || !out || (video?!frontend_video_guests_resources_associated(frontend,video):frontend->stepping)) return false;
    const frontend_source *source=frontend->sources;
    while (source && index--) source=source->next;
    if (!source || !source->constructed || source->frontend!=frontend || source->application!=frontend->application) return false;
    frontend_source_group_view view={.owner=source->owner,.seat=source->seat,.launch_seat=source->launch_seat,.identity=source->identity,
        .source_files=source->source_files,.mounts=source->mounts,.images=source->images,.materials=source->materials,
        .fonts=source->fonts,.sounds=source->sounds,.movies=source->movies,.keys=source->keys,
        .assets=source->assets,.presentation=source->presentation,.listener=source->listener,
        .map_resource=source->map_resource,.geometry=source->geometry,.world=source->world,
        .private_map=source->private_map,
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
bool frontend_source_group_read(const qa_frontend *frontend,size_t index,frontend_source_group_view *out)
{ return source_group_read(frontend,index,out,NULL); }
bool frontend_source_group_video_read(const qa_frontend *frontend,size_t index,frontend_source_group_view *out,
    const frontend_video_guests *video)
{ return video && source_group_read(frontend,index,out,video); }
bool frontend_source_acoustics_shared(const qa_frontend *f,bool *out,qa_error *error)
{
    if (!f || !out || !f->application) return frontend_fail(error,QA_ERROR_ARGUMENT,"Acoustic source roster requires its actual frontend");
    *out=true;
    for (const frontend_source *source=f->sources;source;source=source->next) {
        if (!source->constructed || source->frontend!=f || source->application!=f->application ||
            source->role_operations || (!source->map_resource!=!source->geometry))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Acoustic source roster retains an incomplete or entered role");
        for (const frontend_source_lease *lease=source->lease_list;lease;lease=lease->next)
            if (lease->source!=source || lease->released || lease->time_busy || lease->body_draw)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Acoustic source roster retains its actual Draw borrower");
        if (source->private_map) *out=false;
    }
    return true;
}
bool frontend_source_acoustics_private_current(const qa_frontend *f,uint32_t physical,
    const qa_q3_host_collision_view *view)
{
    if (!f || !view || physical>=f->options.seats || !f->application) return false;
    for (const frontend_source *source=f->sources;source;source=source->next) {
        if (!source->constructed || source->frontend!=f || source->application!=f->application ||
            source->seat!=physical || !source->private_map || source->geometry!=view->geometry ||
            source->map_resource!=view->map_resource || source->owner!=view->receiver) continue;
        for (const frontend_source_lease *lease=source->lease_list;lease;lease=lease->next)
            if (!lease->released && lease->source==source && lease->role==QA_QVM_CGAME &&
                lease->service_owner==view->service_owner && view->frontend_lifetime==lease)
                return true;
    }
    return false;
}
bool frontend_source_acoustics_private_hold(qa_frontend *f,uint32_t physical,
    qa_q3_host_collision_scene **out,bool *private_scene,qa_error *error)
{
    if (!f || !f->application || physical>=f->options.seats || !out || *out || !private_scene)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Private acoustics requires its actual physical Source seat");
    *private_scene=false;
    const frontend_source *selected=NULL;
    const frontend_source_lease *role=NULL;
    bool requires_private=false;
    for (const frontend_source *source=f->sources;source;source=source->next) {
        if (source->seat!=physical || !source->private_map) continue;
        requires_private=true;
        for (const frontend_source_lease *lease=source->lease_list;lease;lease=lease->next) {
            if (lease->role!=QA_QVM_CGAME || lease->released) continue;
            if (selected || !source->constructed || source->frontend!=f ||
                source->application!=f->application || lease->source!=source ||
                source->role_operations || lease->time_busy || lease->body_draw)
                return frontend_fail(error,QA_ERROR_ARGUMENT,"Private acoustics retains an ambiguous or entered Source role");
            selected=source; role=lease;
        }
    }
    if (!selected) return !requires_private || frontend_fail(error,QA_ERROR_UNSUPPORTED,
        "Private Source seat has no completed CG acoustic receiver");
    *private_scene=true;
    bool present=false;
    if (!qa_application_q3_collision_scene_hold(f->application,selected->owner,
        selected->launch_seat,role->service_owner,out,&present,error)) return false;
    if (!present) return frontend_fail(error,QA_ERROR_UNSUPPORTED,
        "Private Source CGAME has no declared collision scene");
    qa_q3_host_collision_view view;
    if (!qa_q3_host_collision_read(*out,&view,&present,error) ||
        !frontend_source_acoustics_private_current(f,physical,&view)) {
        qa_q3_host_collision_release(*out); *out=NULL;
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Private acoustic hold changed its actual Source map or role");
    }
    return true;
}
bool frontend_source_group_q3_ready(const qa_frontend *f,size_t index,
    const qa_q3_presentation_options *p,const qa_q3_presentation_asset_options *a,qa_error *error)
{
    const frontend_source *source=f?f->sources:NULL;
    while (source && index--) source=source->next;
    if (!f || !f->application || f->stepping || !source || !source->constructed ||
        source->frontend!=f || source->application!=f->application || !source->leases || !p || !a ||
        p->assets!=source->assets || p->audio!=f->audio || p->clock.context!=source || p->clock.sample!=milliseconds ||
        p->seat!=source->seat || p->owner!=source->identity || p->context!=source || p->source_state!=source_state ||
        p->far_clip!=16384 || p->lod_scale!=5 || p->lod_bias != 0.0f ||
        p->rail_core_width!=6 || p->rail_ring_width!=16 || p->rail_segment_length!=32 ||
        p->audio_actor!=source_actor || p->listener!=listener || p->music!=music ||
        p->frame_number!=frame_number || p->milliseconds!=source_milliseconds || p->audio_bus!=audio_bus ||
        p->system_movie || p->prepare_picture!=prepare_picture ||
        p->video_frame!=frontend_material_movies_frontend_resolve || p->video_context!=f ||
        p->prepare_view!=prepare_view || p->submit_view!=submit_view || p->scene_cleared!=scene_cleared ||
        p->scene_completed!=scene_completed ||
        p->remap!=source_remap || p->print!=print_source ||
        a->provider.mounts!=source->mounts || a->provider.images!=source->images ||
        a->provider.materials!=source->materials || a->provider.family!=QA_SCENE_Q3 ||
        a->sounds!=source->sounds || a->movies!=source->movies || a->zero_sound ||
        a->context!=source || a->select || a->print!=print_source || a->model_initialize!=model_initialize)
        return frontend_fail(error,QA_ERROR_FORMAT,"Q3 source policy differs from its genuine installed frontend group");
    return true;
}
static bool source_group_role_read(const qa_frontend *frontend,size_t group,size_t index,
    frontend_source_role_identity *out,const frontend_video_guests *video)
{
    if (!frontend || !out || (video?!frontend_video_guests_resources_associated(frontend,video):frontend->stepping)) return false;
    const frontend_source *source = frontend->sources;
    while (source && group--) source = source->next;
    if (!source || !source->constructed || source->frontend != frontend || source->application != frontend->application)
        return false;
    const frontend_source_lease *lease = source->lease_list;
    while (lease && index--) lease = lease->next;
    if (!lease || lease->source != source || !lease->service_owner) return false;
    *out = (frontend_source_role_identity){lease->role, lease->service_owner}; return true;
}
bool frontend_source_group_role_read(const qa_frontend *frontend,size_t group,size_t index,
    frontend_source_role_identity *out)
{ return source_group_role_read(frontend,group,index,out,NULL); }
bool frontend_source_group_role_video_read(const qa_frontend *frontend,size_t group,size_t index,
    frontend_source_role_identity *out,const frontend_video_guests *video)
{ return video && source_group_role_read(frontend,group,index,out,video); }
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
bool frontend_source_client_registry_reference(void *context,const qa_cvars *registry,
    const char **instance,uint32_t *seat,bool *found,qa_error *error)
{
    qa_frontend *frontend=context;
    if (!frontend || !registry || !instance || !seat || !found)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client registry reference requires its actual frontend and outputs");
    *found=false;
    const frontend_client_registry *owner=frontend_client_registry_lookup(frontend,registry);
    if (!owner) return true;
    const qa_launch_instance *source;
    if (!frontend_client_registry_source(owner,&source,seat) || !source->selection.instance)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Client registry lost its actual retained source constructor");
    *instance=source->selection.instance; *found=true; return true;
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
bool frontend_source_role_geometry_read(const qa_frontend *frontend,qa_actor_owner receiver,
    qa_qvm_role role,uint32_t seat,uint64_t service,const qa_collision_geometry **out,
    const qa_resource **map,bool *present,qa_error *error)
{
    if (!frontend || !frontend->application || !receiver || !service || !out || !map || !present ||
        (role!=QA_QVM_CGAME && role!=QA_QVM_UI))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Private collision read requires its actual source role tuple");
    const frontend_source *selected=NULL;
    const frontend_source_lease *selected_lease=NULL;
    for (const frontend_source *source=frontend->sources;source;source=source->next) {
        if (!source->constructed || source->frontend!=frontend || source->application!=frontend->application ||
            source->owner!=receiver || source->launch_seat!=seat) continue;
        for (const frontend_source_lease *lease=source->lease_list;lease;lease=lease->next)
            if (!lease->released && lease->source==source && lease->role==role && lease->service_owner==service) {
                if (selected) return frontend_fail(error,QA_ERROR_ARGUMENT,"Private collision role tuple is ambiguous");
                selected=source;
                selected_lease=lease;
            }
    }
    if (!selected || (!selected->map_resource!=!selected->geometry) ||
        (selected->map_resource && qa_resource_pool_find(qa_vfs_resources(selected->mounts),
            qa_resource_id(selected->map_resource))!=selected->map_resource))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Private collision leaves its actual linked source owner");
    qa_application_q3_scene_role actual=source_scene_role(selected_lease);
    const qa_scene_world *world=NULL;
    if (!qa_application_q3_scene_world_read(frontend->application,&actual,&world,error)) return false;
    if (selected->private_map && world!=selected->world)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Private collision host retains a different source world");
    *out=selected->geometry; *map=selected->map_resource; *present=selected->geometry!=NULL; return true;
}
bool frontend_source_world_adopt_ready(qa_frontend *frontend,size_t index,qa_scene_world *world,qa_error *error)
{
    frontend_source *source=frontend?frontend->sources:NULL;
    while (source && index--) source=source->next;
    if (!frontend || !frontend->source_restoring || frontend->capture || frontend->stepping ||
        !source || !source->constructed || source->world || !source->map_resource || !source->geometry ||
        !world || !qa_scene_world_idle(world) || qa_scene_world_resource_owner(world)!=source->images ||
        qa_scene_world_material_owner(world)!=source->materials)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Private source world adoption requires its decoded root and paired heaps");
    return true;
}
void frontend_source_world_adopt(qa_frontend *frontend,size_t index,qa_scene_world *world)
{
    frontend_source *source=frontend->sources;
    while (index--) source=source->next;
    source->world=world;
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
