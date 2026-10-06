#include "remote_q3_runtime.h"
#include "remote_q3_private.h"
#include "remote_q3_commands.h"
#include "shared_render_controls.h"
#include "q3_render_policy.h"
#include "music_sources.h"
#include "material_movies.h"
#include "qa/audio_music_prepare.h"
#include "capture.h"
#include "save_private.h"
#include "qa/q3_presentation_save.h"
#include "../application/native_q3_remote_client_settings.h"
#include "../../presentation/q3_native/loading.h"
#include "../../presentation/q3_native/packet_remote.h"
#include "../../presentation/q3_native/marks.h"
#include "../../presentation/q3_native/local_entities.h"
#include "../../presentation/q3_native/player_state_internal.h"
#include "qa/bsp.h"
#include "qa/scene_marks.h"
#include "qa/material_source_scratch.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>

typedef struct remote_trace_receipt {
    const qa_trace_result *output;
    qa_trace_result value;
    int32_t number;
} remote_trace_receipt;
struct frontend_remote_q3_runtime {
    frontend_remote_q3 *parent;
    qa_frontend *frontend;
    frontend_remote_q3_services_view services;
    frontend_remote_q3_runtime_owners children;
    frontend_remote_q3_frame *frames;
    frontend_remote_q3_commands *console;
    q3n_loading *loading;
    const q3n_remote_frame *entered;
    q3n_native_frame_options settings;
    qa_q3_refdef refdef;
    qa_vec3 view_angles;
    qa_audio_music *music;
    char *music_intro, *music_loop;
    qa_audio_listener listener;
    remote_trace_receipt *traces;
    size_t trace_count, trace_capacity;
    int32_t old_time, prepared_old_time, frame_milliseconds, client_frame;
    uint32_t stereo;
    bool attached, complete, initialized, faulted, busy, retiring, prepared, prediction_prepared, information_prepared, restoring;
    bool music_attached, music_looping, has_listener, rendered;
};

static bool fail(qa_error *e,qa_status status,const char *text)
{ return frontend_fail(e,status,text); }
static int32_t word(uint32_t n) { int32_t out; memcpy(&out,&n,sizeof(out)); return out; }
static int32_t subtract(int32_t a,int32_t b) { return word((uint32_t)a-(uint32_t)b); }
static float product(float a,float b) { float v=a*b; return v; }
static float sum(float a,float b) { float v=a+b; return v; }
static bool attached(const frontend_remote_q3_runtime *o)
{ return o && o->attached && frontend_remote_q3_runtime_read(o->parent)==o; }
static bool current(frontend_remote_q3_runtime *o,const q3n_remote_frame *r,qa_error *e)
{
    frontend_remote_q3_resources resources;
    return attached(o) && !o->retiring && !o->faulted && !o->restoring && r &&
        o->frames && r->context==o->frames &&
        r->snapshots.owner==frontend_remote_q3_frame_snapshots(o->frames) &&
        r->client==o->services.client && r->source.owner==o->services.source &&
        r->source.basis.application==o->frontend->application &&
        frontend_remote_q3_resources_read(o->parent,&resources,e) &&
        resources.assets==o->services.resources.assets && resources.identity==o->services.resources.identity &&
        resources.physical_seat==o->services.resources.physical_seat &&
        resources.geometry==r->source.basis.geometry && resources.map==r->source.basis.map &&
        q3n_remote_frame_current(r);
}
static bool cut(frontend_remote_q3_runtime *o,const q3n_frame *f,qa_error *e)
{
    return f && f->remote && current(o,f->remote,e) && q3n_frame_current(f) &&
        f->assets==o->services.resources.assets && f->presentation==o->children.presentation &&
        f->clients==o->services.clients && f->media==o->services.media &&
        f->events==o->children.events && f->weapons==o->children.weapons &&
        f->server_commands==o->children.commands;
}
static int32_t milliseconds(void *context)
{ return word((uint32_t)(((frontend_remote_q3_runtime *)context)->frontend->wall_time_ns/UINT64_C(1000000))); }
static double clock_time(void *context)
{ return (double)((frontend_remote_q3_runtime *)context)->frontend->wall_time_ns/1000000.0; }
static int32_t frame_number(void *context)
{ return word((uint32_t)((frontend_remote_q3_runtime *)context)->frontend->frame_number); }
static uint64_t audio_bus(void *context)
{ return ((frontend_remote_q3_runtime *)context)->services.resources.identity; }
static void print(void *context,const char *text)
{
    frontend_remote_q3_runtime *o=context;
    const qa_native_q3_remote_client_services *s=qa_native_q3_remote_client_services_read(o->services.client);
    if(s && text)frontend_console_print(o->frontend,&s->console_origin,text);
}
static bool actor(void *context,int32_t number,uint64_t *out,qa_error *e)
{
    frontend_remote_q3_runtime *o=context;
    const qa_native_q3_remote_client_services *s=qa_native_q3_remote_client_services_read(o->services.client);
    if(!out || !s || !o->entered || !current(o,o->entered,e))return false;
    if(number<0 || number==1022 || number==1023) { *out=QA_AUDIO_NO_ACTOR; return true; }
    qa_actor_id actual; bool present;
    if(!s->network.source_actor(s->network.context,(uint32_t)number,&actual,&present,e))return false;
    *out=present?frontend_audio_actor(o->frontend,actual,e):QA_AUDIO_NO_ACTOR;
    return (!present || *out!=QA_AUDIO_NO_ACTOR) && current(o,o->entered,e);
}
static bool listener(void *context,const qa_audio_listener *value,qa_error *e)
{
    frontend_remote_q3_runtime *o=context;
    if(!value || !o->entered || !current(o,o->entered,e))return false;
    o->listener=*value; o->listener.gain=1.0f/(float)o->frontend->options.seats; o->has_listener=true;
    return true;
}
static bool music_origin_current(void *context,const frontend_music_origin *origin)
{
    frontend_remote_q3_runtime *o=context;
    const frontend_remote_q3_resources *r=o?&o->services.resources:NULL;
    qa_audio_music *bus=o && o->frontend->audio?qa_audio_engine_bus_music(o->frontend->audio,r->identity):NULL;
    return attached(o) && frontend_remote_q3_frontend(o->parent)==o->frontend && r->owner==o->parent &&
        origin && origin->kind==FRONTEND_MUSIC_REMOTE && origin->context==o &&
        origin->music==o->music && o->music && (!bus || bus==o->music) &&
        origin->descriptor==r->descriptor && r->descriptor && r->descriptor->storage &&
        origin->catalog==qa_launch_instance_catalog(r->descriptor) && origin->product==r->descriptor->selection.product &&
        origin->files==r->mounts && origin->bus==r->identity && origin->physical_seat==r->physical_seat &&
        origin->receiver==r->domain.source.receiver.receiver;
}
static bool music_source_stop(void *context,qa_error *e)
{
    frontend_remote_q3_runtime *o=context;
    qa_audio_music *bus=qa_audio_engine_bus_music(o->frontend->audio,audio_bus(o));
    if(!attached(o) || !o->music || !qa_audio_music_idle(o->music) || (bus && bus!=o->music))
        return fail(e,QA_ERROR_ARGUMENT,"Remote music stop lost its actual retained player and bus");
    qa_audio_music_stop(o->music);
    if(bus)qa_audio_engine_remove_music(o->frontend->audio,audio_bus(o));
    if(qa_audio_engine_bus_music(o->frontend->audio,audio_bus(o)))
        return fail(e,QA_ERROR_ARGUMENT,"Remote music stop retains its actual engine attachment");
    o->music_attached=false;
    free(o->music_intro); free(o->music_loop); o->music_intro=o->music_loop=NULL; o->music_looping=false;
    return true;
}
static frontend_music_origin music_origin(frontend_remote_q3_runtime *o)
{
    const frontend_remote_q3_resources *r=&o->services.resources;
    return (frontend_music_origin){.kind=FRONTEND_MUSIC_REMOTE,.bus=r->identity,.physical_seat=r->physical_seat,
        .receiver=r->domain.source.receiver.receiver,.descriptor=r->descriptor,
        .catalog=qa_launch_instance_catalog(r->descriptor),.product=r->descriptor->selection.product,
        .files=r->mounts,.music=o->music,.context=o,.current=music_origin_current,.stop=music_source_stop};
}
static bool music_returned(frontend_remote_q3_runtime *o,const char *intro,const char *loop,bool looping,qa_error *e)
{
    frontend_music_origin origin=music_origin(o);
    return frontend_music_sources_explicit(o->frontend->music_sources,&origin,intro,loop,looping,e) &&
        current(o,o->entered,e);
}
static bool music_attach(frontend_remote_q3_runtime *o,qa_error *e)
{
    qa_audio_music *bus=qa_audio_engine_bus_music(o->frontend->audio,audio_bus(o));
    if(bus && bus!=o->music)return fail(e,QA_ERROR_ARGUMENT,"Remote soundtrack bus belongs to a different actual player");
    if(!bus) {
        if(!qa_audio_music_retain(o->music,e))return false;
        if(!qa_audio_engine_music(o->frontend->audio,audio_bus(o),o->services.resources.physical_seat,1,o->music,e)) {
            qa_audio_music_release(o->music); return false;
        }
    }
    o->music_attached=true; return true;
}
static bool music(void *context,const char *intro_name,const char *loop_name,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; qa_frontend *f=o->frontend;
    if(!o->entered || !current(o,o->entered,e) || !f->audio)
        return fail(e,QA_ERROR_UNSUPPORTED,"Remote music requires its entered CLIENT and audio owner");
    qa_audio_music *bus=qa_audio_engine_bus_music(f->audio,audio_bus(o));
    if(bus && bus!=o->music)return fail(e,QA_ERROR_ARGUMENT,"Remote soundtrack bus changed its actual source player");
    o->music_attached=bus!=NULL;
    if(o->music) {
        frontend_music_origin retained=music_origin(o);
        if(!frontend_music_sources_explicit_selected(f->music_sources,&retained)) {
            if(bus || !qa_audio_music_idle(o->music))
                return fail(e,QA_ERROR_ARGUMENT,"Selecting a different soundtrack retains the old actual player operation");
            qa_audio_music *fresh=NULL;
            qa_audio_music_controls *shared=frontend_music_sources_controls(f->music_sources);
            if(!shared || !qa_audio_music_create(qa_audio_engine_rate(f->audio),QA_AUDIO_Q3,true,&fresh,e))return false;
            if(!qa_audio_music_controls_bind(fresh,shared,e)) { qa_audio_music_destroy(fresh); return false; }
            qa_audio_music_release(o->music); o->music=fresh;
            free(o->music_intro); free(o->music_loop); o->music_intro=o->music_loop=NULL; o->music_looping=false;
        }
    }
    if(!o->music && !qa_audio_music_create(qa_audio_engine_rate(f->audio),QA_AUDIO_Q3,true,&o->music,e))return false;
    qa_audio_music_controls *controls=frontend_music_sources_controls(f->music_sources);
    if(!controls || (!qa_audio_music_controls_is(o->music,controls) && !qa_audio_music_controls_bind(o->music,controls,e)))return false;
    frontend_music_origin origin=music_origin(o);
    if(!frontend_music_sources_explicit_begin(f->music_sources,&origin,e) || !current(o,o->entered,e))return false;
    bool enabled;
    if(!qa_audio_music_controls_enabled(controls,&enabled))return fail(e,QA_ERROR_ARGUMENT,"Remote music lost its application CD controls");
    if(intro_name && *intro_name && !enabled)return music_returned(o,"","",false,e);
    const char *tail=loop_name?loop_name:"";
    if(intro_name && o->music_intro && o->music_loop && o->music_looping &&
       !strcmp(intro_name,o->music_intro) && !strcmp(tail,o->music_loop) && qa_audio_music_playing(o->music))
        return music_returned(o,o->music_intro,o->music_loop,o->music_looping,e);
    qa_audio_music_stop(o->music); free(o->music_intro); free(o->music_loop);
    o->music_intro=o->music_loop=NULL; o->music_looping=false;
    if(!intro_name || !*intro_name) {
        if(bus)qa_audio_engine_remove_music(f->audio,audio_bus(o));
        o->music_attached=qa_audio_engine_bus_music(f->audio,audio_bus(o))!=NULL;
        return !o->music_attached && music_returned(o,"","",false,e);
    }
    size_t a=strlen(intro_name),b=strlen(tail);
    char *intro=malloc(a+1),*loop=malloc(b+1);
    if(!intro || !loop) { free(intro); free(loop); return fail(e,QA_ERROR_MEMORY,"Retaining remote CGAME music names"); }
    memcpy(intro,intro_name,a+1); memcpy(loop,tail,b+1); o->music_intro=intro; o->music_loop=loop;
    if(!music_attach(o,e))return false;
    qa_audio_stream *first=NULL,*last=NULL;
    if(!qa_audio_bank_music_cue(o->services.resources.sounds,intro_name,QA_AUDIO_Q3,NULL,NULL,&first,e))return false;
    if(!first)return music_returned(o,intro_name,tail,false,e);
    last=first;
    if(*tail && strcmp(intro_name,tail) &&
       !qa_audio_bank_music_cue(o->services.resources.sounds,tail,QA_AUDIO_Q3,NULL,NULL,&last,e)) {
        qa_audio_stream_close(first); return false;
    }
    o->music_looping=last!=NULL; qa_audio_music_start(o->music,first,last);
    return music_returned(o,intro_name,tail,o->music_looping,e);
}
static bool prepare_view(void *context,const qa_q3_refdef *definition,qa_q3_scene_options *options,qa_error *e)
{
    frontend_remote_q3_runtime *o=context;
    if(!definition || !options || !o->entered || !current(o,o->entered,e))return false;
    options->world_family=QA_SCENE_Q3; options->split_screen=o->frontend->options.seats>1;
    return frontend_q3_scene_policy_read(o->frontend,options,e) &&
        frontend_q3_shadow_mode_read(o->entered->source.basis.client.cvars,&options->shadow_mode,e) &&
        current(o,o->entered,e);
}
static bool prepare_picture(void *context,qa_material_context *material,qa_error *e)
{
    frontend_remote_q3_runtime *o=context;
    return material && o->entered && current(o,o->entered,e) &&
        frontend_q3_material_diagnostics_read(o->frontend,&material->source_diagnostics,e) &&
        current(o,o->entered,e);
}
static bool remap(void *context,const char *from,const char *to,float offset,qa_error *e)
{
    frontend_remote_q3_runtime *o=context;
    return o->entered && current(o,o->entered,e) &&
        qa_material_remap(o->services.resources.materials,from,to,offset,e) && current(o,o->entered,e);
}
static int32_t memory_remaining(void *context)
{
    (void)context;
    return qa_memory_available();
}
static bool renderer_name(const char *text,const char *name)
{
    for(;*text;++text) { const char *a=text,*b=name;
        while(*a && *b) { unsigned char c=(unsigned char)*a++; if(c>='A' && c<='Z')c=(unsigned char)(c+32);
            if(c!=(unsigned char)*b)break;
            ++b; }
        if(!*b)return true;
    }
    return false;
}
static bool ragepro(const frontend_remote_q3_runtime *o)
{ const qa_gl_capabilities *caps=qa_gl_capabilities_get(o->frontend->gl);
    return caps && !renderer_name(caps->renderer,"banshee") && !renderer_name(caps->renderer,"voodoo_graphics") &&
        (renderer_name(caps->renderer,"rage pro") || renderer_name(caps->renderer,"ragepro")); }
static bool project(frontend_remote_q3_runtime *o,const q3n_remote_frame *r,bool loading,qa_error *e)
{
    return current(o,r,e) && application_native_q3_remote_client_frame_settings(o->services.client,
        r->source.dm_flags,ragepro(o),(size_t)memory_remaining(o),loading,r->source.publication.demo_playback,
        o->stereo,&o->settings,e) && current(o,r,e);
}
static bool frame_read(frontend_remote_q3_runtime *o,const q3n_remote_frame *r,q3n_frame *out,qa_error *e)
{
    if(!out || !current(o,r,e))return false;
    const qa_native_q3_remote_client_basis *b=&r->source.basis;
    *out=(q3n_frame){.application=o->frontend->application,.remote=r,
        .presentation=o->children.presentation,.assets=o->services.resources.assets,
        .clients=o->services.clients,.media=o->services.media,.events=o->children.events,.weapons=o->children.weapons,
        .particles=o->children.particles,.view=o->children.view,.player_state=o->children.player_state,
        .server_commands=o->children.commands,.entities=r->snapshots.entities,
        .seat=b->client.seat,.viewing_client=b->physical_client,.viewing_actor=r->source.publication.viewer,
        .physical_presentation_seat=o->services.resources.physical_seat,.time=r->snapshots.time,
        .frame_milliseconds=o->frame_milliseconds,.client_frame=o->client_frame,.refdef=o->refdef,
        .view_angles=o->view_angles,.weapon_settings=&o->settings.weapons,.event_settings=&o->settings.events};
    return qa_ui_preferences_read(qa_application_cvars(o->frontend->application),
        o->services.resources.physical_seat,&out->preferences,e) && q3n_frame_current(out);
}
bool frontend_remote_q3_runtime_command_frame(frontend_remote_q3_runtime *o,const q3n_remote_frame *r,q3n_frame *out,qa_error *e)
{
    return o && o->initialized && !o->busy && r && r->snapshots.stage==Q3N_REMOTE_CONSOLE &&
        project(o,r,false,e) && frame_read(o,r,out,e);
}
static bool begin(frontend_remote_q3_runtime *o,const q3n_remote_frame *r,q3n_frame *out,qa_error *e)
{
    if(!o || !o->complete || o->busy || !o->frames || !current(o,r,e))
        return fail(e,QA_ERROR_ARGUMENT,"Remote CGAME callback requires its actual bound, returned children");
    o->busy=true; o->entered=r; o->trace_count=0;
    if((r->snapshots.stage==Q3N_REMOTE_INITIALIZATION || project(o,r,false,e)) && frame_read(o,r,out,e))return true;
    o->entered=NULL; o->busy=false; return false;
}
static bool end(frontend_remote_q3_runtime *o,bool okay,qa_error *e)
{
    if(okay)okay=current(o,o->entered,e);
    o->entered=NULL; o->trace_count=0; o->busy=false;
    if(!okay)o->faulted=true;
    return okay;
}
static bool recipient_current(void *context,const q3n_frame *f,const qa_application_q3_client_context *recipient)
{
    frontend_remote_q3_runtime *o=context;
    const qa_application_q3_client_context *a=f&&f->remote?&f->remote->source.basis.client:NULL;
    return a && recipient && cut(o,f,NULL) && recipient->session==a->session &&
        recipient->receiver==a->receiver && recipient->service_owner==a->service_owner &&
        recipient->frontend_lifetime==a->frontend_lifetime && recipient->console==a->console &&
        recipient->cvars==a->cvars && recipient->source_cvars==a->source_cvars && recipient->seat==a->seat &&
        recipient->source_client==a->source_client && qa_actor_id_equal(recipient->source_actor,a->source_actor);
}
static bool receipt_current(void *context,const q3n_frame *f,const q3n_server_command_receipt *receipt)
{ frontend_remote_q3_runtime *o=context; return receipt && recipient_current(o,f,&receipt->recipient) &&
    q3n_remote_command_current(o->services.source,&receipt->remote); }
static bool center(void *context,const q3n_frame *f,const char *text,int32_t y,int32_t width,qa_error *e)
{ frontend_remote_q3_runtime *o=context; return cut(o,f,e) && q3n_hud_center_print(o->children.hud,f,text,y,width,e); }
static bool command_center(void *context,const q3n_frame *f,const qa_application_q3_client_context *recipient,
    const char *text,int32_t y,int32_t width,qa_error *e)
{ return recipient_current(context,f,recipient) && center(context,f,text,y,width,e); }
static bool hud_command(void *context,const q3n_frame *f,const char *text,qa_error *e)
{ frontend_remote_q3_runtime *o=context; return cut(o,f,e) && qa_native_q3_remote_client_reliable(o->services.client,text,e) && cut(o,f,e); }
static bool oldest_command(void *context,const q3n_frame *f,qa_q3_usercmd *out,qa_error *e)
{
    frontend_remote_q3_runtime *o=context;
    const qa_native_q3_remote_client_services *s=qa_native_q3_remote_client_services_read(o->services.client);
    if(!out || !s || !s->network.current_command || !s->network.user_command || !cut(o,f,e))
        return fail(e,QA_ERROR_ARGUMENT,"Remote disconnect requires its actual CLIENT user-command reader");
    int32_t sequence=subtract(s->network.current_command(s->network.context),63);
    bool present=false;
    if(!cut(o,f,e) || !s->network.user_command(s->network.context,sequence,out,&present,e) || !cut(o,f,e))return false;
    return present || fail(e,QA_ERROR_FORMAT,"CG_DrawDisconnect command fell outside CMD_BACKUP");
}
static bool voice(void *context,const q3n_frame *f,int32_t mode,bool only,int32_t client,int32_t color,const char *name,qa_error *e)
{ frontend_remote_q3_runtime *o=context; return cut(o,f,e) && q3n_server_commands_voice(o->children.commands,f,mode,only,client,color,name,e); }
static bool client_settings(void *context,const q3n_frame *f,bool loading,q3n_client_settings *out,qa_error *e)
{
    frontend_remote_q3_runtime *o=context;
    return out && cut(o,f,e) && application_native_q3_remote_client_info_settings(o->services.client,
        (size_t)memory_remaining(o),loading,out,e) && cut(o,f,e);
}
static bool load_deferred(void *context,const q3n_frame *f,qa_error *e)
{ frontend_remote_q3_runtime *o=context; q3n_client_settings settings;
    return client_settings(o,f,false,&settings,e) && q3n_clients_remote_load_deferred(o->services.clients,&f->remote->source,&settings,e); }
bool frontend_remote_q3_runtime_load_deferred(frontend_remote_q3_runtime *o,const q3n_frame *f,qa_error *e)
{ return load_deferred(o,f,e); }
frontend_remote_q3_commands *frontend_remote_q3_runtime_console(const frontend_remote_q3_runtime *o)
{ return o?o->console:NULL; }
bool frontend_remote_q3_runtime_command_call(frontend_remote_q3_runtime *o,const q3n_remote_frame *r,
    void *context,bool (*execute)(void *,const q3n_frame *,qa_error *),qa_error *e)
{
    q3n_frame f;
    if(!o || !o->initialized || o->prepared || !execute || !r || r->snapshots.stage!=Q3N_REMOTE_CONSOLE || !begin(o,r,&f,e))return false;
    bool okay=execute(context,&f,e);
    if(okay)okay=current(o,r,e);
    o->entered=NULL; o->trace_count=0; o->busy=false; return okay;
}
typedef struct remote_input_call {
    frontend_remote_q3_runtime *owner;
    int32_t kind, first, second;
    bool down;
} remote_input_call;
static bool input_call(void *context,const q3n_frame *f,qa_error *e)
{
    remote_input_call *call=context; frontend_remote_q3_runtime *o=call->owner;
    if(!cut(o,f,e))return false;
    if(q3n_frame_product(f)!=QA_Q3_TEAM_ARENA)return true;
    if(!o->children.mission)return fail(e,QA_ERROR_ARGUMENT,"Team input lost its actual authored-menu child");
    switch(call->kind) {
    case 0:return q3n_mission_hud_key(o->children.mission,f,call->first,call->down,e) && cut(o,f,e);
    case 1:return q3n_mission_hud_mouse(o->children.mission,f,call->first,call->second,e) && cut(o,f,e);
    case 2:return q3n_mission_hud_event(o->children.mission,f,call->first,e) && cut(o,f,e);
    default:return fail(e,QA_ERROR_ARGUMENT,"Unknown remote CGAME input operation");
    }
}
bool frontend_remote_q3_runtime_key_event(frontend_remote_q3_runtime *o,const q3n_remote_frame *r,
    int32_t key,bool down,qa_error *e)
{ remote_input_call call={.owner=o,.kind=0,.first=key,.down=down};
    return frontend_remote_q3_runtime_command_call(o,r,&call,input_call,e); }
bool frontend_remote_q3_runtime_mouse_event(frontend_remote_q3_runtime *o,const q3n_remote_frame *r,
    int32_t dx,int32_t dy,qa_error *e)
{ remote_input_call call={.owner=o,.kind=1,.first=dx,.second=dy};
    return frontend_remote_q3_runtime_command_call(o,r,&call,input_call,e); }
bool frontend_remote_q3_runtime_event_handling(frontend_remote_q3_runtime *o,const q3n_remote_frame *r,
    int32_t type,qa_error *e)
{ remote_input_call call={.owner=o,.kind=2,.first=type};
    return frontend_remote_q3_runtime_command_call(o,r,&call,input_call,e); }
bool frontend_remote_q3_runtime_teleport_take(frontend_remote_q3_runtime *o,bool *out,qa_error *e)
{ return o && out && !o->busy && o->initialized &&
    q3n_player_state_remote_teleport_take(o->children.player_state,out,e); }
bool frontend_remote_q3_runtime_listener(const frontend_remote_q3_runtime *o,qa_audio_listener *out,bool *present,qa_error *e)
{
    if(!out || !present || !attached(o) || o->busy || o->prepared || o->retiring || o->restoring || o->faulted ||
       !qa_native_q3_remote_client_current(o->services.client))
        return fail(e,QA_ERROR_ARGUMENT,"Remote listener requires its actual returned physical CLIENT");
    *present=o->has_listener; if(*present)*out=o->listener; return true;
}
static bool message(void *context,const q3n_command_message *value,qa_error *e)
{
    frontend_remote_q3_runtime *o=context;
    if(!value || !recipient_current(o,value->frame,value->recipient))return false;
    print(o,value->text); return cut(o,value->frame,e);
}
static bool update_loading(void *context,q3n_loading *loading,const q3n_frame *f,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; qa_frontend *frontend=o->frontend;
    if(loading!=o->loading || !cut(o,f,e) || frontend->capture || frontend->source_restoring)return false;
    qa_scene_frame_reset(&frontend->frame,frontend->frame_number);
    frontend->frame.source_backend=true;
    if(!qa_q3_presentation_frame(o->children.presentation,&frontend->frame,
        frontend_viewport(frontend,o->services.resources.physical_seat),e) ||
       !q3n_loading_draw_information(loading,f,e))return false;
    if(!frontend_render_controls_live(frontend,e) || !cut(o,f,e))return false;
    if(frontend->frame.source_skip_backend){
        bool retired=!frontend->frame.source_pending||
            qa_material_source_frame_end(frontend->frame.source_pending,&frontend->frame,false,e);
        return retired&&cut(o,f,e);
    }
    bool okay=frontend->cpu?(qa_cpu_execute(frontend->cpu,&frontend->frame,e) && qa_cpu_present_frame(frontend->cpu,e)):
        (frontend->gl && qa_gl_execute(frontend->gl,&frontend->frame,e) &&
         qa_gl_swap(frontend->gl,e));
    return okay && cut(o,f,e);
}
static bool loading(void *context,const q3n_frame *f,const char *text,int32_t item,qa_error *e)
{ frontend_remote_q3_runtime *o=context; return cut(o,f,e) &&
    (item>=0?q3n_loading_item(o->loading,f,(uint32_t)item,e):q3n_loading_string(o->loading,f,text,e)); }
static bool key_catcher(void *context,int32_t mask,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; qa_native_q3_remote_client_basis basis;
    return mask>=0 && qa_native_q3_remote_client_basis_read(o->services.client,&basis,e) &&
        qa_input_seat_set_catcher(o->services.resources.input,basis.client.service_owner,(uint32_t)mask,e);
}
static bool stage(void *context,const q3n_frame *f,q3n_command_init_stage which,
    const char *mapname,int32_t physical,uint32_t *inline_models,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; (void)mapname;
    if(!cut(o,f,e))return false;
    switch(which) {
    case Q3N_INIT_CONSOLE_COMMANDS:return frontend_remote_q3_commands_register(o->console,e);
    case Q3N_INIT_COLLISION_MAP: {
        qa_bsp_view bsp;
        if(!inline_models || !qa_bsp_open(qa_resource_bytes(o->services.resources.map),&bsp,e) ||
           !qa_q3_presentation_world(o->children.presentation,o->services.resources.world,o->services.resources.geometry,
               bsp.lumps[QA_BSP_ENTITIES].bytes,e))return false;
        size_t count=qa_scene_world_model_count(o->services.resources.world);
        if(!count || count>UINT32_MAX)return fail(e,QA_ERROR_FORMAT,"Remote collision has no actual inline model extent");
        *inline_models=(uint32_t)count; break;
    }
    case Q3N_INIT_PARTICLES:if(!q3n_particles_load_remote(o->children.particles,f,e))return false; break;
    case Q3N_INIT_CLIENT_LOADING:if(physical<0 || !q3n_loading_client(o->loading,f,(uint32_t)physical,e))return false; break;
    case Q3N_INIT_STRING_TABLE:case Q3N_INIT_MISSION_ASSETS:case Q3N_INIT_HUD_MENU:case Q3N_INIT_TEAM_CHAT:
        if(!o->children.mission || !q3n_mission_hud_initialize(o->children.mission,f,which,e))return false;
        break;
    default:return fail(e,QA_ERROR_ARGUMENT,"Unknown actual remote CGAME constructor stage");
    }
    return cut(o,f,e);
}
static bool clear_particles(void *context,const q3n_frame *f,qa_error *e)
{ frontend_remote_q3_runtime *o=context; return cut(o,f,e) && q3n_particles_load_remote(o->children.particles,f,e); }
static bool particle_explosion(void *context,const q3n_frame *f,const char *name,
    qa_vec3 origin,qa_vec3 velocity,int32_t duration,float first,float last,qa_error *e)
{ frontend_remote_q3_runtime *o=context; return cut(o,f,e) &&
    q3n_particles_weapon_explosion(o->children.particles,f,name,origin,velocity,duration,first,last,e) && cut(o,f,e); }

static bool mission_order(void *context,const q3n_frame *f,qa_error *e)
{ frontend_remote_q3_runtime *o=context; return cut(o,f,e) && q3n_mission_hud_check_order(o->children.mission,f,e); }
static bool mission_paint(void *context,const q3n_frame *f,bool scores,bool first,qa_error *e)
{ frontend_remote_q3_runtime *o=context; return cut(o,f,e) && q3n_mission_hud_paint(o->children.mission,f,scores,first,e); }
static bool mission_timed(void *context,const q3n_frame *f,qa_error *e)
{ frontend_remote_q3_runtime *o=context; return cut(o,f,e) && q3n_mission_hud_timed(o->children.mission,f,e); }
static bool mission_text(void *context,const q3n_frame *f,const char *text,float y,float scale,const float color[4],int32_t style,bool half,qa_error *e)
{ frontend_remote_q3_runtime *o=context; return cut(o,f,e) && q3n_mission_hud_text(o->children.mission,f,text,y,scale,color,style,half,e); }
static bool mission_center(void *context,const q3n_frame *f,const char *text,float y,const float color[4],float *height,qa_error *e)
{ frontend_remote_q3_runtime *o=context; return cut(o,f,e) && q3n_mission_hud_center_line(o->children.mission,f,text,y,color,height,e); }
static bool score_selection(void *context,const q3n_frame *f,const q3n_command_state *state,qa_error *e)
{ frontend_remote_q3_runtime *o=context; return cut(o,f,e) && q3n_mission_hud_score_selection(o->children.mission,f,state,e); }
static bool response_head(void *context,const q3n_frame *f,const q3n_command_state *state,qa_error *e)
{ frontend_remote_q3_runtime *o=context; return cut(o,f,e) && q3n_mission_hud_response(o->children.mission,f,state,e); }
static bool policy(frontend_remote_q3_runtime *o,const q3n_frame *f,qa_trace_policy *out,qa_error *e)
{
    if(!cut(o,f,e) || !out)return false;
    *out=qa_collision_default_policy(QA_COLLISION_Q3);
    const qa_cvars *cvars=f->remote->source.basis.client.source_cvars;
    const qa_cvar_view *curves=qa_cvars_find(cvars,"cm_noCurves"),*players=qa_cvars_find(cvars,"cm_playerCurveClip");
    if(!curves || !players)return fail(e,QA_ERROR_FORMAT,"Remote collision lost its actual source controls");
    out->curves=curves->integer==0; out->player_curve_clip=players->integer!=0; return true;
}
static bool skip_actor(frontend_remote_q3_runtime *o,int32_t skip,qa_actor_id *out,qa_error *e)
{
    *out=(qa_actor_id){0};
    if(skip<0 || skip>=1022)return true;
    const qa_native_q3_remote_client_services *s=qa_native_q3_remote_client_services_read(o->services.client);
    bool present;
    if(!s || !s->network.source_actor(s->network.context,(uint32_t)skip,out,&present,e))return false;
    if(!present)*out=(qa_actor_id){0};
    return true;
}
static bool retain_trace(frontend_remote_q3_runtime *o,const qa_trace_result *hit,int32_t number,qa_error *e)
{
    for(size_t i=0;i<o->trace_count;++i)if(o->traces[i].output==hit) {
        memcpy(&o->traces[i].value,hit,sizeof(*hit)); o->traces[i].number=number; return true;
    }
    if(o->trace_count==o->trace_capacity) {
        if(o->trace_capacity>SIZE_MAX/2/sizeof(*o->traces))return fail(e,QA_ERROR_MEMORY,"Remote trace receipt capacity overflow");
        size_t count=o->trace_capacity?o->trace_capacity*2:16;
        remote_trace_receipt *rows=realloc(o->traces,count*sizeof(*rows));
        if(!rows)return fail(e,QA_ERROR_MEMORY,"Retaining paired remote trace witnesses");
        o->traces=rows; o->trace_capacity=count;
    }
    remote_trace_receipt *row=o->traces+o->trace_count++;
    row->output=hit; memcpy(&row->value,hit,sizeof(*hit)); row->number=number; return true;
}
static bool trace(void *context,const q3n_frame *f,qa_vec3 start,qa_vec3 finish,qa_bounds bounds,
    int32_t skip,uint32_t mask,qa_trace_result *out,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; frontend_network_prediction_source source;
    qa_trace_query query={.start=start,.end=finish,.shape={QA_SHAPE_BOX,bounds}}; int32_t number;
    if(!out || o->entered!=f->remote || !policy(o,f,&query.policy,e) || !skip_actor(o,skip,&query.pass_actor,e) ||
       !frontend_remote_q3_frame_network_source(o->frames,f->remote,&source,e))return false;
    query.policy.contents_mask=mask;
    return frontend_network_prediction_trace_with_number(o->frontend,&source,&query,out,&number,e) &&
        cut(o,f,e) && retain_trace(o,out,number,e);
}
static bool trace_number(void *context,const q3n_remote_frame *r,const qa_trace_result *hit,int32_t *out,qa_error *e)
{
    frontend_remote_q3_runtime *o=context;
    if(!hit || !out || !o->busy || o->entered!=r || !current(o,r,e))return false;
    for(size_t i=o->trace_count;i>0;--i)if(o->traces[i-1].output==hit &&
        !memcmp(hit,&o->traces[i-1].value,sizeof(*hit))) { *out=o->traces[i-1].number; return true; }
    return fail(e,QA_ERROR_ARGUMENT,"Remote raw hit number requires its actual paired trace receipt");
}
static bool point_contents(void *context,const q3n_frame *f,qa_vec3 point,int32_t pass,uint32_t *out,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; frontend_network_prediction_source source;
    qa_point_query query={.point=point}; qa_point_contents result;
    if(!out || !policy(o,f,&query.policy,e) || !skip_actor(o,pass,&query.pass_actor,e) ||
       !frontend_remote_q3_frame_network_source(o->frames,f->remote,&source,e) ||
       !frontend_network_prediction_point_contents(o->frontend,&source,&query,&result,e) || !cut(o,f,e))return false;
    *out=(uint32_t)result.contents; return true;
}
static bool world_trace(void *context,const q3n_frame *f,qa_vec3 start,qa_vec3 finish,qa_bounds bounds,
    uint32_t mask,qa_trace_result *out,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; qa_trace_query query={.start=start,.end=finish,.shape={QA_SHAPE_BOX,bounds}};
    if(!policy(o,f,&query.policy,e))return false;
    query.policy.contents_mask=mask;
    return qa_collision_trace(o->services.resources.geometry,&query,out,e) && cut(o,f,e);
}
static bool world_contents(void *context,const q3n_frame *f,qa_vec3 point,uint32_t *out,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; qa_point_query query={.point=point}; qa_point_contents result;
    if(!out || !policy(o,f,&query.policy,e) || !qa_collision_point_contents(o->services.resources.geometry,&query,&result,e) ||
       !cut(o,f,e))return false;
    *out=(uint32_t)result.contents; return true;
}
static bool mark_fragments(void *context,const q3n_frame *f,const qa_vec3 *points,size_t count,qa_vec3 projection,
    qa_vec3 *output,size_t point_capacity,q3n_mark_fragment *fragments,size_t fragment_capacity,size_t *returned,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; qa_scene_mark_fragment native[128]; qa_scene_mark_result result;
    if(!returned || fragment_capacity>128 || !cut(o,f,e) ||
       !qa_scene_world_mark_fragments(o->services.resources.world,points,count,projection,output,point_capacity,
           native,fragment_capacity,&result,e) || !cut(o,f,e))return false;
    for(size_t i=0;i<result.fragment_count;++i) {
        if(native[i].first_point>UINT32_MAX || native[i].point_count>UINT32_MAX)return fail(e,QA_ERROR_FORMAT,"Remote mark fragment exceeds its source word");
        fragments[i]=(q3n_mark_fragment){(uint32_t)native[i].first_point,(uint32_t)native[i].point_count};
    }
    *returned=result.fragment_count; return true;
}
static bool reached(void *context,const q3n_remote_frame *r,const q3n_remote_command *command,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; q3n_frame f;
    if(!command || !begin(o,r,&f,e))return false;
    q3n_server_command_receipt receipt={.remote=*command,.recipient=r->source.basis.client,
        .publication_generation=r->source.basis.publication_generation,.map_revision=r->source.publication.restart_generation,
        .sequence=command->sequence,.present=command->present};
    bool okay=q3n_server_commands_remote_dispatch(o->children.commands,&f,&receipt,e);
    if(okay) {
        const q3n_command_state *state=q3n_server_commands_state(o->children.commands);
        if(!state)okay=false;
        else if(state->map_restart) {
            q3n_player_state_round(o->children.player_state);
            okay=q3n_server_commands_map_restart_taken(o->children.commands,&f,e);
        }
    }
    return end(o,okay,e);
}
static bool respawn(void *context,const q3n_remote_frame *r,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; q3n_frame f;
    if(!begin(o,r,&f,e))return false;
    return end(o,q3n_player_state_respawn_remote(o->children.player_state,&f,e),e);
}
static bool reset_player(void *context,const q3n_remote_frame *r,frontend_remote_centity *row,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; q3n_frame f; q3n_remote_entity actual;
    if(!row || !begin(o,r,&f,e))return false;
    bool okay=row->current.number>=0 && q3n_remote_frame_entity(r,(uint32_t)row->current.number,&actual,e) &&
        actual.current==&row->current && actual.presentation==row->presentation && q3n_remote_entity_current(&actual);
    if(okay)q3n_player_reset(&row->presentation->player,row->presentation->lerp_angles);
    return end(o,okay,e);
}
static bool event(void *context,const q3n_remote_frame *r,frontend_remote_centity *row,
    const qa_q3_entity *scratch,qa_vec3 position,int32_t time,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; q3n_frame f; q3n_remote_entity actual;
    if(!row || !scratch || time!=r->snapshots.time || !begin(o,r,&f,e))return false;
    bool okay=row->current.number>=0 && q3n_remote_frame_entity(r,(uint32_t)row->current.number,&actual,e) &&
        actual.current==&row->current && actual.presentation==row->presentation &&
        q3n_events_apply_remote(&f,&actual,scratch,position,e);
    return end(o,okay,e);
}
static bool transition_player(void *context,const q3n_remote_frame *r,const qa_q3_player *player,
    const qa_q3_player *previous,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; q3n_frame f;
    if(!begin(o,r,&f,e))return false;
    const q3n_command_state *state=q3n_server_commands_state(o->children.commands); qa_native_q3_client_cvar miss;
    bool okay=state && qa_native_q3_remote_client_cvar_read(o->services.client,"cg_showmiss",&miss,e);
    q3n_player_state_context settings={0};
    if(okay)settings=(q3n_player_state_context){.warmup=state->warmup,.timelimit=state->timelimit,.fraglimit=state->fraglimit,
        .scores1=state->scores1,.intermission_started=state->intermission_started,.show_miss=miss.integer!=0};
    return end(o,okay && q3n_player_state_transition_remote(o->children.player_state,&f,player,previous,&settings,e),e);
}
static bool prediction_completed(void *context,const q3n_remote_frame *r,frontend_remote_prediction_status status,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; q3n_frame f;
    if(!begin(o,r,&f,e))return false;
    qa_native_q3_client_cvar miss;
    bool okay=r->snapshots.stage==Q3N_REMOTE_PREDICTION_CALLBACK &&
        qa_native_q3_remote_client_cvar_read(o->services.client,"cg_showmiss",&miss,e);
    if(okay && status==FRONTEND_REMOTE_PREDICTION_PREDICTED)
        okay=q3n_player_state_prediction_finish(o->children.player_state,&f,miss.integer!=0,e);
    return end(o,okay,e);
}
static bool lagometer(void *context,const qa_q3_snapshot *snapshot,int32_t ping,qa_error *e)
{
    frontend_remote_q3_runtime *o=context;
    if(!attached(o) || o->busy || o->retiring || !qa_native_q3_remote_client_current(o->services.client))
        return fail(e,QA_ERROR_ARGUMENT,"Remote lagometer requires its returned physical CLIENT");
    q3n_hud_snapshot_sample(o->children.hud,snapshot==NULL,ping,snapshot?snapshot->flags:0); return true;
}
static bool warning(void *context,const char *text,qa_error *e)
{
    frontend_remote_q3_runtime *o=context;
    if(!text || !attached(o) || o->retiring || !qa_native_q3_remote_client_current(o->services.client))return false;
    print(o,text); return qa_native_q3_remote_client_current(o->services.client) || fail(e,QA_ERROR_ARGUMENT,"Remote warning expired its physical CLIENT");
}
static bool teleport_take(void *context,bool *out,qa_error *e)
{ return frontend_remote_q3_runtime_teleport_take(context,out,e); }
static bool set_view_size(void *context,int32_t value,qa_error *e)
{ frontend_remote_q3_runtime *o=context; return qa_native_q3_remote_client_set_view_size(o->services.client,value,e); }
static bool set_orbit_angle(void *context,float value,qa_error *e)
{ frontend_remote_q3_runtime *o=context; return application_native_q3_remote_client_set_orbit_angle(o->services.client,value,e); }

frontend_remote_q3 *frontend_remote_q3_runtime_parent(const frontend_remote_q3_runtime *o)
{ return o?o->parent:NULL; }
static bool children_returned(const frontend_remote_q3_runtime *o)
{
    const frontend_remote_q3_runtime_owners *c=&o->children;
    return (!o->console || frontend_remote_q3_commands_idle(o->console)) &&
        (!o->loading || q3n_loading_idle(o->loading)) && (!c->mission || q3n_mission_hud_idle(c->mission)) &&
        (!c->commands || q3n_server_commands_idle(c->commands)) && (!c->hud || q3n_hud_idle(c->hud)) &&
        (!c->player_state || q3n_player_state_idle(c->player_state)) && (!c->view || q3n_view_idle(c->view)) &&
        (!c->particles || q3n_particles_idle(c->particles)) && (!c->events || q3n_events_idle(c->events)) &&
        (!c->weapons || q3n_weapons_idle(c->weapons));
}
static bool children_idle(const frontend_remote_q3_runtime *o)
{ return children_returned(o) && (!o->children.presentation || qa_q3_presentation_idle(o->children.presentation)); }
bool frontend_remote_q3_runtime_idle(const frontend_remote_q3_runtime *o)
{ return !o || (!o->busy && !o->entered && !o->prepared && children_idle(o) && (!o->music || qa_audio_music_idle(o->music))); }
static bool initialized_source_current(const frontend_remote_q3_runtime *o,
    const q3n_remote_source_view *source,qa_error *e)
{
    frontend_remote_q3_services_view services;
    return attached(o) && o->complete && o->initialized && !o->faulted && !o->retiring && !o->restoring &&
        o->frames && frontend_remote_q3_frames_read(o->parent)==o->frames &&
        frontend_remote_q3_frame_parent(o->frames)==o->parent &&
        frontend_remote_q3_frame_callbacks_context(o->frames)==o && source &&
        source->owner==o->services.source && q3n_remote_source_client(source->owner)==o->services.client &&
        source->basis.application==o->frontend->application && source->basis.client.initialized &&
        frontend_remote_q3_services_read(o->parent,&services,e) &&
        services.client==o->services.client && services.source==o->services.source &&
        services.media==o->services.media && services.clients==o->services.clients &&
        services.resources.identity==o->services.resources.identity &&
        services.resources.physical_seat==o->services.resources.physical_seat &&
        services.resources.assets==o->services.resources.assets &&
        services.resources.world==o->services.resources.world &&
        services.resources.map==o->services.resources.map && services.resources.map==source->basis.map &&
        services.resources.geometry==o->services.resources.geometry && services.resources.geometry==source->basis.geometry &&
        q3n_remote_source_current(source);
}
bool frontend_remote_q3_runtime_initialized_current(const frontend_remote_q3_runtime *o)
{
    q3n_remote_source_view source;
    return o && frontend_remote_q3_runtime_idle(o) && o->frames &&
        frontend_remote_q3_frame_initialized_current(o->frames) &&
        q3n_remote_source_read(o->services.source,&source,NULL) && initialized_source_current(o,&source,NULL) &&
        q3n_media_assets(o->services.media)==o->services.resources.assets &&
        q3n_clients_assets(o->services.clients)==o->services.resources.assets &&
        q3n_media_remote_current(o->services.media,&source,NULL) &&
        q3n_clients_remote_current(o->services.clients,&source,NULL);
}
bool frontend_remote_q3_runtime_previous_time_read(const frontend_remote_q3_runtime *o,
    const q3n_remote_source_view *source,int32_t *out,qa_error *e)
{
    if(!out || !initialized_source_current(o,source,e) ||
       (!o->prepared && (!frontend_remote_q3_runtime_idle(o) || !frontend_remote_q3_frame_idle(o->frames))))
        return fail(e,QA_ERROR_ARGUMENT,"Previous presentation time requires its actual initialized CLIENT draw owner");
    *out=o->prepared?o->prepared_old_time:o->old_time; return true;
}
bool frontend_remote_q3_runtime_previous_time_import_read(const frontend_remote_q3_runtime *o,
    const frontend_remote_q3_frame_import_view *frame,int32_t *out,qa_error *e)
{
    frontend_remote_q3_services_view services; qa_q3_presentation_binding backend;
    const q3n_remote_source_view *source=frame?&frame->snapshots.source:NULL;
    if(!out || !attached(o) || !o->complete || !o->console || o->restoring || o->frames ||
       o->faulted || o->retiring || !o->frontend->source_restoring || o->frontend->capture ||
       o->frontend->resource_inventory || !frontend_remote_q3_runtime_idle(o) || !frame ||
       !frontend_remote_q3_frame_import_current(frame) ||
       frontend_remote_q3_frame_parent(frame->owner)!=o->parent ||
       frontend_remote_q3_frames_read(o->parent)!=frame->owner ||
       frontend_remote_q3_frame_callbacks_context(frame->owner)!=o ||
       frame->init_finished!=o->initialized || source->basis.client.initialized!=o->initialized ||
       source->owner!=o->services.source || q3n_remote_source_client(source->owner)!=o->services.client ||
       source->basis.application!=o->frontend->application ||
       !frontend_remote_q3_services_read(o->parent,&services,e))
        return fail(e,QA_ERROR_ARGUMENT,"Imported presentation time requires its actual decoded runtime and frame parents");
    const frontend_remote_q3_resources *actual=&services.resources,*retained=&o->services.resources;
    if(services.client!=o->services.client || services.source!=o->services.source ||
       services.media!=o->services.media || services.clients!=o->services.clients ||
       actual->owner!=o->parent || actual->identity!=retained->identity ||
       actual->physical_seat!=retained->physical_seat || actual->descriptor!=retained->descriptor ||
       actual->domain.content!=retained->domain.content || actual->mounts!=retained->mounts ||
       actual->images!=retained->images || actual->materials!=retained->materials || actual->fonts!=retained->fonts ||
       actual->sounds!=retained->sounds || actual->movies!=retained->movies || actual->assets!=retained->assets ||
       actual->world!=retained->world || actual->geometry!=retained->geometry || actual->map!=retained->map ||
       actual->registry!=retained->registry || actual->input!=retained->input ||
       actual->geometry!=source->basis.geometry || actual->map!=source->basis.map ||
       !frontend_remote_q3_resources_import_current(actual) || !q3n_remote_source_current(source) ||
       !q3n_clients_remote_current(services.clients,source,e) ||
       q3n_clients_assets(services.clients)!=actual->assets || q3n_media_assets(services.media)!=actual->assets ||
       !qa_q3_presentation_binding_read(o->children.presentation,&backend,e) ||
       backend.options.assets!=actual->assets || backend.options.owner!=actual->identity ||
       backend.options.seat!=actual->physical_seat || backend.world!=actual->world || backend.geometry!=actual->geometry)
        return fail(e,QA_ERROR_ARGUMENT,"Imported presentation time changed its actual CLIENT resources");
    q3n_loading_media media;
    if(!q3n_media_loading_read(services.media,&media,e) || media.remote_source!=source->owner ||
       media.product!=source->basis.product || media.assets!=actual->assets ||
       (o->initialized && !q3n_media_remote_current(services.media,source,e)) ||
       !frontend_remote_q3_frame_import_current(frame))return false;
    *out=o->old_time; return true;
}
bool frontend_remote_q3_runtime_retired(const frontend_remote_q3_runtime *o)
{
    return o && frontend_remote_q3_runtime_idle(o) && !o->frames && !o->console && !o->loading &&
        !o->children.mission && !o->children.commands && !o->children.hud && !o->children.player_state &&
        !o->children.view && !o->children.particles && !o->children.events && !o->children.weapons &&
        !o->children.presentation && !o->music;
}
frontend_remote_q3_frame *frontend_remote_q3_runtime_frames(const frontend_remote_q3_runtime *o)
{ return attached(o)?o->frames:NULL; }
bool frontend_remote_q3_runtime_bind_frames(frontend_remote_q3_runtime *o,frontend_remote_q3_frame *frames,qa_error *e)
{
    if(!attached(o) || !o->complete || o->restoring || o->frames || !frames || o->retiring || !frontend_remote_q3_runtime_idle(o) ||
       frontend_remote_q3_frame_parent(frames)!=o->parent || frontend_remote_q3_frames_read(o->parent)!=frames ||
       frontend_remote_q3_frame_callbacks_context(frames)!=o ||
       !frontend_remote_q3_frame_idle(frames))return fail(e,QA_ERROR_ARGUMENT,"Remote runtime requires its actual empty frame-child association");
    o->frames=frames; return true;
}
bool frontend_remote_q3_runtime_unbind_frames(frontend_remote_q3_runtime *o,frontend_remote_q3_frame *frames,qa_error *e)
{
    if(!o || !frames || !frontend_remote_q3_runtime_idle(o) || !frontend_remote_q3_frame_idle(frames) ||
       (o->frames && o->frames!=frames) || frontend_remote_q3_frame_parent(frames)!=o->parent)
        return fail(e,QA_ERROR_ARGUMENT,"Remote frame retirement requires its exact returned runtime association");
    o->frames=NULL; return true;
}
bool frontend_remote_q3_runtime_owners_read(const frontend_remote_q3_runtime *o,frontend_remote_q3_runtime_owners *out,qa_error *e)
{
    if(!out || !attached(o) || !o->complete || o->retiring || o->faulted ||
       !qa_native_q3_remote_client_current(o->services.client))return fail(e,QA_ERROR_ARGUMENT,"Remote child projection requires its retained actual CLIENT");
    *out=o->children; return true;
}
bool frontend_remote_q3_runtime_cinematics_restore(frontend_remote_q3_runtime *o,
    qa_q3_cinematic_source *source,qa_error *e)
{
    if (!attached(o) || !o->restoring || !o->frontend->source_restoring || o->frontend->capture ||
        o->frontend->resource_inventory || !o->complete || !frontend_remote_q3_runtime_idle(o))
        return fail(e,QA_ERROR_ARGUMENT,"Remote cinematic import lost its actual returned runtime and provider");
    return qa_q3_presentation_cinematics_bind(o->children.presentation,source,e);
}
static frontend_remote_q3_frame_callbacks frame_callbacks(frontend_remote_q3_runtime *o)
{
    return (frontend_remote_q3_frame_callbacks){.context=o,.reached=reached,.respawn=respawn,.reset_player=reset_player,
        .event=event,.transition_player=transition_player,.prediction_completed=prediction_completed,
        .lagometer=lagometer,.warning=warning,.trace_number=trace_number,.teleport_take=teleport_take};
}
bool frontend_remote_q3_runtime_callbacks_read(frontend_remote_q3_runtime *o,frontend_remote_q3_frame_callbacks *out,qa_error *e)
{
    if(!out || !attached(o) || !o->complete || !o->console || o->restoring || o->frames || !frontend_remote_q3_runtime_idle(o) || o->retiring)
        return fail(e,QA_ERROR_ARGUMENT,"Remote frame callbacks require their returned constructed runtime");
    *out=frame_callbacks(o); return true;
}
static bool create_runtime(frontend_remote_q3 *parent,bool restoring,frontend_remote_q3_runtime **out,qa_error *e)
{
    frontend_remote_q3_services_view services; qa_frontend *f=frontend_remote_q3_frontend(parent);
    if(!out || *out || !f || f->capture || restoring!=f->source_restoring || !frontend_remote_q3_services_read(parent,&services,e) ||
       !qa_native_q3_remote_client_idle(services.client) || !q3n_media_idle(services.media) || !q3n_clients_idle(services.clients) ||
       frontend_remote_q3_runtime_read(parent))return fail(e,QA_ERROR_ARGUMENT,"Remote runtime requires its true empty CLIENT resources and children");
    frontend_remote_q3_runtime *o=calloc(1,sizeof(*o));
    if(!o)return fail(e,QA_ERROR_MEMORY,"Retaining remote compiled CGAME runtime");
    o->parent=parent; o->frontend=f; o->services=services; o->restoring=restoring;
    o->children.media=services.media; o->children.clients=services.clients;
    if(!frontend_remote_q3_runtime_attach(parent,o,e)) { free(o); return false; }
    o->attached=true; *out=o;
    qa_native_q3_remote_client_basis basis;
    if(!qa_native_q3_remote_client_basis_read(services.client,&basis,e))return false;
    const frontend_remote_q3_resources *r=&services.resources;
    qa_q3_presentation_options backend={.assets=r->assets,.audio=f->audio,.clock={o,clock_time},
        .seat=r->physical_seat,.owner=r->identity,.viewport=frontend_viewport(f,r->physical_seat),
        .near_clip=4,.far_clip=16384,.identity_light=1,.lod_scale=5,.rail_core_width=6,.rail_ring_width=16,.rail_segment_length=32,
        .context=o,.audio_actor=actor,.listener=listener,.music=music,.frame_number=frame_number,.milliseconds=milliseconds,
        .audio_bus=audio_bus,.prepare_view=prepare_view,.prepare_picture=prepare_picture,.remap=remap,.print=print};
    frontend_material_movies *shader_movies=NULL;
    if (!restoring && (!frontend_material_movies_library_owner(r->materials,&shader_movies,e) ||
        !frontend_material_movies_cinematic_read(shader_movies,&backend.cinematics,e))) return false;
    q3n_weapon_options weapons={.assets=r->assets,.product=basis.product,.context=o,
        .particle_explosion=particle_explosion};
    q3n_event_options events={.assets=r->assets,.product=basis.product,.context=o,.print=print,.center_print=center,
        .trace=trace,.point_contents=point_contents,.mark_fragments=mark_fragments,.weapon_event=q3n_weapons_event};
    if(basis.product==QA_Q3_TEAM_ARENA)events.voice_chat=voice;
    q3n_view_options view={.application=f->application,.assets=r->assets,.remote_client=services.client,.seat=basis.client.seat,
        .context=o,.set_view_size=set_view_size,.set_third_person_angle_value=set_orbit_angle,.print=print};
    q3n_player_state_options ps={.application=f->application,.assets=r->assets,.remote_client=services.client,
        .seat=basis.client.seat,.context=o,.print=print};
    q3n_hud_options hud={.application=f->application,.assets=r->assets,.remote_client=services.client,.seat=basis.client.seat,
        .presentation_seat=r->physical_seat,.ui=f->seats[r->physical_seat].ui,.context=o,.milliseconds=milliseconds,
        .load_deferred=load_deferred,.client_command=hud_command,.oldest_command=oldest_command};
    if(basis.product==QA_Q3_TEAM_ARENA) {
        hud.mission_order=mission_order; hud.mission_paint=mission_paint; hud.mission_timed=mission_timed;
        hud.mission_text=mission_text; hud.mission_center_line=mission_center;
    }
    if(!qa_q3_presentation_create(&backend,&o->children.presentation,e) ||
       !q3n_weapons_create(&weapons,&o->children.weapons,e) || !q3n_events_create_remote(&events,&o->children.events,e) ||
       !q3n_particles_create_remote(r->assets,services.source,&o->children.particles,e) ||
       !q3n_view_create_remote(&view,&o->children.view,e) || !q3n_player_state_create_remote(&ps,&o->children.player_state,e) ||
       !q3n_hud_create_remote(&hud,&o->children.hud,e))return false;
    q3n_server_command_options commands={.application=f->application,.remote_client=services.client,.remote_source=services.source,
        .recipient=basis.client,.product=basis.product,.content=basis.content,.assets=r->assets,.presentation=o->children.presentation,
        .clients=services.clients,.media=services.media,.events=o->children.events,.context=o,.current=recipient_current,
        .receipt_current=receipt_current,.message=message,.center_print=command_center,.client_settings=client_settings,
        .loading=loading,.initialize_stage=stage,.clear_particles=clear_particles,.memory_remaining=memory_remaining};
    if(basis.product==QA_Q3_TEAM_ARENA) { commands.score_selection=score_selection; commands.response_head=response_head; }
    if(!q3n_server_commands_create_remote(&commands,&o->children.commands,e))return false;
    q3n_loading_options load={.application=f->application,.remote_client=services.client,.remote_source=services.source,
        .assets=r->assets,.presentation=o->children.presentation,.media=services.media,.ui=f->seats[r->physical_seat].ui,
        .seat=basis.client.seat,.presentation_seat=r->physical_seat,.context=o,.update_screen=update_loading};
    if(!q3n_loading_create_remote(&load,&o->loading,e))return false;
    if(basis.product==QA_Q3_TEAM_ARENA) {
        q3n_mission_hud_options mission={.application=f->application,.remote_client=services.client,.remote_source=services.source,
            .recipient=basis.client,.content=basis.content,.assets=r->assets,.presentation=o->children.presentation,
            .fonts=r->fonts,.seat=basis.client.seat,.context=o,.milliseconds=milliseconds,.print=print,.key_catcher=key_catcher};
        if(!q3n_mission_hud_create_remote(&mission,&o->children.mission,e) ||
           !q3n_mission_hud_bind(o->children.mission,o->children.hud,e))return false;
    }
    o->complete=true;
    q3n_remote_source_view final; q3n_loading_media media;
    if(!(restoring?frontend_remote_q3_commands_create_restored(o,&o->console,e):frontend_remote_q3_commands_create(o,&o->console,e)) ||
       !q3n_remote_source_read(services.source,&final,e) ||
       !q3n_media_loading_read(services.media,&media,e) || media.assets!=r->assets ||
       media.product!=final.basis.product || media.remote_source!=services.source ||
       !q3n_clients_remote_current(services.clients,&final,e)) {
        o->complete=false; return false;
    }
    return true;
}
bool frontend_remote_q3_runtime_create(frontend_remote_q3 *parent,frontend_remote_q3_runtime **out,qa_error *e)
{ return create_runtime(parent,false,out,e); }
bool frontend_remote_q3_runtime_create_restored(frontend_remote_q3 *parent,frontend_remote_q3_runtime **out,qa_error *e)
{ return create_runtime(parent,true,out,e); }
bool frontend_remote_q3_runtime_restore_candidate_ready(const frontend_remote_q3_runtime *o,qa_error *e)
{
    qa_q3_presentation_binding backend; q3n_remote_source_view source;
    qa_q3_presentation_assets *a=o?o->services.resources.assets:NULL;
    return attached(o) && o->restoring && o->frontend->source_restoring && !o->frontend->capture &&
        !o->initialized && !o->frames && !o->busy && !o->prepared && !o->faulted && !o->retiring &&
        a && qa_q3_assets_idle(a) && !a->name_count && !a->model_count && !a->skin_count && !a->shader_count && !a->sound_count &&
        q3n_remote_source_read(o->services.source,&source,e) &&
        qa_q3_presentation_binding_read(o->children.presentation,&backend,e) &&
        backend.options.assets==a && backend.options.owner==o->services.resources.identity &&
        backend.options.seat==o->services.resources.physical_seat &&
        (!backend.world || (backend.world==o->services.resources.world && backend.geometry==o->services.resources.geometry)) ? true :
        fail(e,QA_ERROR_ARGUMENT,"Remote import constructor requires its genuine empty retained physical candidate");
}
static bool import_services_read(const frontend_remote_q3_runtime *o,frontend_remote_q3_services_view *out,qa_error *e)
{
    frontend_remote_q3_services_view services; q3n_remote_source_view source;
    if(!out || !attached(o) || !o->restoring || !o->frontend->source_restoring || o->frontend->capture ||
       o->frontend->resource_inventory || o->faulted || o->retiring || !frontend_remote_q3_runtime_idle(o) ||
       !frontend_remote_q3_services_read(o->parent,&services,e))
        return fail(e,QA_ERROR_ARGUMENT,"Runtime import association requires its returned staged CLIENT parent");
    const frontend_remote_q3_resources *before=&o->services.resources,*actual=&services.resources;
    if(services.client!=o->services.client || services.source!=o->services.source ||
       services.media!=o->services.media || services.clients!=o->services.clients ||
       actual->owner!=o->parent || actual->owner!=before->owner || actual->identity!=before->identity ||
       actual->physical_seat!=before->physical_seat || actual->descriptor!=before->descriptor ||
       actual->domain.content!=before->domain.content || actual->mounts!=before->mounts ||
       actual->images!=before->images || actual->materials!=before->materials || actual->fonts!=before->fonts ||
       actual->sounds!=before->sounds || actual->movies!=before->movies || actual->assets!=before->assets ||
       actual->geometry!=before->geometry || actual->map!=before->map || actual->registry!=before->registry ||
       actual->input!=before->input || (before->world && actual->world!=before->world) ||
       !frontend_remote_q3_resources_import_current(actual) ||
       !qa_native_q3_remote_client_idle(services.client) || !q3n_remote_source_idle(services.source) ||
       !q3n_media_idle(services.media) || !q3n_clients_idle(services.clients) ||
       q3n_media_assets(services.media)!=actual->assets || q3n_clients_assets(services.clients)!=actual->assets ||
       !q3n_remote_source_read(services.source,&source,e) ||
       q3n_remote_source_client(services.source)!=services.client ||
       source.basis.application!=o->frontend->application || source.basis.content!=actual->domain.content ||
       source.basis.map!=actual->map || source.basis.geometry!=actual->geometry || !q3n_remote_source_current(&source))
        return fail(e,QA_ERROR_ARGUMENT,"Runtime import changed its retained physical CLIENT or resource heaps");
    *out=services; return true;
}
bool frontend_remote_q3_runtime_callbacks_read_restored(frontend_remote_q3_runtime *o,
    frontend_remote_q3_frame_callbacks *out,qa_error *e)
{
    frontend_remote_q3_services_view services;
    if(!out || !o || !o->complete || !o->console ||
       !frontend_remote_q3_runtime_restore_candidate_ready(o,e) || !import_services_read(o,&services,e))
        return fail(e,QA_ERROR_ARGUMENT,"Restored frame callbacks require their actual empty runtime before asset capture");
    *out=frame_callbacks(o); return true;
}
bool frontend_remote_q3_runtime_prepare_import(frontend_remote_q3_runtime *o,qa_error *e)
{
    qa_bsp_view bsp; frontend_remote_q3_services_view services;
    if(!frontend_remote_q3_runtime_restore_candidate_ready(o,e) || !import_services_read(o,&services,e))return false;
    if(!services.resources.world)return fail(e,QA_ERROR_ARGUMENT,"Runtime import requires its actual adopted world before backend binding");
    if(!qa_bsp_open(qa_resource_bytes(services.resources.map),&bsp,e))return false;
    if(bsp.family!=QA_BSP_Q3)return fail(e,QA_ERROR_FORMAT,"Runtime import requires its retained Q3 map");
    if(!qa_q3_presentation_prepare_restored(o->children.presentation,&o->frontend->frame,
            services.resources.world,services.resources.geometry,bsp.lumps[QA_BSP_ENTITIES].bytes,e))return false;
    o->services.resources=services.resources; return true;
}
bool frontend_remote_q3_runtime_destroy(frontend_remote_q3_runtime **owner,qa_error *e)
{
    if(!owner)return false;
    frontend_remote_q3_runtime *o=*owner;
    if(!o)return true;
    if(!frontend_remote_q3_runtime_idle(o) || o->frames || o->frontend->capture)
        return fail(e,QA_ERROR_ARGUMENT,"Remote runtime retirement still retains actual frames or active children");
    o->retiring=true;
    if(o->music && !frontend_music_sources_explicit_retire(o->frontend->music_sources,o,e))return false;
    if(o->music && (o->music_intro || o->music_loop || qa_audio_music_playing(o->music) ||
       qa_audio_engine_bus_music(o->frontend->audio,audio_bus(o))) && !music_source_stop(o,e))return false;
    if(!frontend_remote_q3_commands_destroy(&o->console,e))return false;
    q3n_loading_destroy(o->loading); o->loading=NULL;
    q3n_mission_hud_destroy(o->children.mission); o->children.mission=NULL;
    q3n_server_commands_destroy(o->children.commands); o->children.commands=NULL;
    q3n_hud_destroy(o->children.hud); o->children.hud=NULL;
    q3n_player_state_destroy(o->children.player_state); o->children.player_state=NULL;
    q3n_view_destroy(o->children.view); o->children.view=NULL;
    q3n_particles_destroy(o->children.particles); o->children.particles=NULL;
    q3n_events_destroy(o->children.events); o->children.events=NULL;
    q3n_weapons_destroy(o->children.weapons); o->children.weapons=NULL;
    if(o->children.presentation) {
        if(!qa_q3_presentation_destroy(o->children.presentation,e))return false;
        o->children.presentation=NULL;
    }
    qa_audio_music_release(o->music); o->music=NULL; o->music_attached=false;
    free(o->music_intro); free(o->music_loop); o->music_intro=o->music_loop=NULL;
    if(o->attached && !frontend_remote_q3_runtime_detach(o->parent,o,e))return false;
    free(o->traces); free(o); *owner=NULL; return true;
}
bool frontend_remote_q3_runtime_initialize(void *context,const q3n_remote_frame *r,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; q3n_frame f;
    if(!o || o->initialized || !r || r->snapshots.stage!=Q3N_REMOTE_INITIALIZATION ||
       !qa_native_q3_remote_client_prepare(o->services.client,e) || !begin(o,r,&f,e))return false;
    int32_t baseline=o->parent->compiled_video?r->source.reached_command:r->source.publication.initial_command;
    bool okay=(o->parent->compiled_video?
        q3n_server_commands_initialize_video(o->children.commands,&f,baseline,e):
        q3n_server_commands_initialize(o->children.commands,&f,baseline,e)) &&
        qa_native_q3_remote_client_initialized(o->services.client,e);
    if(okay)o->initialized=true;
    return end(o,okay,e);
}
bool frontend_remote_q3_runtime_prepare(frontend_remote_q3_runtime *o,uint32_t stereo,qa_error *e)
{
    q3n_remote_source_view source;
    if(!o || !o->initialized || !o->frames || stereo>2 || !frontend_remote_q3_runtime_idle(o) || o->faulted || o->retiring ||
       !q3n_remote_source_read(o->services.source,&source,e))return fail(e,QA_ERROR_ARGUMENT,"Remote draw requires its actual initialized returned owner");
    o->prepared_old_time=o->old_time; o->prepared=true;
    o->prediction_prepared=o->information_prepared=false; o->stereo=stereo; o->rendered=false; o->has_listener=false;
    bool okay=qa_native_q3_remote_client_update(o->services.client,e) &&
        qa_q3_presentation_frame(o->children.presentation,&o->frontend->frame,
            frontend_viewport(o->frontend,o->services.resources.physical_seat),e);
    const char *text=q3n_loading_text(o->loading);
    if(okay && !text)okay=fail(e,QA_ERROR_ARGUMENT,"Remote frame lost its actual loading-information owner");
    if(okay) {
        o->information_prepared=*text!=0;
        if(!o->information_prepared)okay=qa_q3_presentation_clear_loops(o->children.presentation,false,e) &&
            qa_q3_presentation_clear(o->children.presentation,e);
    }
    if(!okay)o->faulted=true;
    return okay;
}
bool frontend_remote_q3_runtime_information_read(const frontend_remote_q3_runtime *o,
    const q3n_remote_source_view *source,const char **text,qa_error *e)
{
    if(!text || !attached(o) || !o->complete || !o->initialized || !o->prepared || o->busy ||
       o->prediction_prepared || o->faulted || o->retiring || !source || source->owner!=o->services.source ||
       source->basis.application!=o->frontend->application || !q3n_remote_source_current(source) || !children_idle(o))
        return fail(e,QA_ERROR_ARGUMENT,"Loading reason requires its actual prepared returned CLIENT runtime");
    const char *value=q3n_loading_text(o->loading);
    if(!value || (*value!=0)!=o->information_prepared)
        return fail(e,QA_ERROR_ARGUMENT,"Loading text changed after the actual frame preparation");
    *text=value; return true;
}
bool frontend_remote_q3_runtime_information_current(const frontend_remote_q3_runtime *o,
    const q3n_remote_source_view *source,const char *text)
{
    return attached(o) && o->complete && o->initialized && o->prepared && o->information_prepared &&
        !o->retiring && !o->faulted && !o->restoring && text &&
        text==q3n_loading_text(o->loading) && *text && source && source->owner==o->services.source &&
        source->basis.application==o->frontend->application && q3n_remote_source_current(source);
}
bool frontend_remote_q3_runtime_before_prediction(frontend_remote_q3_runtime *o,bool *active,qa_error *e)
{
    frontend_remote_snapshots *snapshots=o?frontend_remote_q3_frame_snapshots(o->frames):NULL;
    frontend_remote_snapshots_view receipt;
    if(!active || !o || !o->prepared || o->prediction_prepared || o->information_prepared || o->busy || o->faulted || o->retiring ||
       !frontend_remote_snapshots_read(snapshots,&receipt) || !frontend_remote_snapshots_current(snapshots,&receipt))
        return fail(e,QA_ERROR_ARGUMENT,"Remote prediction preparation requires its actual returned retail snapshot");
    *active=receipt.snapshot && !(receipt.snapshot->flags&2);
    if(!*active)return true;
    const q3n_weapon_selection *weapon=q3n_weapons_selection(o->children.weapons);
    const q3n_view_state *camera=q3n_view_read(o->children.view);
    if(!weapon || !camera || !qa_native_q3_remote_client_command_values(o->services.client,
        weapon->weapon,camera->zoom_sensitivity,e) || !frontend_remote_snapshots_current(snapshots,&receipt))return false;
    o->client_frame=word((uint32_t)o->client_frame+1u); o->prediction_prepared=true; return true;
}
bool frontend_remote_q3_runtime_frame_end(frontend_remote_q3_runtime *o,bool completed,qa_error *e)
{
    if(!o || !o->prepared || o->busy || o->entered || !children_idle(o))
        return fail(e,QA_ERROR_ARGUMENT,"Remote draw end requires all actual child callbacks to return");
    o->prepared=false; o->prepared_old_time=0; o->prediction_prepared=o->information_prepared=false;
    if(!completed)o->faulted=true;
    return true;
}

static int32_t packet_rand(void *context)
{ return q3n_events_rand(((frontend_remote_q3_runtime *)context)->children.events); }
static bool packet_body(void *context,const q3n_frame *f,const q3n_remote_entity *row,q3n_entity *cent,
    const qa_q3_ref_entity *ref,qa_error *e)
{
    frontend_remote_q3_runtime *o=context;
    return ref && row && cent==row->presentation && cut(o,f,e) && q3n_remote_entity_current(row) &&
        qa_q3_presentation_entity(f->presentation,ref,e) && q3n_remote_entity_current(row) && cut(o,f,e);
}
static bool body_hidden(void *context,const q3n_frame *f,const q3n_remote_entity *row,bool *out,qa_error *e)
{
    frontend_remote_q3_runtime *o=context;
    if(!out || !cut(o,f,e) || !q3n_remote_entity_current(row))return false;
    /* This owner has the primary received-source body. A selected remote body
     * requires its own admitted producer before it can suppress that body. */
    *out=false; return true;
}
static bool body_submit(void *context,const q3n_frame *f,const q3n_remote_entity *row,uint32_t part,
    const qa_q3_ref_entity *ref,bool base,bool *consumed,qa_error *e)
{
    (void)base;
    if(!consumed || part>2)return false;
    *consumed=false;
    if(!packet_body(context,f,row,row->presentation,ref,e))return false;
    *consumed=true; return true;
}
static bool player_weapon(void *context,const q3n_frame *f,const q3n_remote_entity *row,
    const qa_q3_ref_entity *torso,int32_t team,qa_error *e)
{ frontend_remote_q3_runtime *o=context; (void)team;
    return cut(o,f,e) && q3n_weapons_player_remote(f,torso,row,e) && cut(o,f,e); }
static bool packet_player(void *context,const q3n_frame *f,const q3n_remote_entity *row,q3n_entity *cent,qa_error *e)
{
    frontend_remote_q3_runtime *o=context;
    if(!cut(o,f,e) || !row || row->presentation!=cent || !q3n_remote_entity_current(row))return false;
    int32_t client=row->current->clientNum;
    if(client<0 || client>=64)return fail(e,QA_ERROR_FORMAT,"Remote player has an invalid physical client-info index");
    const q3n_client_info *ci=q3n_clients_get(f->clients,(uint32_t)client);
    if(!ci || !ci->info_valid)return true;
    if(cent->client_media_revision!=ci->media_revision) {
        q3n_player_reset(&cent->player,qa_v3(row->current->angles[0],row->current->angles[1],row->current->angles[2]));
        cent->client_media_revision=ci->media_revision;
    }
    const qa_q3_player *ps=q3n_frame_predicted_player(f);
    q3n_body_options options={.time=f->time,.frame_milliseconds=f->frame_milliseconds,
        .local_view_client=ps->clientNum,.shadow_mode=o->settings.player_fx.shadow_mode,.swing_speed=o->settings.swing_speed,
        .no_player_animations=o->settings.no_player_animations,.animations_disabled=o->settings.animations_disabled,
        .third_person=f->third_person,.camera_mode=o->settings.view.camera_mode};
    q3n_player_body body;
    q3n_player_fx_remote_backend backend={.context=o,.world_trace=world_trace,.world_point_contents=world_contents,
        .body_hidden=body_hidden,.body_submit=body_submit,.player_weapon=player_weapon};
    return q3n_player_body_build_remote(f,row,ci,&options,&body,e) &&
        q3n_player_fx_submit_remote(f,row,ci,&body,&o->settings.player_fx,&backend,e) && cut(o,f,e);
}
static bool packet_trail(void *context,const q3n_frame *f,const q3n_remote_entity *row,q3n_entity *cent,
    const q3n_weapon_media *media,bool grapple,qa_error *e)
{ frontend_remote_q3_runtime *o=context; (void)media; (void)grapple;
    return cent==row->presentation && cut(o,f,e) && q3n_weapons_trail_remote(f,row,e); }
static bool packet_powerups(void *context,const q3n_frame *f,const q3n_remote_entity *row,q3n_entity *cent,
    const qa_q3_ref_entity *base,int32_t team,qa_error *e)
{
    const q3n_media_view *media=q3n_media_read(f->media); qa_q3_ref_entity ref=*base;
    uint32_t powers=(uint32_t)row->current->powerups;
    if(powers&(1u<<4)) { ref.custom_shader=media->graphics[Q3N_G_INVIS]; return packet_body(context,f,row,cent,&ref,e); }
    if(!packet_body(context,f,row,cent,&ref,e))return false;
    if(powers&(1u<<1)) { ref.custom_shader=media->graphics[team==1?Q3N_G_RED_QUAD:Q3N_G_QUAD];
        if(!packet_body(context,f,row,cent,&ref,e))return false; }
    if((powers&(1u<<5)) && (f->time/100)%10==1) { ref.custom_shader=media->graphics[Q3N_G_REGEN];
        if(!packet_body(context,f,row,cent,&ref,e))return false; }
    if(powers&(1u<<2)) { ref.custom_shader=media->graphics[Q3N_G_BATTLE_SUIT];
        if(!packet_body(context,f,row,cent,&ref,e))return false; }
    return true;
}
static bool required_weapon(frontend_remote_q3_runtime *o,const q3n_frame *f,int32_t number,bool registered[16],qa_error *e)
{
    if(number<0 || number>=16)return fail(e,QA_ERROR_FORMAT,"Remote frame weapon is outside the authored registry");
    if(registered[number])return true;
    if(!q3n_media_register_weapon(o->services.media,(uint32_t)number,e) || !cut(o,f,e))return false;
    registered[number]=true; return true;
}
static bool required_media(frontend_remote_q3_runtime *o,const q3n_frame *f,qa_error *e)
{
    bool registered[16]={0}; int32_t extent=q3n_frame_product(f)==QA_Q3_TEAM_ARENA?14:11;
    const qa_q3_player *predicted=q3n_frame_predicted_player(f),*snapshot=q3n_frame_snapshot_player(f);
    if(!predicted || !snapshot || !required_weapon(o,f,predicted->weapon,registered,e))return false;
    unsigned stat=q3n_frame_product(f)==QA_Q3_TEAM_ARENA?3u:2u;
    uint32_t owned=(uint32_t)snapshot->stats[stat];
    for(int32_t i=1;i<extent;++i)if((owned&(1u<<(unsigned)i)) && !required_weapon(o,f,i,registered,e))return false;
    const qa_q3_snapshot *snap=f->remote->snapshots.snapshot;
    for(size_t i=0;i<snap->entity_count;++i) {
        int32_t number=snap->entities[i].number; q3n_remote_entity row;
        if(number<0 || !q3n_remote_frame_entity(f->remote,(uint32_t)number,&row,e))return false;
        int32_t type=row.current->eType,weapon=row.current->weapon;
        if(type==1 || type==3 || type==11) {
            if(type!=1 && weapon>extent)weapon=0;
            if(!required_weapon(o,f,weapon,registered,e))return false;
        }
    }
    return true;
}
static bool packet(frontend_remote_q3_runtime *o,q3n_frame *f,qa_error *e)
{
    int32_t before=f->remote->predicted_player->entityEventSequence;
    if(!q3n_packet_remote_predict(f,e) || !frontend_remote_q3_frame_event_publish(o->frames,f->remote,before,e))return false;
    q3n_packet_remote_imports imports={.context=o,.rand=packet_rand,.body=packet_body,.player=packet_player,
        .trail=packet_trail,.powerups=packet_powerups};
    q3n_remote_entity predicted,followed;
    const qa_q3_player *ps=q3n_frame_predicted_player(f);
    if(!q3n_remote_frame_predicted(f->remote,&predicted,e) ||
       !q3n_packet_remote_entity(f,&predicted,&o->settings.packet,&imports,e) || ps->clientNum<0 ||
       !q3n_remote_frame_entity(f->remote,(uint32_t)ps->clientNum,&followed,e) ||
       !q3n_packet_remote_lerp(f,&followed,&o->settings.packet,e) ||
       !q3n_packet_remote_sound_position(f,&followed,e))return false;
    const qa_q3_snapshot *snapshot=f->remote->snapshots.snapshot;
    for(size_t i=0;i<snapshot->entity_count;++i) {
        int32_t number=snapshot->entities[i].number; q3n_remote_entity row;
        if(number<0 || !q3n_remote_frame_entity(f->remote,(uint32_t)number,&row,e))return false;
        if(row.current_valid && !q3n_packet_remote_entity(f,&row,&o->settings.packet,&imports,e))return false;
    }
    return cut(o,f,e);
}
static bool powerup_audio(frontend_remote_q3_runtime *o,const q3n_frame *f,qa_error *e)
{
    const qa_q3_player *snapshot=q3n_frame_snapshot_player(f);
    int32_t sound=q3n_media_read(o->services.media)->sounds[Q3N_S_WEAR_OFF];
    for(unsigned i=0;i<16;++i) {
        int32_t expiry=snapshot->powerups[i];
        if(expiry<=f->time)continue;
        int32_t remaining=subtract(expiry,f->time),previous=subtract(expiry,o->old_time);
        if(remaining<5000 && remaining/1000!=previous/1000 &&
           (!qa_q3_presentation_sound(f->presentation,sound,NULL,snapshot->clientNum,4,false,e) || !cut(o,f,e)))return false;
    }
    return true;
}
static bool timescale(frontend_remote_q3_runtime *o,qa_error *e)
{
    qa_native_q3_client_cvar finish,speed,value;
    qa_native_q3_remote_client_service *client=o->services.client;
    if(!qa_native_q3_remote_client_cvar_read(client,"cg_timescaleFadeEnd",&finish,e) ||
       !qa_native_q3_remote_client_cvar_read(client,"cg_timescaleFadeSpeed",&speed,e) ||
       !qa_native_q3_remote_client_cvar_read(client,"cg_timescale",&value,e))return false;
    if(value.number==finish.number)return true;
    float delta=product(speed.number,(float)o->frame_milliseconds)/1000;
    float next=value.number<finish.number?fminf(finish.number,sum(value.number,delta)):fmaxf(finish.number,sum(value.number,-delta));
    return qa_native_q3_remote_client_cvar_number(client,"cg_timescale",next,e) &&
        (speed.number==0 || qa_native_q3_remote_client_set_timescale(client,next,e));
}
bool frontend_remote_q3_runtime_draw(void *context,const q3n_remote_frame *r,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; q3n_frame f;
    if(!o || !o->initialized || !o->prepared || !o->prediction_prepared || !r ||
       r->snapshots.stage!=Q3N_REMOTE_COMPLETED_FRAME || !begin(o,r,&f,e))return false;
    bool in_water=false; o->rendered=false;
    qa_scene_rect viewport=frontend_viewport(o->frontend,o->services.resources.physical_seat);
    bool okay=q3n_view_frame(o->children.view,&f,&o->settings.view,o->children.player_state,viewport,&in_water,e) &&
        required_media(o,&f,e);
    if(okay)memcpy(f.refdef.area_mask,r->snapshots.snapshot->area_mask,sizeof(f.refdef.area_mask));
    if(okay && !f.third_person)okay=q3n_view_damage_blob(o->children.view,&f,&o->settings.view,o->children.player_state,e);
    if(okay && !r->prediction.hyperspace)okay=packet(o,&f,e) && q3n_marks_submit(&f,e) &&
        q3n_particles_add(&f,e) && q3n_local_submit(&f,e);
    const q3n_view_state *camera=q3n_view_read(o->children.view);
    const q3n_event_state *events=q3n_events_state(o->children.events);
    q3n_weapon_view weapon={.predicted_entity=r->predicted_entity,.predicted_state=r->predicted_state,
        .bob_cycle=camera->bob_cycle,.xy_speed=camera->xy_speed,.bob_fraction_sin=camera->bob_fraction_sin,
        .land_time=events->land_time,.land_change=events->land_change,.test_gun=camera->test_gun};
    const qa_q3_player *snapshot=q3n_frame_snapshot_player(&f);
    if(okay)okay=q3n_weapons_view(&f,&weapon,e) && q3n_events_finish(&f,e) &&
        q3n_server_commands_finish(o->children.commands,&f,e) && q3n_view_test_submit(o->children.view,&f,&o->settings.view,e) &&
        powerup_audio(o,&f,e) && qa_q3_presentation_listener(f.presentation,snapshot->clientNum,f.refdef.origin,f.refdef.axis,e);
    if(okay && o->stereo!=2) {
        int32_t elapsed=subtract(f.time,o->old_time); o->frame_milliseconds=elapsed<0?0:elapsed; o->old_time=f.time;
        q3n_hud_frame_sample(o->children.hud,subtract(f.time,r->source.publication.latest_time));
    }
    if(okay)okay=timescale(o,e);
    bool tournament=snapshot->persistant[3]==3 && (snapshot->pmFlags&8192);
    if(okay) { o->refdef=f.refdef; o->view_angles=f.view_angles; }
    if(okay && !tournament) {
        okay=q3n_hud_tile_clear(o->children.hud,&f,viewport,e);
        qa_q3_refdef render=f.refdef;
        float separation=o->stereo==0?0:product(o->settings.stereo_separation,o->stereo==1?-0.5f:0.5f);
        render.origin.x=sum(render.origin.x,product(render.axis[1].x,-separation));
        render.origin.y=sum(render.origin.y,product(render.axis[1].y,-separation));
        render.origin.z=sum(render.origin.z,product(render.axis[1].z,-separation));
        if(okay)okay=qa_q3_presentation_render(f.presentation,&render,e);
    }
    if(okay)okay=q3n_hud_frame(o->children.hud,&f,&o->settings.hud,o->children.commands,o->children.player_state,viewport,e);
    if(okay && !tournament) {
        qa_native_q3_client_cvar stats;
        okay=qa_native_q3_remote_client_cvar_read(o->services.client,"cg_stats",&stats,e);
        if(okay && stats.integer) { char text[64]; snprintf(text,sizeof(text),"cg.clientFrame:%d\n",o->client_frame); print(o,text); }
    }
    if(okay)o->rendered=true;
    (void)in_water; return end(o,okay,e);
}
bool frontend_remote_q3_runtime_waiting_draw(void *context,const q3n_remote_frame *r,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; q3n_frame f;
    if(!o || !o->initialized || !o->prepared || o->prediction_prepared || o->information_prepared || !r ||
       r->snapshots.stage!=Q3N_REMOTE_AWAITING_SNAPSHOT || !begin(o,r,&f,e))return false;
    return end(o,q3n_loading_draw_information(o->loading,&f,e),e);
}
bool frontend_remote_q3_runtime_information_draw(void *context,const q3n_remote_frame *r,qa_error *e)
{
    frontend_remote_q3_runtime *o=context; q3n_frame f;
    if(!o || !o->initialized || !o->prepared || !o->information_prepared || o->prediction_prepared || !r ||
       r->snapshots.stage!=Q3N_REMOTE_LOADING_INFORMATION ||
       !frontend_remote_q3_runtime_information_current(o,&r->source,r->loading_information_text) ||
       !begin(o,r,&f,e))return false;
    return end(o,q3n_loading_draw_information(o->loading,&f,e),e);
}

/* The outer graph imports media, client-info, fonts, the asset registry and
 * attached audio bus first. This child codec owns only its private children
 * and an unattached music decoder; it never replays CG_Init or a cue. */
static bool codec_ready(const frontend_remote_q3_runtime *o,bool reading,qa_error *e)
{
    qa_q3_presentation_binding backend; q3n_remote_source_view source;
    frontend_remote_q3_services_view services;
    const qa_q3_presentation_assets *a=o?o->services.resources.assets:NULL;
    if(!attached(o) || !o->complete || !o->console || o->busy || o->entered || o->prepared || o->retiring ||
       (reading?!o->restoring||!o->frontend->source_restoring:!o->frontend->capture||o->restoring) ||
       !children_returned(o) || (o->frames&&!frontend_remote_q3_frame_idle(o->frames)) ||
       !a || !a->capturing || a->busy!=1 || a->codec_busy ||
       !frontend_remote_q3_services_read(o->parent,&services,e) ||
       services.client!=o->services.client || services.source!=o->services.source ||
       services.media!=o->services.media || services.clients!=o->services.clients || services.resources.assets!=a ||
       !qa_native_q3_remote_client_idle(services.client) || !q3n_remote_source_idle(services.source) ||
       !q3n_media_idle(services.media) || !q3n_clients_idle(services.clients) ||
       !q3n_remote_source_read(services.source,&source,e) ||
       !qa_q3_presentation_binding_read(o->children.presentation,&backend,e) ||
       backend.options.assets!=a || backend.options.owner!=services.resources.identity ||
       backend.options.seat!=services.resources.physical_seat || backend.world!=services.resources.world ||
       backend.geometry!=services.resources.geometry)
        return fail(e,QA_ERROR_ARGUMENT,"Remote runtime codec requires its actual returned graph and registry capture");
    return true;
}
bool frontend_remote_q3_runtime_capture_current(const frontend_remote_q3_runtime *o)
{
    const frontend_capture *capture=o?o->frontend->capture:NULL;
    if(!capture)return false;
    for(size_t i=0;;++i) {
        const qa_q3_presentation_assets *assets=frontend_capture_assets_at(capture,i);
        if(!assets)return false;
        if(assets==o->services.resources.assets)return codec_ready(o,false,NULL);
    }
}
