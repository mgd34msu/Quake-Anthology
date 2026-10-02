#include "unified_q3_runtime_private.h"
#include "unified_q3_runtime_video.h"
#include "../../presentation/q3_native/packet_compiled.h"
#include "../../presentation/q3_native/marks.h"
#include "../../presentation/q3_native/local_entities.h"
#include "qa/q3_presentation_save.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *e,const char *text)
{ qa_error_set(e,QA_ERROR_ARGUMENT,0,"%s",text); return false; }
static int32_t subtract(int32_t a,int32_t b)
{ uint32_t bits=(uint32_t)a-(uint32_t)b; int32_t value; memcpy(&value,&bits,4); return value; }
static bool parent_current(const frontend_unified_q3_runtime *o)
{
    return o && !o->retiring && !o->restoring && o->options.current(o->options.context,&o->options) &&
        frontend_remote_unified_current(o->options.replica,NULL) && frontend_unified_q3_client_current(o->options.client);
}
bool frontend_unified_q3_runtime_current(const frontend_unified_q3_runtime *o)
{ return parent_current(o) && o->complete && !o->faulted; }
static bool children_idle(const frontend_unified_q3_runtime *o)
{
    const frontend_unified_q3_runtime_owners *c=&o->children;
    return qa_q3_presentation_idle(c->presentation) && q3n_weapons_idle(c->weapons) && q3n_events_idle(c->events) &&
        q3n_particles_idle(c->particles) && q3n_view_idle(c->view) && q3n_player_state_idle(c->player_state) &&
        q3n_hud_idle(c->hud) && q3n_server_commands_idle(c->commands) && q3n_loading_idle(c->loading) &&
        q3n_mission_hud_idle(c->mission) && frontend_unified_q3_snapshots_idle(c->snapshots);
}
bool frontend_unified_q3_runtime_idle(const frontend_unified_q3_runtime *o)
{ return !o || (!o->busy && !o->entered && !o->command && !o->rebind && children_idle(o)); }
static bool cut(frontend_unified_q3_runtime *o,const q3n_frame *f,qa_error *e)
{ return f && f->compiled==o->entered && frontend_unified_q3_runtime_current(o) && q3n_frame_current(f) ? true :
    fail(e,"Unified CG callback left its actual Source/cache/resource receipt"); }
static bool begin(frontend_unified_q3_runtime *o,const q3n_compiled_frame *r,q3n_frame *f,qa_error *e)
{
    if(!frontend_unified_q3_runtime_current(o) || o->busy || !r ||
       r->source.owner!=frontend_unified_q3_client_source(o->options.client) || !q3n_compiled_frame_current(r))
        return fail(e,"Unified CG entry requires its actual returned compiled Source");
    o->busy=true; o->entered=r;
    bool okay=o->options.frame_settings(o->options.context,r,r->stage==Q3N_COMPILED_INITIALIZATION,o->stereo,&o->settings,e);
    const q3n_compiled_source_basis *b=&r->source.basis;
    *f=(q3n_frame){.application=b->application,.compiled=r,.presentation=o->children.presentation,.assets=b->assets,
        .clients=o->options.clients,.media=o->options.media,.weapons=o->children.weapons,.events=o->children.events,
        .particles=o->children.particles,.view=o->children.view,.player_state=o->children.player_state,
        .server_commands=o->children.commands,.entities=r->entities,.seat=b->seat,.viewing_client=(uint32_t)b->client_number,
        .physical_presentation_seat=b->physical_seat,.viewing_actor=b->viewer,.time=r->time,
        .frame_milliseconds=o->frame_milliseconds,.client_frame=o->client_frame,.refdef=o->refdef,.view_angles=o->view_angles,
        .weapon_settings=&o->settings.weapons,.event_settings=&o->settings.events};
    if(okay)okay=o->options.preferences(o->options.context,&f->preferences,e) && cut(o,f,e);
    if(!okay) { o->entered=NULL; o->busy=false; }
    return okay;
}
static bool end(frontend_unified_q3_runtime *o,bool okay,qa_error *e)
{
    if(okay)okay=parent_current(o) && q3n_compiled_frame_current(o->entered);
    o->command=NULL; o->entered=NULL; o->busy=false;
    if(!okay) { o->faulted=true; if(e && e->code==QA_OK)fail(e,"Unified CG returned with a stale Source receipt"); }
    return okay;
}
static bool receipt_current(void *ctx,const q3n_frame *f,const q3n_server_command_receipt *r)
{
    frontend_unified_q3_runtime *o=ctx; const frontend_unified_q3_command *c=o->command;
    return cut(o,f,NULL) && c && frontend_unified_q3_command_current(c) && r &&
        r->compiled_source==f->compiled->source.owner && r->sequence==c->sequence && r->present==c->present &&
        r->arguments==c->arguments;
}
static bool compiled_current(void *ctx,const q3n_frame *f,qa_cvars *vars,const qa_command_context *command)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,NULL) &&
    o->options.commands.compiled_current(o->options.commands.context,f,vars,command); }
static bool cvar_read(void *ctx,const char *name,qa_native_q3_client_cvar *out,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return frontend_unified_q3_client_cvar_read(o->options.client,name,out,e); }
static bool register_commands(void *ctx,const q3n_frame *f,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    frontend_unified_q3_client_register(o->options.client,e) &&
    o->options.commands.compiled_register(o->options.commands.context,f,e) && cut(o,f,e); }
static bool console(void *ctx,const q3n_frame *f,const char *text,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    o->options.commands.compiled_console(o->options.commands.context,f,text,e) && cut(o,f,e); }
static bool center(void *ctx,const q3n_frame *f,const qa_command_context *c,const char *text,int32_t y,int32_t width,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; (void)c; return cut(o,f,e) &&
    q3n_hud_center_print(o->children.hud,f,text,y,width,e) && cut(o,f,e); }
static bool message(void *ctx,const q3n_command_message *m,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,m->frame,e) &&
    o->options.commands.message(o->options.commands.context,m,e) && cut(o,m->frame,e); }
static bool client_settings(void *ctx,const q3n_frame *f,bool loading,q3n_client_settings *out,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; q3n_native_frame_options settings;
    if(!cut(o,f,e) || !o->options.frame_settings(o->options.context,f->compiled,loading,o->stereo,&settings,e) || !cut(o,f,e))return false;
    *out=settings.clients; return true; }
static bool loading(void *ctx,const q3n_frame *f,const char *text,int32_t item,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx;
    if(!cut(o,f,e))return false;
    bool okay=item>=0?q3n_loading_item(o->children.loading,f,(uint32_t)item,e):q3n_loading_string(o->children.loading,f,text,e);
    return okay && cut(o,f,e); }
static bool stage(void *ctx,const q3n_frame *f,q3n_command_init_stage which,const char *map,int32_t physical,uint32_t *extent,qa_error *e)
{
    frontend_unified_q3_runtime *o=ctx; if(!cut(o,f,e))return false;
    bool okay;
    if(which==Q3N_INIT_PARTICLES)okay=q3n_particles_load_compiled(o->children.particles,f,e);
    else if(which==Q3N_INIT_MISSION_ASSETS || which==Q3N_INIT_HUD_MENU || which==Q3N_INIT_TEAM_CHAT || which==Q3N_INIT_STRING_TABLE)
        okay=o->children.mission?q3n_mission_hud_initialize(o->children.mission,f,which,e):
            o->options.commands.initialize_stage(o->options.commands.context,f,which,map,physical,extent,e);
    else okay=o->options.commands.initialize_stage(o->options.commands.context,f,which,map,physical,extent,e);
    return okay && cut(o,f,e);
}
static bool clear_particles(void *ctx,const q3n_frame *f,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) && q3n_particles_load_compiled(o->children.particles,f,e) && cut(o,f,e); }
static bool score(void *ctx,const q3n_frame *f,const q3n_command_state *s,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return o->children.mission && q3n_mission_hud_score_selection(o->children.mission,f,s,e); }
static bool response(void *ctx,const q3n_frame *f,const q3n_command_state *s,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return o->children.mission && q3n_mission_hud_response(o->children.mission,f,s,e); }
static int32_t memory_remaining(void *ctx)
{ frontend_unified_q3_runtime *o=ctx; return o->options.commands.memory_remaining(o->options.commands.context); }
static bool reached(void *ctx,const q3n_compiled_frame *r,const frontend_unified_q3_command *command,qa_error *e)
{
    frontend_unified_q3_runtime *o=ctx; q3n_frame f;
    if(!command || !begin(o,r,&f,e))return false;
    o->command=command;
    q3n_server_command_receipt receipt={.compiled_source=r->source.owner,
        .compiled_context=*frontend_unified_q3_client_context(o->options.client),.sequence=command->sequence,
        .present=command->present,.arguments=command->arguments};
    bool okay=q3n_server_commands_compiled_dispatch(o->children.commands,&f,&receipt,e);
    const q3n_command_state *state=q3n_server_commands_state(o->children.commands);
    if(okay && state->map_restart) { q3n_player_state_round(o->children.player_state);
        okay=q3n_server_commands_map_restart_taken(o->children.commands,&f,e); }
    return end(o,okay,e);
}
static bool respawn(void *ctx,const q3n_compiled_frame *r,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; q3n_frame f; return begin(o,r,&f,e) &&
    end(o,q3n_player_state_respawn_compiled(o->children.player_state,&f,e),e); }
static bool reset_player(void *ctx,const q3n_compiled_frame *r,const q3n_compiled_entity *row,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; q3n_frame f; if(!begin(o,r,&f,e))return false;
    bool okay=row && row->frame==r && q3n_compiled_entity_current(row);
    if(okay)q3n_player_reset(&row->presentation->player,row->presentation->lerp_angles);
    return end(o,okay,e); }
static bool event(void *ctx,const q3n_compiled_frame *r,const q3n_compiled_entity *row,const qa_q3_entity *scratch,qa_vec3 position,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; q3n_frame f; return begin(o,r,&f,e) &&
    end(o,q3n_events_apply_compiled(&f,row,scratch,position,e),e); }
static bool transition(void *ctx,const q3n_compiled_frame *r,const qa_q3_player *p,const qa_q3_player *before,qa_error *e)
{
    frontend_unified_q3_runtime *o=ctx; q3n_frame f; if(!begin(o,r,&f,e))return false;
    const q3n_command_state *s=q3n_server_commands_state(o->children.commands); qa_native_q3_client_cvar miss;
    bool okay=s && cvar_read(o,"cg_showmiss",&miss,e);
    q3n_player_state_context settings={0};
    if(okay)settings=(q3n_player_state_context){s->warmup,s->timelimit,s->fraglimit,s->scores1,s->intermission_started,miss.integer!=0};
    return end(o,okay && q3n_player_state_transition_compiled(o->children.player_state,&f,p,before,&settings,e),e);
}
static bool lagometer(void *ctx,const qa_q3_snapshot *s,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; if(!parent_current(o))return fail(e,"Unified lagometer lost its physical CLIENT");
    q3n_hud_snapshot_sample(o->children.hud,s==NULL,s?s->player.ping:0,s?s->flags:0); return true; }
static bool warning(void *ctx,const char *text,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; if(!parent_current(o))return fail(e,"Unified warning lost its physical CLIENT");
    o->options.view.print(o->options.view.context,text); return parent_current(o); }
static bool trace_number(void *ctx,const q3n_compiled_frame *r,const qa_trace_result *hit,int32_t *out,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return parent_current(o) &&
    o->options.trace_number(o->options.context,r,hit,out,e) && parent_current(o); }
static bool draw_reason(void *ctx,q3n_compiled_stage stage)
{
    frontend_unified_q3_runtime *o=ctx;
    if(!frontend_unified_q3_runtime_current(o) || !o->initialized || !o->prepared || o->prediction_prepared)return false;
    const char *text=q3n_loading_text(o->children.loading);
    return text && (stage==Q3N_COMPILED_LOADING_INFORMATION ? o->information_prepared && *text :
        stage==Q3N_COMPILED_AWAITING_SNAPSHOT && !o->information_prepared && !*text);
}
frontend_unified_q3_snapshots_options frontend_unified_q3_runtime_cache_options(frontend_unified_q3_runtime *o)
{ return (frontend_unified_q3_snapshots_options){.client=o->options.client,.context=o,.reached=reached,.respawn=respawn,
    .reset_player=reset_player,.event=event,.transition_player=transition,.lagometer=lagometer,.warning=warning,
    .trace_number=trace_number,.draw_reason=draw_reason}; }
bool frontend_unified_q3_runtime_build_children(frontend_unified_q3_runtime *o,qa_error *e)
{
    q3n_compiled_source *source=frontend_unified_q3_client_source(o->options.client);
    q3n_compiled_source_view view;
    if(!q3n_compiled_source_checkpoint_read(source,&view,e))return false;
    frontend_unified_q3_runtime_owners *c=&o->children;
    c->media=o->options.media; c->clients=o->options.clients;
    q3n_weapon_options weapons=o->options.weapons;
    q3n_event_options events=o->options.events; events.compiled_source=source;
    q3n_view_options camera=o->options.view; camera.compiled_source=source;
    q3n_player_state_options player=o->options.player_state; player.compiled_source=source;
    q3n_hud_options hud=o->options.hud; hud.compiled_source=source;
    q3n_loading_options load=o->options.loading; load.compiled_source=source;
    q3n_mission_hud_options mission=o->options.mission; mission.compiled_source=source;
    if(!qa_q3_presentation_create(&o->options.presentation,&c->presentation,e) ||
       !q3n_weapons_create(&weapons,&c->weapons,e) || !q3n_events_create_compiled(&events,&c->events,e) ||
       !q3n_particles_create_compiled(view.basis.assets,source,&c->particles,e) ||
       !q3n_view_create_compiled(&camera,&c->view,e) || !q3n_player_state_create_compiled(&player,&c->player_state,e) ||
       !q3n_hud_create_compiled(&hud,&c->hud,e))return false;
    q3n_server_command_options commands=o->options.commands;
    commands.compiled_source=source; commands.context=o; commands.presentation=c->presentation;
    commands.clients=c->clients; commands.media=c->media; commands.events=c->events;
    commands.compiled_current=compiled_current; commands.compiled_cvar_read=cvar_read;
    commands.compiled_register=register_commands; commands.compiled_console=console; commands.compiled_center_print=center;
    commands.receipt_current=receipt_current; commands.message=message; commands.client_settings=client_settings;
    commands.loading=loading; commands.initialize_stage=stage; commands.clear_particles=clear_particles;
    commands.score_selection=score; commands.response_head=response; commands.memory_remaining=memory_remaining;
    if(!q3n_server_commands_create_compiled(&commands,&c->commands,e))return false;
    load.presentation=c->presentation; load.media=c->media;
    if(!q3n_loading_create_compiled(&load,&c->loading,e))return false;
    if(view.basis.product==QA_Q3_TEAM_ARENA) {
        mission.presentation=c->presentation;
        if(!q3n_mission_hud_create_compiled(&mission,&c->mission,e) || !q3n_mission_hud_bind(c->mission,c->hud,e))return false;
    }
    if(!o->restoring) {
        frontend_unified_q3_snapshots_options options=frontend_unified_q3_runtime_cache_options(o);
        bool okay=o->video_constructor?frontend_unified_q3_snapshots_create_video(&options,
            frontend_unified_q3_runtime_video_client(o->video),&c->snapshots,e):
            frontend_unified_q3_snapshots_create(&options,&c->snapshots,e);
        if(!okay)return false;
    }
    o->complete=true; return true;
}
static bool create(const frontend_unified_q3_runtime_options *options,bool restoring,frontend_unified_q3_runtime **out,qa_error *e)
{
    if(!out || *out || !options || !options->frontend || !options->replica || !options->client || !options->media ||
       !options->clients || !options->current || !options->frame_settings || !options->preferences ||
       !options->backend_frame || !options->trace_number || !options->command_values || !options->prediction_cursor ||
       !options->timescale || !options->backend_checkpoint || !options->backend_restore ||
       !options->player_fx.world_trace || !options->player_fx.world_point_contents ||
       !options->player_fx.body_hidden || !options->player_fx.body_submit ||
       !options->commands.compiled_current || !options->commands.compiled_register ||
       !options->commands.compiled_console || !options->commands.message || !options->commands.initialize_stage ||
       !options->commands.memory_remaining || !options->view.print ||
       !(restoring?frontend_unified_q3_client_checkpoint_current(options->client):frontend_unified_q3_client_current(options->client)))
        return fail(e,"Unified CG requires its real factory and compiled CLIENT services");
    frontend_unified_q3_runtime *o=calloc(1,sizeof(*o));
    if(!o) { qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining Unified CG runtime"); return false; }
    o->options=*options; o->restoring=restoring; *out=o;
    return frontend_unified_q3_runtime_build_children(o,e);
}
bool frontend_unified_q3_runtime_create(const frontend_unified_q3_runtime_options *o,frontend_unified_q3_runtime **out,qa_error *e)
{ return create(o,false,out,e); }
bool frontend_unified_q3_runtime_create_restored(const frontend_unified_q3_runtime_options *o,frontend_unified_q3_runtime **out,qa_error *e)
{ return create(o,true,out,e); }
bool frontend_unified_q3_runtime_close_children(frontend_unified_q3_runtime *o,qa_error *e)
{
    if(!frontend_unified_q3_runtime_idle(o))return fail(e,"Unified CG teardown retains an entered child");
    frontend_unified_q3_runtime_owners *c=&o->children;
    if(!frontend_unified_q3_snapshots_destroy(&c->snapshots,e))return false;
    q3n_loading_destroy(c->loading); c->loading=NULL; q3n_mission_hud_destroy(c->mission); c->mission=NULL;
    q3n_server_commands_destroy(c->commands); c->commands=NULL; q3n_hud_destroy(c->hud); c->hud=NULL;
    q3n_player_state_destroy(c->player_state); c->player_state=NULL; q3n_view_destroy(c->view); c->view=NULL;
    q3n_particles_destroy(c->particles); c->particles=NULL; q3n_events_destroy(c->events); c->events=NULL;
    q3n_weapons_destroy(c->weapons); c->weapons=NULL;
    if(c->presentation && !qa_q3_presentation_destroy(c->presentation,e))return false;
    c->presentation=NULL; o->complete=false; return true;
}
bool frontend_unified_q3_runtime_destroy(frontend_unified_q3_runtime **out,qa_error *e)
{ if(!out || !*out)return true; frontend_unified_q3_runtime *o=*out;
    if(o->video || o->prepared || !frontend_unified_q3_runtime_idle(o))return fail(e,"Unified CG retirement retains a frame or video ticket");
    o->retiring=true; if(!frontend_unified_q3_runtime_close_children(o,e))return false;
    free(o); *out=NULL; return true; }
bool frontend_unified_q3_runtime_owners_read(const frontend_unified_q3_runtime *o,frontend_unified_q3_runtime_owners *out,qa_error *e)
{ if(!o || !out || !o->complete || !frontend_unified_q3_runtime_idle(o))return fail(e,"Unified CG owner read requires its returned children");
    *out=o->children; return true; }
static bool initialize(void *ctx,const q3n_compiled_frame *r,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; q3n_frame f; if(!begin(o,r,&f,e))return false;
    bool okay=o->video_constructor?q3n_server_commands_initialize_video(o->children.commands,&f,r->source.basis.reached_command,e):
        q3n_server_commands_initialize(o->children.commands,&f,r->source.basis.initial_command,e);
    return end(o,okay,e); }
bool frontend_unified_q3_runtime_initialize(frontend_unified_q3_runtime *o,qa_error *e)
{ if(!frontend_unified_q3_runtime_current(o) || o->initialized || o->video || !frontend_unified_q3_runtime_idle(o))
    return fail(e,"Unified CG Init requires its real fresh constructor");
    if(!frontend_unified_q3_snapshots_initialize(o->children.snapshots,initialize,o,e))return false;
    o->initialized=true; return true; }
bool frontend_unified_q3_runtime_initialize_video(frontend_unified_q3_runtime *o,qa_error *e)
{
    if(!frontend_unified_q3_runtime_current(o) || o->initialized || !o->video_constructor || !o->video ||
       !frontend_unified_q3_runtime_video_current(o->video,e))return fail(e,"Unified video Init requires its actual retained CLIENT ticket");
    if(!frontend_unified_q3_snapshots_initialize(o->children.snapshots,initialize,o,e))return false;
    o->initialized=true; o->video_constructor=false; return true;
}
bool frontend_unified_q3_runtime_prepare(frontend_unified_q3_runtime *o,uint32_t stereo,qa_error *e)
{
    if(!frontend_unified_q3_runtime_current(o) || !o->initialized || o->prepared || o->video || stereo>2 ||
       !frontend_unified_q3_runtime_idle(o))return fail(e,"Unified CG draw requires its returned initialized owner");
    o->stereo=stereo; o->prepared=true; o->rendered=false; o->prediction_prepared=false;
    bool okay=frontend_unified_q3_client_cvars_update(o->options.client,e) &&
        o->options.backend_frame(o->options.context,o->children.presentation,e);
    const char *text=q3n_loading_text(o->children.loading);
    if(okay && !text)okay=fail(e,"Unified CG lost its actual loading text owner");
    if(okay) { o->information_prepared=*text!=0;
        if(!o->information_prepared)okay=qa_q3_presentation_clear_loops(o->children.presentation,false,e) &&
            qa_q3_presentation_clear(o->children.presentation,e); }
    if(!okay)o->faulted=true; return okay;
}
bool frontend_unified_q3_runtime_process(frontend_unified_q3_runtime *o,int32_t time,bool *active,qa_error *e)
{
    if(!active || !frontend_unified_q3_runtime_current(o) || !o->prepared || o->information_prepared || o->prediction_prepared)
        return fail(e,"Unified CG snapshots require the actual prepared frame");
    qa_native_q3_client_cvar no_predict,synchronous;
    if(!cvar_read(o,"cg_nopredict",&no_predict,e) || !cvar_read(o,"g_synchronousClients",&synchronous,e) ||
       !frontend_unified_q3_snapshots_process(o->children.snapshots,time,no_predict.integer!=0,synchronous.integer!=0,e))return false;
    q3n_compiled_frame frame;
    *active=frontend_unified_q3_snapshots_read(o->children.snapshots,&frame,NULL) && frame.snapshot && !(frame.snapshot->flags&2);
    if(!*active)return true;
    const q3n_view_state *view=q3n_view_read(o->children.view);
    const q3n_weapon_selection *weapon=q3n_weapons_selection(o->children.weapons);
    if(!o->options.command_values(o->options.context,weapon->weapon,view->zoom_sensitivity,e))return false;
    uint32_t bits=(uint32_t)o->client_frame+1u; memcpy(&o->client_frame,&bits,4); o->prediction_prepared=true; return true;
}
bool frontend_unified_q3_runtime_prediction(frontend_unified_q3_runtime *o,const qa_q3_player *player,uint64_t receipt,
    qa_vec3 correction,int32_t time,bool hyperspace,qa_error *e)
{ return frontend_unified_q3_runtime_current(o) && o->prepared && o->prediction_prepared &&
    frontend_unified_q3_snapshots_prediction(o->children.snapshots,player,receipt,correction,time,hyperspace,e); }
static int32_t packet_rand(void *ctx)
{ return q3n_events_rand(((frontend_unified_q3_runtime *)ctx)->children.events); }
static bool packet_body(void *ctx,const q3n_frame *f,const q3n_compiled_entity *row,q3n_entity *cent,
    const qa_q3_ref_entity *ref,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) && row && cent==row->presentation &&
    q3n_compiled_entity_current(row) && qa_q3_presentation_entity(f->presentation,ref,e) && cut(o,f,e); }
static bool world_trace(void *ctx,const q3n_frame *f,qa_vec3 start,qa_vec3 finish,qa_bounds bounds,uint32_t mask,
    qa_trace_result *out,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    o->options.player_fx.world_trace(o->options.player_fx.context,f,start,finish,bounds,mask,out,e) && cut(o,f,e); }
static bool world_contents(void *ctx,const q3n_frame *f,qa_vec3 point,uint32_t *out,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    o->options.player_fx.world_point_contents(o->options.player_fx.context,f,point,out,e) && cut(o,f,e); }
static bool body_hidden(void *ctx,const q3n_frame *f,const q3n_compiled_entity *row,bool *hidden,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    o->options.player_fx.body_hidden(o->options.player_fx.context,f,row,hidden,e) && cut(o,f,e); }
static bool body_submit(void *ctx,const q3n_frame *f,const q3n_compiled_entity *row,uint32_t part,
    const qa_q3_ref_entity *ref,bool base,bool *consumed,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    o->options.player_fx.body_submit(o->options.player_fx.context,f,row,part,ref,base,consumed,e) && cut(o,f,e); }
static bool player_weapon(void *ctx,const q3n_frame *f,const q3n_compiled_entity *row,const qa_q3_ref_entity *torso,
    int32_t team,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; (void)team;
    return cut(o,f,e) && q3n_weapons_player_compiled(f,torso,row,e) && cut(o,f,e); }
static bool packet_player(void *ctx,const q3n_frame *f,const q3n_compiled_entity *row,q3n_entity *cent,qa_error *e)
{
    frontend_unified_q3_runtime *o=ctx;
    if(!cut(o,f,e) || !row || row->presentation!=cent || !q3n_compiled_entity_current(row))return false;
    int32_t client=row->current->clientNum;
    if(client<0 || client>=64)return fail(e,"Unified player has an invalid physical client-info index");
    const q3n_client_info *ci=q3n_clients_get(f->clients,(uint32_t)client);
    if(!ci || !ci->info_valid)return true;
    if(cent->client_media_revision!=ci->media_revision) {
        q3n_player_reset(&cent->player,qa_v3(row->current->angles[0],row->current->angles[1],row->current->angles[2]));
        cent->client_media_revision=ci->media_revision;
    }
    const qa_q3_player *ps=q3n_frame_predicted_player(f);
    q3n_body_options options={.time=f->time,.frame_milliseconds=f->frame_milliseconds,.local_view_client=ps->clientNum,
        .shadow_mode=o->settings.player_fx.shadow_mode,.swing_speed=o->settings.swing_speed,
        .no_player_animations=o->settings.no_player_animations,.animations_disabled=o->settings.animations_disabled,
        .third_person=f->third_person,.camera_mode=o->settings.view.camera_mode};
    q3n_player_body body;
    q3n_player_fx_compiled_backend backend={.context=o,.world_trace=world_trace,.world_point_contents=world_contents,
        .body_hidden=body_hidden,.body_submit=body_submit,.player_weapon=player_weapon};
    return q3n_player_body_build_compiled(f,row,ci,&options,&body,e) &&
        q3n_player_fx_submit_compiled(f,row,ci,&body,&o->settings.player_fx,&backend,e) && cut(o,f,e);
}
static bool trail(void *ctx,const q3n_frame *f,const q3n_compiled_entity *row,q3n_entity *cent,
    const q3n_weapon_media *media,bool grapple,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; (void)media; (void)grapple;
    return cut(o,f,e) && cent==row->presentation && q3n_weapons_trail_compiled(f,row,e) && cut(o,f,e); }
static bool powerups(void *ctx,const q3n_frame *f,const q3n_compiled_entity *row,q3n_entity *cent,
    const qa_q3_ref_entity *base,int32_t team,qa_error *e)
{
    const q3n_media_view *m=q3n_media_read(f->media); qa_q3_ref_entity ref=*base;
    if(!m)return fail(e,"Unified powerup lost its genuine media registry");
    uint32_t p=(uint32_t)row->current->powerups;
    if(p&(1u<<4)) { ref.custom_shader=m->graphics[Q3N_G_INVIS]; return packet_body(ctx,f,row,cent,&ref,e); }
    if(!packet_body(ctx,f,row,cent,&ref,e))return false;
    if(p&(1u<<1)) { ref.custom_shader=m->graphics[team==1?Q3N_G_RED_QUAD:Q3N_G_QUAD];
        if(!packet_body(ctx,f,row,cent,&ref,e))return false; }
    if((p&(1u<<5)) && (f->time/100)%10==1) { ref.custom_shader=m->graphics[Q3N_G_REGEN];
        if(!packet_body(ctx,f,row,cent,&ref,e))return false; }
    if(p&(1u<<2)) { ref.custom_shader=m->graphics[Q3N_G_BATTLE_SUIT];
        if(!packet_body(ctx,f,row,cent,&ref,e))return false; }
    return true;
}
static bool register_weapon(frontend_unified_q3_runtime *o,const q3n_frame *f,int32_t number,bool done[16],qa_error *e)
{ if(number<0 || number>=16)return fail(e,"Unified weapon is outside its genuine registry");
    if(done[number])return true;
    if(!q3n_media_register_weapon(f->media,(uint32_t)number,e) || !cut(o,f,e))return false;
    done[number]=true; return true; }
static bool required_media(frontend_unified_q3_runtime *o,const q3n_frame *f,qa_error *e)
{
    bool done[16]={0}; const qa_q3_player *predicted=q3n_frame_predicted_player(f),*snapshot=q3n_frame_snapshot_player(f);
    if(!predicted || !snapshot || !register_weapon(o,f,predicted->weapon,done,e))return false;
    int32_t extent=q3n_frame_product(f)==QA_Q3_TEAM_ARENA?14:11;
    uint32_t owned=(uint32_t)snapshot->stats[q3n_frame_product(f)==QA_Q3_TEAM_ARENA?3:2];
    for(int32_t i=1;i<extent;++i)if((owned&(1u<<(unsigned)i)) && !register_weapon(o,f,i,done,e))return false;
    const qa_q3_snapshot *snap=f->compiled->snapshot;
    for(size_t i=0;i<snap->entity_count;++i) {
        q3n_compiled_entity row; int32_t number=snap->entities[i].number;
        if(number<0 || !q3n_compiled_frame_entity(f->compiled,(uint32_t)number,&row,e))return false;
        int32_t type=row.current->eType,weapon=row.current->weapon;
        if(type==1 || type==3 || type==11) {
            if(type!=1 && weapon>extent)weapon=0;
            if(!register_weapon(o,f,weapon,done,e))return false;
        }
    }
    return true;
}
static bool packet(frontend_unified_q3_runtime *o,q3n_frame *f,qa_error *e)
{
    int32_t before=f->compiled->predicted_player->entityEventSequence;
    if(!q3n_packet_compiled_predict(f,e) ||
       !o->options.prediction_cursor(o->options.context,f->compiled,before,f->compiled->predicted_player->entityEventSequence,e) ||
       !cut(o,f,e))return false;
    q3n_packet_compiled_imports imports={.context=o,.rand=packet_rand,.body=packet_body,.player=packet_player,
        .trail=trail,.powerups=powerups};
    q3n_compiled_entity predicted,followed; const qa_q3_player *ps=q3n_frame_predicted_player(f);
    if(!q3n_compiled_frame_predicted(f->compiled,&predicted,e) ||
       !q3n_packet_compiled_entity(f,&predicted,&o->settings.packet,&imports,e) || ps->clientNum<0 ||
       !q3n_compiled_frame_entity(f->compiled,(uint32_t)ps->clientNum,&followed,e) ||
       !q3n_packet_compiled_lerp(f,&followed,&o->settings.packet,e) || !q3n_packet_compiled_sound_position(f,&followed,e))return false;
    const qa_q3_snapshot *snapshot=f->compiled->snapshot;
    for(size_t i=0;i<snapshot->entity_count;++i) {
        q3n_compiled_entity row; int32_t number=snapshot->entities[i].number;
        if(number<0 || !q3n_compiled_frame_entity(f->compiled,(uint32_t)number,&row,e))return false;
        if(row.current_valid && !q3n_packet_compiled_entity(f,&row,&o->settings.packet,&imports,e))return false;
    }
    return cut(o,f,e);
}
static bool powerup_audio(frontend_unified_q3_runtime *o,const q3n_frame *f,qa_error *e)
{
    const qa_q3_player *snapshot=q3n_frame_snapshot_player(f); const q3n_media_view *media=q3n_media_read(f->media);
    if(!snapshot || !media)return false;
    for(unsigned i=0;i<16;++i) {
        int32_t expiry=snapshot->powerups[i]; if(expiry<=f->time)continue;
        int32_t remaining=subtract(expiry,f->time),previous=subtract(expiry,o->old_time);
        if(remaining<5000 && remaining/1000!=previous/1000 &&
           (!qa_q3_presentation_sound(f->presentation,media->sounds[Q3N_S_WEAR_OFF],NULL,snapshot->clientNum,4,false,e) || !cut(o,f,e)))return false;
    }
    return true;
}
static bool information_draw(void *ctx,const q3n_compiled_frame *r,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; q3n_frame f; return begin(o,r,&f,e) &&
    end(o,q3n_loading_draw_information(o->children.loading,&f,e),e); }
bool frontend_unified_q3_runtime_draw(frontend_unified_q3_runtime *o,bool *rendered,qa_error *e)
{
    if(!rendered || !frontend_unified_q3_runtime_current(o) || !o->prepared || !o->initialized)
        return fail(e,"Unified draw requires its genuine prepared CG owner");
    *rendered=false;
    if(!o->prediction_prepared) {
        q3n_compiled_stage stage=o->information_prepared?Q3N_COMPILED_LOADING_INFORMATION:Q3N_COMPILED_AWAITING_SNAPSHOT;
        bool okay=frontend_unified_q3_snapshots_entered_draw(o->children.snapshots,stage,information_draw,o,e);
        if(okay)*rendered=o->rendered=true; return okay;
    }
    q3n_compiled_frame r; q3n_frame f;
    if(!frontend_unified_q3_snapshots_read(o->children.snapshots,&r,e) || !begin(o,&r,&f,e))return false;
    bool in_water=false; qa_scene_rect viewport=o->options.presentation.viewport;
    bool okay=q3n_view_frame(o->children.view,&f,&o->settings.view,o->children.player_state,viewport,&in_water,e) && required_media(o,&f,e);
    if(okay)memcpy(f.refdef.area_mask,r.snapshot->area_mask,sizeof(f.refdef.area_mask));
    if(okay && !f.third_person)okay=q3n_view_damage_blob(o->children.view,&f,&o->settings.view,o->children.player_state,e);
    if(okay && !r.hyperspace)okay=packet(o,&f,e) && q3n_marks_submit(&f,e) && q3n_particles_add(&f,e) && q3n_local_submit(&f,e);
    const q3n_view_state *camera=q3n_view_read(o->children.view); const q3n_event_state *events=q3n_events_state(o->children.events);
    q3n_weapon_view weapon={.predicted_entity=r.predicted_entity,.predicted_state=r.predicted_state,.bob_cycle=camera->bob_cycle,
        .xy_speed=camera->xy_speed,.bob_fraction_sin=camera->bob_fraction_sin,.land_time=events->land_time,
        .land_change=events->land_change,.test_gun=camera->test_gun};
    const qa_q3_player *snapshot=q3n_frame_snapshot_player(&f);
    if(okay)okay=q3n_weapons_view(&f,&weapon,e) && q3n_events_finish(&f,e) &&
        q3n_server_commands_finish(o->children.commands,&f,e) && q3n_view_test_submit(o->children.view,&f,&o->settings.view,e) &&
        powerup_audio(o,&f,e) && qa_q3_presentation_listener(f.presentation,snapshot->clientNum,f.refdef.origin,f.refdef.axis,e);
    if(okay && o->stereo!=2) {
        int32_t elapsed=subtract(f.time,o->old_time); o->frame_milliseconds=elapsed<0?0:elapsed; o->old_time=f.time;
        q3n_hud_frame_sample(o->children.hud,subtract(f.time,r.source.basis.time));
    }
    if(okay)okay=o->options.timescale(o->options.context,o->frame_milliseconds,e) && cut(o,&f,e);
    bool tournament=snapshot->persistant[3]==3 && (snapshot->pmFlags&8192);
    if(okay) { o->refdef=f.refdef; o->view_angles=f.view_angles; }
    if(okay && !tournament) {
        okay=q3n_hud_tile_clear(o->children.hud,&f,viewport,e); qa_q3_refdef render=f.refdef;
        volatile float separation=o->stereo==0?0:o->settings.stereo_separation*(o->stereo==1?-0.5f:0.5f);
        volatile float x=render.axis[1].x*-separation,y=render.axis[1].y*-separation,z=render.axis[1].z*-separation;
        render.origin.x+=x; render.origin.y+=y; render.origin.z+=z;
        if(okay)okay=qa_q3_presentation_render(f.presentation,&render,e) && cut(o,&f,e);
    }
    if(okay)okay=q3n_hud_frame(o->children.hud,&f,&o->settings.hud,o->children.commands,o->children.player_state,viewport,e);
    if(okay)*rendered=o->rendered=true;
    (void)in_water; return end(o,okay,e);
}
bool frontend_unified_q3_runtime_frame_end(frontend_unified_q3_runtime *o,bool completed,qa_error *e)
{
    if(!o || !o->prepared || !frontend_unified_q3_runtime_idle(o))return fail(e,"Unified CG frame end still retains a child callback");
    o->prepared=false; o->prediction_prepared=false; o->information_prepared=false;
    if(!completed)o->faulted=true; return completed;
}
bool frontend_unified_q3_runtime_rebind_prepare(frontend_unified_q3_runtime *o,const frontend_unified_q3_client_frame *frame,qa_error *e)
{
    const q3n_compiled_source_rebind_ticket *ticket=frontend_unified_q3_client_frame_rebind(frame);
    const qa_command_context *context=frontend_unified_q3_client_frame_context(frame);
    if(!o || o->rebind || o->prepared || !frontend_unified_q3_runtime_idle(o) || !ticket || !context)
        return fail(e,"Unified CG round requires the genuine prepared CLIENT cohort");
    if(!q3n_server_commands_rebind_prepare(o->children.commands,ticket,context,e))return false;
    if(o->children.mission && !q3n_mission_hud_rebind_prepare(o->children.mission,ticket,context,e)) {
        q3n_server_commands_rebind_abort(o->children.commands,ticket); return false; }
    o->rebind=frame; return true;
}
bool frontend_unified_q3_runtime_rebind_ready(const frontend_unified_q3_runtime *o,const frontend_unified_q3_client_frame *f)
{ const q3n_compiled_source_rebind_ticket *t=frontend_unified_q3_client_frame_rebind(f);
    return o && o->rebind==f && frontend_unified_q3_client_ready(f) && q3n_server_commands_rebind_ready(o->children.commands,t) &&
        (!o->children.mission || q3n_mission_hud_rebind_ready(o->children.mission,t)); }
void frontend_unified_q3_runtime_rebind_commit(frontend_unified_q3_runtime *o,const frontend_unified_q3_client_frame *f)
{ if(!frontend_unified_q3_runtime_rebind_ready(o,f))return;
    const q3n_compiled_source_rebind_ticket *t=frontend_unified_q3_client_frame_rebind(f);
    q3n_server_commands_rebind_commit(o->children.commands,t);
    if(o->children.mission)q3n_mission_hud_rebind_commit(o->children.mission,t); o->rebind=NULL; }
void frontend_unified_q3_runtime_rebind_abort(frontend_unified_q3_runtime *o,const frontend_unified_q3_client_frame *f)
{ if(!o || o->rebind!=f)return; const q3n_compiled_source_rebind_ticket *t=frontend_unified_q3_client_frame_rebind(f);
    q3n_server_commands_rebind_abort(o->children.commands,t);
    if(o->children.mission)q3n_mission_hud_rebind_abort(o->children.mission,t); o->rebind=NULL; }
