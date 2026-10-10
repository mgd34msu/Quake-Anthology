#include "unified_q3_runtime_private.h"
#include "unified_q3_runtime_video.h"
#include "source_renderer_runtime.h"
#include "../../presentation/q3_native/packet_compiled.h"
#include "../../presentation/q3_native/marks.h"
#include "../../presentation/q3_native/local_entities.h"
#include "qa/q3_presentation_save.h"
#include "qa/q3_source_scene_bank.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *e,const char *text)
{ qa_error_set(e,QA_ERROR_ARGUMENT,0,"%s",text); return false; }
static int32_t subtract(int32_t a,int32_t b)
{ uint32_t bits=(uint32_t)a-(uint32_t)b; int32_t value; memcpy(&value,&bits,4); return value; }
static bool parent_current(const frontend_unified_q3_runtime *o)
{
    return o && !o->retiring && o->options.current(o->options.context,&o->options) &&
        frontend_remote_unified_current(o->options.replica,NULL) && frontend_unified_q3_client_current(o->options.client);
}
bool frontend_unified_q3_runtime_current(const frontend_unified_q3_runtime *o)
{ return parent_current(o) && o->complete && !o->faulted; }
static bool children_returned(const frontend_unified_q3_runtime *o,bool checkpoint)
{
    const frontend_unified_q3_runtime_owners *c=&o->children;
    const q3n_compiled_source_rebind_ticket *t=checkpoint&&o->rebind?
        frontend_unified_q3_client_frame_rebind(o->rebind):NULL;
    return (!c->presentation || qa_q3_presentation_idle(c->presentation)) &&
        (!c->weapons || q3n_weapons_idle(c->weapons)) && (!c->events || q3n_events_idle(c->events)) &&
        (!c->particles || q3n_particles_idle(c->particles)) && (!c->view || q3n_view_idle(c->view)) &&
        (!c->player_state || q3n_player_state_idle(c->player_state)) && (!c->hud || q3n_hud_idle(c->hud)) &&
        (!c->commands || (t?q3n_server_commands_rebind_checkpoint_current(c->commands,t):q3n_server_commands_idle(c->commands))) &&
        (!c->loading || q3n_loading_idle(c->loading)) &&
        (!c->mission || (t?q3n_mission_hud_rebind_checkpoint_current(c->mission,t):q3n_mission_hud_idle(c->mission))) &&
        frontend_unified_q3_snapshots_idle(c->snapshots);
}
bool frontend_unified_q3_runtime_idle(const frontend_unified_q3_runtime *o)
{ return !o || (!o->busy && !o->entered && !o->command && !o->rebind && children_returned(o,false)); }
const frontend_unified_q3_client_frame *frontend_unified_q3_runtime_rebind_frame(const frontend_unified_q3_runtime *o)
{ return o?o->rebind:NULL; }
bool frontend_unified_q3_runtime_checkpoint_current(const frontend_unified_q3_runtime *o)
{
    return o && !o->busy && !o->entered && !o->command && !o->prepared && !o->video &&
        frontend_unified_q3_client_checkpoint_stage_current(o->options.client,o->rebind) && children_returned(o,true);
}
bool frontend_unified_q3_runtime_rebind_checkpoint_ready(const frontend_unified_q3_runtime *o,
    const frontend_unified_q3_client_frame *frame)
{ return o && frame && o->rebind==frame && frontend_unified_q3_runtime_checkpoint_current(o); }
const q3n_compiled_frame *frontend_unified_q3_runtime_entered(const frontend_unified_q3_runtime *o)
{ return o && o->busy ? o->entered : NULL; }
static bool cut(frontend_unified_q3_runtime *o,const q3n_frame *f,qa_error *e)
{ return f && f->compiled==o->entered && frontend_unified_q3_runtime_current(o) && q3n_frame_current(f) ? true :
    fail(e,"Unified CG callback left its actual Source/cache/resource receipt"); }
static bool begin(frontend_unified_q3_runtime *o,const q3n_compiled_frame *r,q3n_frame *f,qa_error *e)
{
    if(!frontend_unified_q3_runtime_current(o) || o->busy || !r ||
       r->source.owner!=frontend_unified_q3_client_source(o->options.client) || !q3n_compiled_frame_current(r))
        return fail(e,"Unified CG entry requires its actual returned compiled Source");
    o->busy=true; o->entered=r;
    bool okay=r->stage==Q3N_COMPILED_INITIALIZATION ||
        o->options.frame_settings(o->options.context,r,false,o->stereo,&o->settings,e);
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
static bool cvar_read(void *ctx,qa_native_q3_cvar_id id,qa_native_q3_client_cvar *out,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return frontend_unified_q3_client_cvar_read(o->options.client,id,out,e); }
static bool register_commands(void *ctx,const q3n_frame *f,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    frontend_unified_q3_client_register(o->options.client,e) &&
    cut(o,f,e); }
static bool console(void *ctx,const q3n_frame *f,const char *text,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    o->options.commands.compiled_console(o->options.commands.context,f,text,e) && cut(o,f,e); }
static bool center(void *ctx,const q3n_frame *f,const qa_command_context *c,const char *text,int32_t y,int32_t width,qa_error *e)
{
    frontend_unified_q3_runtime *o=ctx;
    if(!cut(o,f,e))return false;
    (void)c;
    const qa_native_q3_cvar_refs *refs=frontend_unified_q3_client_cvar_refs(o->options.client);
    const qa_cvar_view *duration=qa_cvars_read(frontend_unified_q3_client_cvars(o->options.client),refs->rows[QA_NATIVE_Q3_CVAR_cg_centertime]);
    double seconds=duration?fmax(0,fmin(86400,duration->number)):3;
    return qa_hud_center_print(o->options.hud.messages,text,
        (uint64_t)(uint32_t)f->time*UINT64_C(1000000),(uint64_t)(seconds*1e9),
        (qa_hud_center_policy){.instant=true,.source_layout=true,.y=y,.character_width=width,.fade_ns=UINT64_C(200000000)},e);
}
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
    if(which==Q3N_INIT_CONSOLE_COMMANDS)okay=o->options.scene_only ||
        o->options.commands.compiled_register(o->options.commands.context,f,e);
    else if(which==Q3N_INIT_CLIENT_LOADING)okay=physical>=0 && q3n_loading_client(o->children.loading,f,(uint32_t)physical,e);
    else if(which==Q3N_INIT_PARTICLES)okay=q3n_particles_load_compiled(o->children.particles,f,e);
    else if(which==Q3N_INIT_MISSION_ASSETS || which==Q3N_INIT_HUD_MENU || which==Q3N_INIT_TEAM_CHAT || which==Q3N_INIT_STRING_TABLE)
        okay=o->children.mission?q3n_mission_hud_initialize(o->children.mission,f,which,e):
            o->options.commands.initialize_stage(o->options.commands.context,f,which,map,physical,extent,e);
    else okay=o->options.commands.initialize_stage(o->options.commands.context,f,which,map,physical,extent,e);
    return okay && cut(o,f,e);
}
static bool clear_particles(void *ctx,const q3n_frame *f,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) && q3n_particles_load_compiled(o->children.particles,f,e) && cut(o,f,e); }
static bool score(void *ctx,const q3n_frame *f,const q3n_command_state *s,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx;
    if(o->options.scene_only)return fail(e,"Supplemental CG score selection requires an actual HUD menu owner");
    return o->children.mission && q3n_mission_hud_score_selection(o->children.mission,f,s,e); }
static bool response(void *ctx,const q3n_frame *f,const q3n_command_state *s,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx;
    if(o->options.scene_only)return fail(e,"Supplemental CG response head requires an actual HUD menu owner");
    return o->children.mission && q3n_mission_hud_response(o->children.mission,f,s,e); }
static int32_t memory_remaining(void *ctx)
{ frontend_unified_q3_runtime *o=ctx; return o->options.commands.memory_remaining(o->options.commands.context); }
static void event_print(void *ctx,const char *text)
{ frontend_unified_q3_runtime *o=ctx; o->options.events.print(o->options.events.context,text); }
static bool event_center(void *ctx,const q3n_frame *f,const char *text,int32_t y,int32_t width,qa_error *e)
{ return center(ctx,f,NULL,text,y,width,e); }
static bool event_voice(void *ctx,const q3n_frame *f,int32_t mode,bool only,int32_t client,int32_t color,const char *name,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    q3n_server_commands_voice(o->children.commands,f,mode,only,client,color,name,e) && cut(o,f,e); }
static bool event_trace(void *ctx,const q3n_frame *f,qa_vec3 start,qa_vec3 finish,qa_bounds bounds,int32_t skip,
    uint32_t mask,qa_trace_result *out,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    o->options.events.trace(o->options.events.context,f,start,finish,bounds,skip,mask,out,e) && cut(o,f,e); }
static bool event_contents(void *ctx,const q3n_frame *f,qa_vec3 point,int32_t pass,uint32_t *out,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    o->options.events.point_contents(o->options.events.context,f,point,pass,out,e) && cut(o,f,e); }
static bool event_marks(void *ctx,const q3n_frame *f,const qa_vec3 *points,size_t count,qa_vec3 projection,
    qa_vec3 *out,size_t capacity,q3n_mark_fragment *fragments,size_t fragment_capacity,size_t *returned,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    o->options.events.mark_fragments(o->options.events.context,f,points,count,projection,out,capacity,fragments,fragment_capacity,returned,e) && cut(o,f,e); }
static bool event_replace(void *ctx,const q3n_frame *f,q3n_entity *cent,const qa_q3_entity *scratch,
    qa_vec3 position,bool *suppressed,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    o->options.events.event_replacement(o->options.events.context,f,cent,scratch,position,suppressed,e) && cut(o,f,e); }
static void local_allocated(void *ctx,int32_t slot)
{ frontend_unified_q3_runtime *o=ctx; o->options.events.local_allocated(o->options.events.context,slot); }
static bool weapon_view(void *ctx,const q3n_frame *f,const qa_q3_player *ps,bool *consumed,qa_error *e)
{
    frontend_unified_q3_runtime *o=ctx;
    if(!cut(o,f,e) || !o->options.weapons.view_replacement(o->options.weapons.context,f,ps,consumed,e) ||
       !cut(o,f,e))return false;
    if(*consumed)o->view_weapon_replaced=true;
    return true;
}
static bool weapon_held(void *ctx,const q3n_frame *f,const qa_q3_entity *s,const qa_q3_ref_entity *torso,bool *suppressed,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    o->options.weapons.held_replacement(o->options.weapons.context,f,s,torso,suppressed,e) && cut(o,f,e); }
static bool weapon_particles(void *ctx,const q3n_frame *f,const char *animation,qa_vec3 origin,qa_vec3 velocity,
    int32_t duration,float start,float finish,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    q3n_particles_explosion(f,animation,origin,velocity,duration,start,finish,e) && cut(o,f,e); }
static int32_t hud_milliseconds(void *ctx)
{ frontend_unified_q3_runtime *o=ctx; return o->options.hud.milliseconds(o->options.hud.context); }
static bool hud_deferred(void *ctx,const q3n_frame *f,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; q3n_client_settings settings;
    return client_settings(o,f,false,&settings,e) &&
        q3n_clients_compiled_load_deferred(o->options.clients,&f->compiled->source,&settings,e) && cut(o,f,e); }
static bool hud_command(void *ctx,const q3n_frame *f,const char *text,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    o->options.hud.client_command(o->options.hud.context,f,text,e) && cut(o,f,e); }
static bool oldest_command(void *ctx,const q3n_frame *f,int32_t *time,bool *available,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    o->options.hud.compiled_oldest_command(o->options.hud.context,f,time,available,e) && cut(o,f,e); }
static bool hud_warning(void *ctx,const q3n_frame *f,q3n_weapon_hud *out,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    o->options.hud.weapon_warning(o->options.hud.context,f,out,e) && cut(o,f,e); }
static bool mission_paint(void *ctx,const q3n_frame *f,bool scoreboard,bool first,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) && q3n_mission_hud_paint(o->children.mission,f,scoreboard,first,e) && cut(o,f,e); }
static bool mission_order(void *ctx,const q3n_frame *f,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) && q3n_mission_hud_check_order(o->children.mission,f,e) && cut(o,f,e); }
static bool mission_timed(void *ctx,const q3n_frame *f,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) && q3n_mission_hud_timed(o->children.mission,f,e) && cut(o,f,e); }
static bool mission_text(void *ctx,const q3n_frame *f,const char *text,float y,float scale,const float color[4],
    int32_t style,bool half,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    q3n_mission_hud_text(o->children.mission,f,text,y,scale,color,style,half,e) && cut(o,f,e); }
static bool mission_center(void *ctx,const q3n_frame *f,const char *text,float y,const float color[4],float *height,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; return cut(o,f,e) &&
    q3n_mission_hud_center_line(o->children.mission,f,text,y,color,height,e) && cut(o,f,e); }
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
    bool okay=s && cvar_read(o,QA_NATIVE_Q3_CVAR_cg_showmiss,&miss,e);
    q3n_player_state_context settings={0};
    if(okay)settings=(q3n_player_state_context){s->warmup,s->timelimit,s->fraglimit,s->scores1,s->intermission_started,miss.integer!=0};
    return end(o,okay && q3n_player_state_transition_compiled(o->children.player_state,&f,p,before,&settings,e),e);
}
static bool lagometer(void *ctx,const qa_q3_snapshot *s,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; if(!parent_current(o))return fail(e,"Unified lagometer lost its physical CLIENT");
    q3n_hud_snapshot_sample(o->children.hud,s==NULL,s?s->player.ping:0,s?s->flags:0); return true; }
static bool prediction_finished(void *ctx,const q3n_compiled_frame *r,qa_error *e)
{
    frontend_unified_q3_runtime *o=ctx; q3n_frame f; if(!begin(o,r,&f,e))return false;
    qa_native_q3_client_cvar miss;
    return end(o,cvar_read(o,QA_NATIVE_Q3_CVAR_cg_showmiss,&miss,e) &&
        q3n_player_state_prediction_finish(o->children.player_state,&f,miss.integer!=0,e),e);
}
static bool transition_teleport(void *ctx,const q3n_compiled_frame *r,bool *out,qa_error *e)
{
    frontend_unified_q3_runtime *o=ctx;
    if(!out || o->busy || !frontend_unified_q3_runtime_current(o) || !r ||
        r->stage!=Q3N_COMPILED_PLAYER_TRANSITION ||
        r->source.owner!=frontend_unified_q3_client_source(o->options.client) || !q3n_compiled_frame_current(r))
        return fail(e,"Unified teleport feedback requires its returned player-state transition");
    return q3n_player_state_compiled_teleport_take(o->children.player_state,out,e) && q3n_compiled_frame_current(r);
}
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
    .reset_player=reset_player,.event=event,.transition_player=transition,.transition_teleport=transition_teleport,
    .prediction_finished=prediction_finished,
    .lagometer=lagometer,.warning=warning,
    .trace_number=trace_number,.draw_reason=draw_reason}; }
bool frontend_unified_q3_runtime_build_children(frontend_unified_q3_runtime *o,qa_error *e)
{
    q3n_compiled_source *source=frontend_unified_q3_client_source(o->options.client);
    q3n_compiled_source_view view;
    if(!q3n_compiled_source_checkpoint_read(source,&view,e))return false;
    frontend_unified_q3_runtime_owners *c=&o->children;
    c->media=o->options.media; c->clients=o->options.clients;
    q3n_weapon_options weapons=o->options.weapons;
    weapons.context=o; weapons.particle_explosion=weapon_particles;
    if(weapons.view_replacement)weapons.view_replacement=weapon_view;
    if(weapons.held_replacement)weapons.held_replacement=weapon_held;
    q3n_event_options events=o->options.events; events.compiled_source=source; events.context=o;
    events.print=event_print; events.center_print=event_center; events.trace=event_trace; events.point_contents=event_contents;
    events.mark_fragments=event_marks; events.weapon_event=q3n_weapons_event;
    if(events.local_allocated)events.local_allocated=local_allocated;
    if(events.event_replacement)events.event_replacement=event_replace;
    if(view.basis.product==QA_Q3_TEAM_ARENA)events.voice_chat=event_voice;
    q3n_view_options camera=o->options.view; camera.compiled_source=source;
    q3n_player_state_options player=o->options.player_state; player.compiled_source=source;
    q3n_hud_options hud=o->options.hud; hud.compiled_source=source; hud.context=o;
    hud.milliseconds=hud_milliseconds; hud.load_deferred=hud_deferred; hud.client_command=hud_command; hud.compiled_oldest_command=oldest_command;
    if(hud.weapon_warning)hud.weapon_warning=hud_warning;
    if(view.basis.product==QA_Q3_TEAM_ARENA) {
        hud.mission_paint=mission_paint; hud.mission_order=mission_order; hud.mission_timed=mission_timed;
        hud.mission_text=mission_text; hud.mission_center_line=mission_center;
    }
    q3n_loading_options load=o->options.loading; load.compiled_source=source;
    q3n_mission_hud_options mission=o->options.mission; mission.compiled_source=source;
    if(!qa_q3_presentation_create(&o->options.presentation,&c->presentation,e) ||
       !q3n_weapons_create(&weapons,&c->weapons,e) || !q3n_events_create_compiled(&events,&c->events,e) ||
       !q3n_particles_create_compiled(view.basis.assets,source,&c->particles,e) ||
       !q3n_view_create_compiled(&camera,&c->view,e) || !q3n_player_state_create_compiled(&player,&c->player_state,e) ||
       !q3n_hud_create_compiled(&hud,&c->hud,e))return false;
    q3n_server_command_options commands=o->options.commands;
    commands.compiled_scene_only=o->options.scene_only;
    const qa_command_context *actual_context=frontend_unified_q3_client_context(o->options.client);
    if(!actual_context)return fail(e,"Unified CG constructor lost its real CLIENT command namespace");
    commands.compiled_context=*actual_context; commands.publication_generation=view.basis.publication;
    commands.map_revision=view.basis.map_revision; mission.compiled_context=*actual_context;
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
    frontend_unified_q3_snapshots_options options=frontend_unified_q3_runtime_cache_options(o);
    bool okay=o->video_constructor?frontend_unified_q3_snapshots_create_video(&options,
        frontend_unified_q3_runtime_video_client(o->video),&c->snapshots,e):
        frontend_unified_q3_snapshots_create(&options,&c->snapshots,e);
    if(!okay)return false;
    o->complete=true; return true;
}
bool frontend_unified_q3_runtime_create(const frontend_unified_q3_runtime_options *options,frontend_unified_q3_runtime **out,qa_error *e)
{
    if(!out || *out || !options || !options->frontend || !options->replica || !options->client || !options->media ||
       !options->clients || !options->current || !options->frame_settings || !options->preferences ||
       !options->backend_frame || !options->trace_number || !options->command_values ||
       !options->timescale ||
       !options->player_fx.world_trace || !options->player_fx.world_point_contents ||
       !options->player_fx.body_hidden || !options->player_fx.body_submit || !options->player_fx.player_weapon ||
       !options->commands.compiled_current || !options->commands.compiled_register ||
       !options->commands.compiled_console || !options->commands.message || !options->commands.initialize_stage ||
       !options->commands.memory_remaining || !options->view.print || !options->events.print ||
       !options->events.trace || !options->events.point_contents || !options->events.mark_fragments ||
       !options->hud.milliseconds || !options->hud.client_command || !options->hud.compiled_oldest_command ||
       !frontend_unified_q3_client_current(options->client))
        return fail(e,"Unified CG requires its real factory and compiled CLIENT services");
    frontend_unified_q3_runtime *o=calloc(1,sizeof(*o));
    if(!o) { qa_error_set(e,QA_ERROR_MEMORY,0,"Retaining Unified CG runtime"); return false; }
    o->options=*options; *out=o;
    return frontend_unified_q3_runtime_build_children(o,e);
}
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
{ if(!o || !out || !o->complete || (!frontend_unified_q3_runtime_idle(o)&&!frontend_unified_q3_runtime_checkpoint_current(o)))return fail(e,"Unified CG owner read requires its returned children");
    *out=o->children; return true; }
static bool initialize(void *ctx,const q3n_compiled_frame *r,qa_error *e)
{ frontend_unified_q3_runtime *o=ctx; q3n_frame f; if(!begin(o,r,&f,e))return false;
    bool okay=o->video_constructor?q3n_server_commands_initialize_video(o->children.commands,&f,r->source.basis.reached_command,e):
        q3n_server_commands_initialize(o->children.commands,&f,r->source.basis.initial_command,e);
    return end(o,okay,e); }
bool frontend_unified_q3_runtime_initialize(frontend_unified_q3_runtime *o,qa_error *e)
{
    if(!frontend_unified_q3_runtime_current(o) || o->initialized || o->video || !frontend_unified_q3_runtime_idle(o))
        return fail(e,"Unified CG Init requires its real fresh constructor");
    if(!frontend_unified_q3_snapshots_initialize(o->children.snapshots,initialize,o,e) ||
        !frontend_source_renderer_end_registration(o->options.frontend,e))return false;
    o->initialized=true; return true; }
bool frontend_unified_q3_runtime_initialize_video(frontend_unified_q3_runtime *o,qa_error *e)
{
    if(!frontend_unified_q3_runtime_current(o) || o->initialized || !o->video_constructor || !o->video ||
       !frontend_unified_q3_runtime_video_current(o->video,e))return fail(e,"Unified video Init requires its actual retained CLIENT ticket");
    if(!frontend_unified_q3_snapshots_initialize(o->children.snapshots,initialize,o,e) ||
        !frontend_source_renderer_end_registration(o->options.frontend,e))return false;
    o->initialized=true; o->video_constructor=false; return true;
}
bool frontend_unified_q3_runtime_prepare(frontend_unified_q3_runtime *o,uint32_t stereo,qa_error *e)
{
    if(!frontend_unified_q3_runtime_current(o) || !o->initialized || o->prepared || o->video || stereo>2 ||
       !frontend_unified_q3_runtime_idle(o))return fail(e,"Unified CG draw requires its returned initialized owner");
    o->stereo=stereo; o->prepared=true; o->rendered=false; o->prediction_prepared=false;o->prediction_applied=false;
    bool okay=frontend_unified_q3_client_cvars_update(o->options.client,e) &&
        o->options.backend_frame(o->options.context,o->children.presentation,e);
    const char *text=q3n_loading_text(o->children.loading);
    if(okay && !text)okay=fail(e,"Unified CG lost its actual loading text owner");
    if(okay) { o->information_prepared=!o->options.scene_only && *text!=0;
        if(!o->information_prepared)okay=qa_q3_presentation_clear_loops(o->children.presentation,false,e) &&
            qa_q3_presentation_clear(o->children.presentation,e); }
    if(!okay)o->faulted=true;
    return okay;
}
bool frontend_unified_q3_runtime_process(frontend_unified_q3_runtime *o,int32_t time,bool *active,qa_error *e)
{
    if(!active || !frontend_unified_q3_runtime_current(o) || !o->prepared || o->prediction_prepared)
        return fail(e,"Unified CG snapshots require the actual prepared frame");
    o->presentation_time=time;
    if(o->information_prepared) { *active=false; return true; }
    qa_native_q3_client_cvar no_predict,synchronous;
    if(!cvar_read(o,QA_NATIVE_Q3_CVAR_cg_nopredict,&no_predict,e) || !cvar_read(o,QA_NATIVE_Q3_CVAR_cg_synchronousClients,&synchronous,e) ||
       !frontend_unified_q3_snapshots_process(o->children.snapshots,time,no_predict.integer!=0,synchronous.integer!=0,e))return false;
    q3n_compiled_frame frame; bool has_snapshot;
    if(!frontend_unified_q3_snapshots_has_snapshot(o->children.snapshots,&has_snapshot,e))return false;
    if(!has_snapshot) { *active=false; return true; }
    if(!frontend_unified_q3_snapshots_read(o->children.snapshots,&frame,e))return false;
    *active=!(frame.snapshot->flags&2);
    if(!*active)return true;
    if(o->options.scene_only) {
        if(!frontend_unified_q3_snapshots_scene_player(o->children.snapshots,e))return false;
        int32_t elapsed=subtract(time,o->old_time);o->frame_milliseconds=elapsed<0?0:elapsed;o->old_time=time;
        o->prediction_applied=true;
    } else {
        const q3n_view_state *view=q3n_view_read(o->children.view);
        const q3n_weapon_selection *weapon=q3n_weapons_selection(o->children.weapons);
        if(!o->options.command_values(o->options.context,weapon->weapon,view->zoom_sensitivity,e))return false;
    }
    uint32_t bits=(uint32_t)o->client_frame+1u; memcpy(&o->client_frame,&bits,4); o->prediction_prepared=true; return true;
}
bool frontend_unified_q3_runtime_prediction(frontend_unified_q3_runtime *o,const qa_q3_player *player,
    const frontend_unified_q3_prediction_receipt *receipt,
    qa_vec3 correction,int32_t time,bool hyperspace,qa_error *e)
{
    if(!receipt || !frontend_unified_q3_runtime_current(o) || o->options.scene_only || !o->prepared || !o->prediction_prepared)
        return fail(e,"Unified prediction requires its actual paired predictor and CG frame");
    q3n_compiled_frame frame; qa_native_q3_client_cvar no_predict,synchronous;
    if(!frontend_unified_q3_snapshots_read(o->children.snapshots,&frame,e) ||
       !cvar_read(o,QA_NATIVE_Q3_CVAR_cg_nopredict,&no_predict,e) || !cvar_read(o,QA_NATIVE_Q3_CVAR_cg_synchronousClients,&synchronous,e))return false;
    frontend_unified_q3_prediction_receipt actual=*receipt;
    if((frame.snapshot->player.pmFlags&4096) || no_predict.integer || synchronous.integer) {
        actual.outcome=FRONTEND_UNIFIED_Q3_INTERPOLATED;actual.teleport_consumed=false;
    }
    bool okay=frontend_unified_q3_snapshots_prediction(o->children.snapshots,player,&actual,correction,time,hyperspace,e);
    if(okay)o->prediction_applied=true;
    return okay;
}
bool frontend_unified_q3_runtime_prediction_baseline_read(const frontend_unified_q3_runtime *o,
    frontend_unified_q3_runtime_prediction_baseline *out,qa_error *e)
{
    if(!out || !frontend_unified_q3_runtime_current(o) || o->options.scene_only || !o->prepared || !o->prediction_prepared ||
       !frontend_unified_q3_runtime_idle(o))return fail(e,"Compiled prediction baseline requires its processed returned snapshot");
    q3n_compiled_frame r;
    if(!frontend_unified_q3_snapshots_read(o->children.snapshots,&r,e) || !r.snapshot || !r.predicted_player ||
       !q3n_compiled_frame_current(&r))return false;
    int32_t previous_command_time;
    if(!frontend_unified_q3_snapshots_previous_command_time(o->children.snapshots,&previous_command_time,e))return false;
    *out=(frontend_unified_q3_runtime_prediction_baseline){.player=r.snapshot->player,.viewer=r.source.basis.viewer,
        .message=r.snapshot->message_number,.previous_command_time=previous_command_time,
        .this_frame_teleport=r.this_frame_teleport,.correction=r.prediction_error,
        .correction_time=r.prediction_error_time,.hyperspace=r.hyperspace};return true;
}
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
{ frontend_unified_q3_runtime *o=ctx;
    return cut(o,f,e) && o->options.player_fx.player_weapon(o->options.player_fx.context,f,row,torso,team,e) && cut(o,f,e); }
static bool packet_player(void *ctx,const q3n_frame *f,const q3n_compiled_entity *row,q3n_entity *cent,qa_error *e)
{
    frontend_unified_q3_runtime *o=ctx;
    if(!cut(o,f,e) || !row || row->presentation!=cent || !q3n_compiled_entity_current(row))return false;
    int32_t client=row->current->clientNum;
    if(client<0 || client>=64)return fail(e,"Unified player has an invalid physical client-info index");
    const q3n_client_info *ci=q3n_clients_get(f->clients,(uint32_t)client);
    if(o->options.scene_only || !ci || !ci->info_valid)return true;
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
    if(!o->options.scene_only)
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
    if(!q3n_packet_compiled_predict(f,e) || !cut(o,f,e))return false;
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
        if(row.current_valid) {
            if(!q3n_packet_compiled_entity(f,&row,&o->settings.packet,&imports,e) || !cut(o,f,e))return false;
            if(row.number>=1022)return fail(e,"Compiled packet exceeds its physical Source entity extent");
            o->packet_handled[row.number]=true;o->packet_actors[row.number]=row.actor;
        }
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
bool frontend_unified_q3_runtime_camera_prepare(frontend_unified_q3_runtime *o,
    frontend_unified_q3_runtime_camera *out,bool *active,qa_error *e)
{
    if(!out || !active || !frontend_unified_q3_runtime_current(o) || !o->prepared || !o->initialized ||
       !frontend_unified_q3_runtime_idle(o))return fail(e,"Compiled camera requires its returned actual draw frame");
    if(o->options.scene_only || !o->prediction_prepared){*active=false;return true;}
    if(!o->prediction_applied)return fail(e,"Compiled camera has not consumed its actual prediction outcome");
    qa_q3_presentation_binding binding;
    if(!qa_q3_presentation_binding_read(o->children.presentation,&binding,e))return false;
    q3n_compiled_frame r;
    if(!frontend_unified_q3_snapshots_read(o->children.snapshots,&r,e))return false;
    if(!o->camera_prepared) {
        q3n_frame f;bool in_water=false;
        if(!begin(o,&r,&f,e))return false;
        bool okay=q3n_view_frame(o->children.view,&f,&o->settings.view,o->children.player_state,
            binding.options.viewport,&in_water,e);
        if(okay){memcpy(f.refdef.area_mask,r.snapshot->area_mask,sizeof(f.refdef.area_mask));
            o->refdef=f.refdef;o->view_angles=f.view_angles;o->scene_third_person=f.third_person;}
        if(!end(o,okay,e))return false;
        o->camera_prepared=true;
    }
    const qa_q3_player *snapshot=&r.snapshot->player;
    *out=(frontend_unified_q3_runtime_camera){.refdef=o->refdef,.viewport=binding.options.viewport,
        .near_clip=binding.options.near_clip,.far_clip=binding.options.far_clip};
    *active=!(snapshot->persistant[3]==3 && (snapshot->pmFlags&8192));return true;
}
bool frontend_unified_q3_runtime_scene_camera(frontend_unified_q3_runtime *o,const qa_scene_view *view,qa_error *e)
{
    if(!view || !frontend_unified_q3_runtime_current(o) || !o->options.scene_only || !o->prepared ||
       o->scene_prepared ||
       !frontend_unified_q3_runtime_idle(o) || view->viewport.width<=0 || view->viewport.height<=0 || view->viewport.width>INT32_MAX || view->viewport.height>INT32_MAX ||
       !qa_vec_finite(view->origin) || !qa_vec_finite(view->axis[0]) || !qa_vec_finite(view->axis[1]) ||
       !qa_vec_finite(view->axis[2]) || !isfinite(view->projection.m[0]) || view->projection.m[0]<=0 ||
       !isfinite(view->projection.m[5]) || view->projection.m[5]<=0)
        return fail(e,"Supplemental CG camera requires its actual completed common view projection");
    if(!o->prediction_prepared)return true;
    if(!o->prediction_applied)return fail(e,"Supplemental CG has not copied its actual snapshot player");
    q3n_compiled_frame r;
    if(!frontend_unified_q3_snapshots_read(o->children.snapshots,&r,e))return false;
    o->refdef.origin=view->origin;memcpy(o->refdef.axis,view->axis,sizeof(o->refdef.axis));
    o->refdef.x=view->viewport.x;o->refdef.y=view->viewport.y;
    o->refdef.width=(int32_t)view->viewport.width;o->refdef.height=(int32_t)view->viewport.height;
    o->refdef.fov_x=(float)(atan(1.0/(double)view->projection.m[0])*360.0/3.14159265358979323846);
    o->refdef.fov_y=(float)(atan(1.0/(double)view->projection.m[5])*360.0/3.14159265358979323846);
    o->refdef.time=r.time;memcpy(o->refdef.area_mask,r.snapshot->area_mask,sizeof(o->refdef.area_mask));
    o->camera_prepared=true;o->scene_third_person=false;return true;
}
static bool draw(frontend_unified_q3_runtime *o,bool gather,bool *rendered,qa_error *e)
{
    if(!rendered || !frontend_unified_q3_runtime_current(o) || !o->prepared || !o->initialized)
        return fail(e,"Unified draw requires its genuine prepared CG owner");
    *rendered=false;
    if(!o->prediction_prepared) {
        if(o->options.scene_only)return true;
        if(gather)return true;
        q3n_compiled_stage stage=o->information_prepared?Q3N_COMPILED_LOADING_INFORMATION:Q3N_COMPILED_AWAITING_SNAPSHOT;
        bool okay=frontend_unified_q3_snapshots_entered_draw(o->children.snapshots,stage,o->presentation_time,information_draw,o,e);
        if(okay)*rendered=o->rendered=true;
        return okay;
    }
    if(!o->prediction_applied)return fail(e,"Compiled draw has not consumed its actual prediction outcome");
    if(o->options.scene_only) {
        if(!gather || !o->camera_prepared)return fail(e,"Supplemental CG needs its actual common camera and scene admission");
    } else {
        frontend_unified_q3_runtime_camera prepared_camera;bool camera_active;
        if(!frontend_unified_q3_runtime_camera_prepare(o,&prepared_camera,&camera_active,e))return false;
    }
    q3n_compiled_frame r; q3n_frame f;
    if(!frontend_unified_q3_snapshots_read(o->children.snapshots,&r,e) || !begin(o,&r,&f,e))return false;
    qa_q3_presentation_binding binding;
    bool okay=qa_q3_presentation_binding_read(o->children.presentation,&binding,e);
    qa_scene_rect viewport=okay?binding.options.viewport:(qa_scene_rect){0};
    f.third_person=o->scene_third_person;
    if(okay)okay=required_media(o,&f,e);
    if(okay && !o->options.scene_only && !f.third_person)
        okay=q3n_view_damage_blob(o->children.view,&f,&o->settings.view,o->children.player_state,e);
    if(okay && !r.hyperspace)okay=packet(o,&f,e) && q3n_marks_submit(&f,e) && q3n_particles_add(&f,e) && q3n_local_submit(&f,e);
    const q3n_view_state *camera=q3n_view_read(o->children.view); const q3n_event_state *events=q3n_events_state(o->children.events);
    q3n_weapon_view weapon={.predicted_entity=r.predicted_entity,.predicted_state=r.predicted_state,.bob_cycle=camera->bob_cycle,
        .xy_speed=camera->xy_speed,.bob_fraction_sin=camera->bob_fraction_sin,.land_time=events->land_time,
        .land_change=events->land_change,.test_gun=camera->test_gun};
    const qa_q3_player *snapshot=q3n_frame_snapshot_player(&f);
    if(okay) {
        o->view_weapon_replaced=false;o->view_weapon_handled=false;
        okay=q3n_weapons_view(&f,&weapon,e);
        if(okay)o->view_weapon_handled=!o->view_weapon_replaced;
    }
    if(okay)okay=q3n_events_finish(&f,e) && q3n_server_commands_finish(o->children.commands,&f,e);
    if(okay && !o->options.scene_only)okay=q3n_view_test_submit(o->children.view,&f,&o->settings.view,e) &&
        powerup_audio(o,&f,e) && qa_q3_presentation_listener(f.presentation,snapshot->clientNum,f.refdef.origin,f.refdef.axis,e);
    if(okay && !o->options.scene_only && o->stereo!=2) {
        int32_t elapsed=subtract(f.time,o->old_time); o->frame_milliseconds=elapsed<0?0:elapsed; o->old_time=f.time;
        q3n_hud_frame_sample(o->children.hud,subtract(f.time,r.source.basis.time));
    }
    if(okay && !o->options.scene_only)okay=o->options.timescale(o->options.context,o->frame_milliseconds,e) && cut(o,&f,e);
    bool tournament=!o->options.scene_only && snapshot->persistant[3]==3 && (snapshot->pmFlags&8192);
    if(okay) { o->refdef=f.refdef; o->view_angles=f.view_angles; }
    if(gather) {
        if(okay)o->scene_third_person=f.third_person;
        bool returned=end(o,okay,e);
        if(returned)*rendered=!tournament;
        return returned;
    }
    if(okay && !tournament) {
        okay=q3n_hud_tile_clear(o->children.hud,&f,viewport,e); qa_q3_refdef render=f.refdef;
        float separation=o->stereo==0?0:o->settings.stereo_separation*(o->stereo==1?-0.5f:0.5f);
        float x=render.axis[1].x*-separation,y=render.axis[1].y*-separation,z=render.axis[1].z*-separation;
        render.origin.x+=x; render.origin.y+=y; render.origin.z+=z;
        if(okay)okay=qa_q3_presentation_render(f.presentation,&render,e) && cut(o,&f,e);
    }
    if(okay)okay=q3n_hud_frame(o->children.hud,&f,&o->settings.hud,o->children.commands,o->children.player_state,viewport,e);
    if(okay && !tournament) {
        qa_native_q3_client_cvar stats; okay=cvar_read(o,QA_NATIVE_Q3_CVAR_cg_stats,&stats,e);
        if(okay && stats.integer) {
            char text[64]; snprintf(text,sizeof(text),"cg.clientFrame:%d\n",o->client_frame);
            o->options.view.print(o->options.view.context,text); okay=cut(o,&f,e);
        }
    }
    if(okay)*rendered=o->rendered=true;
    return end(o,okay,e);
}
bool frontend_unified_q3_runtime_draw(frontend_unified_q3_runtime *o,bool *rendered,qa_error *e)
{ return draw(o,false,rendered,e); }
bool frontend_unified_q3_runtime_scene_prepare(frontend_unified_q3_runtime *o,
    qa_q3_source_scene_bank *bank,bool *active,qa_error *e)
{
    if(!o || !bank || !active || o->scene_prepared || o->supplement)
        return fail(e,"Compiled scene admission requires its fresh actual frame and shared bank");
    bool gathered=false;
    if(!draw(o,true,&gathered,e))return false;
    qa_q3_presentation_binding binding;
    if(gathered && (!qa_q3_presentation_binding_read(o->children.presentation,&binding,e) ||
       !qa_q3_presentation_supplement_prepare(o->children.presentation,bank,
           binding.frame,&o->supplement,e))){o->faulted=true;return false;}
    o->scene_bank=bank;o->scene_light_count=0;o->scene_submitted=false;
    if(gathered) {
        const qa_scene_light *lights=NULL;size_t count=0;qa_q3_source_scene_membership membership;
        if(!qa_q3_presentation_lights_read(o->children.presentation,&lights,&count,e) ||
           !qa_q3_source_scene_bank_membership(bank,&membership)){o->faulted=true;return false;}
        o->scene_light_first=membership.lights;
        for(size_t i=0;i<count;++i) {
            bool admitted=false;
            if(!qa_q3_source_scene_bank_light(bank,binding.options.assets,lights+i,&admitted,e)){
                o->faulted=true;return false;}
            if(admitted) {
                qa_q3_source_light_cell cell;
                if(o->scene_light_count==32 || !qa_q3_source_scene_bank_light_read(bank,
                    o->scene_light_first+(uint32_t)o->scene_light_count,&cell)){
                    o->faulted=true;return fail(e,"Compiled light admission left its actual shared bank span");}
                o->scene_lights[o->scene_light_count++]=cell.value;
            }
        }
    }
    o->scene_prepared=true;*active=gathered;return true;
}
bool frontend_unified_q3_runtime_scene_lights(const frontend_unified_q3_runtime *o,
    const qa_scene_light **out,size_t *count,qa_error *e)
{
    if(!out || !count || !frontend_unified_q3_runtime_current(o) || !o->prepared || !o->scene_prepared)
        return fail(e,"Compiled lights require their genuinely gathered scene");
    if(!o->supplement){*out=NULL;*count=0;return true;}
    qa_q3_presentation_binding binding;qa_q3_source_scene_membership membership;
    if(!qa_q3_presentation_binding_read(o->children.presentation,&binding,e) ||
       !qa_q3_presentation_supplement_current(o->supplement,binding.frame) ||
       !qa_q3_source_scene_bank_membership(o->scene_bank,&membership) ||
       o->scene_light_first>membership.lights || o->scene_light_count>membership.lights-o->scene_light_first)
        return fail(e,"Compiled lights left their actual admitted bank span");
    *out=o->scene_lights;*count=o->scene_light_count;return true;
}
bool frontend_unified_q3_runtime_scene_actor(const frontend_unified_q3_runtime *o,qa_actor_id actor,
    frontend_unified_q3_runtime_scene_owner *out,bool *owned,qa_error *e)
{
    if(!out || !owned || !frontend_unified_q3_runtime_current(o) || !o->prepared || !o->scene_prepared)
        return fail(e,"Compiled model ownership requires its genuinely admitted Source scene");
    *owned=false;
    if(!o->supplement)return true;
    qa_q3_presentation_binding binding;q3n_compiled_frame r;
    if(!qa_q3_presentation_binding_read(o->children.presentation,&binding,e) ||
       !qa_q3_presentation_supplement_current(o->supplement,binding.frame) ||
       !frontend_unified_q3_snapshots_read(o->children.snapshots,&r,e))return false;
    uint32_t number;bool present;
    if(!frontend_unified_q3_client_snapshot_number(o->options.client,r.snapshot->message_number,
        actor,&number,&present,e) || !q3n_compiled_frame_current(&r))return false;
    if(!present || number>=1022)return true;
    if(o->packet_handled[number] && qa_actor_id_equal(o->packet_actors[number],actor)) {
        q3n_compiled_entity row;
        if(!q3n_compiled_frame_entity(&r,number,&row,e) || !row.published || !row.current_valid ||
           !qa_actor_id_equal(row.actor,actor) || !q3n_compiled_entity_current(&row))
            return fail(e,"Compiled packet actor changed after its actual scene admission");
    }
    *out=(frontend_unified_q3_runtime_scene_owner){.provider=r.source.basis.provider,
        .instance=r.source.basis.instance,.publication=r.source.basis.publication,
        .map_revision=r.source.basis.map_revision,.actor=actor,.number=number,.message=r.snapshot->message_number};
    *owned=true;return true;
}
bool frontend_unified_q3_runtime_scene_view_weapon(const frontend_unified_q3_runtime *o,qa_actor_id actor,
    frontend_unified_q3_runtime_scene_owner *out,bool *owned,qa_error *e)
{
    if(!out || !owned || !frontend_unified_q3_runtime_current(o) || !o->prepared || !o->scene_prepared)
        return fail(e,"Compiled view weapon ownership requires its actual admitted scene");
    *owned=false;
    if(!o->supplement || !o->view_weapon_handled)return true;
    qa_q3_presentation_binding binding;q3n_compiled_frame r;q3n_compiled_entity predicted;
    if(!qa_q3_presentation_binding_read(o->children.presentation,&binding,e) ||
       !qa_q3_presentation_supplement_current(o->supplement,binding.frame) ||
       !frontend_unified_q3_snapshots_read(o->children.snapshots,&r,e) ||
       !q3n_compiled_frame_predicted(&r,&predicted,e))return false;
    if(!qa_actor_id_equal(predicted.actor,actor))return true;
    if(!q3n_compiled_entity_current(&predicted))
        return fail(e,"Compiled view weapon left its actual predicted Source actor");
    *out=(frontend_unified_q3_runtime_scene_owner){.provider=r.source.basis.provider,
        .instance=r.source.basis.instance,.publication=r.source.basis.publication,
        .map_revision=r.source.basis.map_revision,.actor=actor,.number=predicted.number,
        .message=r.snapshot->message_number};
    *owned=true;return true;
}
bool frontend_unified_q3_runtime_scene_submit(frontend_unified_q3_runtime *o,
    const qa_scene_world_input *world,qa_scene_frame *frame,qa_error *e)
{
    if(!world || !frame || !frontend_unified_q3_runtime_current(o) || !o->prepared || !o->scene_prepared)
        return fail(e,"Compiled supplement lost its actual common view");
    if(!o->supplement)return true;
    q3n_compiled_frame r;q3n_frame f;qa_q3_presentation_binding binding;
    if(!frontend_unified_q3_snapshots_read(o->children.snapshots,&r,e) || !begin(o,&r,&f,e))return false;
    bool okay=qa_q3_presentation_supplement_current(o->supplement,frame) &&
        qa_q3_presentation_binding_read(o->children.presentation,&binding,e) && binding.frame==frame;
    qa_q3_scene_options options={0};
    if(okay) {
        qa_collision_family family=qa_collision_geometry_family(binding.geometry);
        options=(qa_q3_scene_options){.world=*world,
            .world_family=family==QA_COLLISION_Q1?QA_GAME_Q1:family==QA_COLLISION_Q2?QA_GAME_Q2:QA_GAME_Q3,
            .shadow_mode=binding.options.shadow_mode,.lod_scale=binding.options.lod_scale,
            .lod_bias=binding.options.lod_bias,.near_clip=binding.options.near_clip,
            .ambient_scale=.6f,.directed_scale=1,
            .rail={.core_width=binding.options.rail_core_width,.ring_width=binding.options.rail_ring_width,
                .segment_length=binding.options.rail_segment_length}};
        options.world.milliseconds=o->refdef.time;options.world.seconds=(float)o->refdef.time*.001f;
        options.weapon_offset=qa_vec_sub(world->view.origin,o->refdef.origin);
        qa_scene_state_default(&options.state);
        if(binding.options.prepare_view)okay=binding.options.prepare_view(binding.options.context,&o->refdef,&options,e) && cut(o,&f,e);
        options.world.view=world->view;
        char texts[8][33];const char *rows[8];
        for(size_t i=0;i<8;++i){memcpy(texts[i],o->refdef.text[i],32);texts[i][32]=0;rows[i]=texts[i];}
        options.world.render_texts=rows;options.world.render_text_count=8;
        if(okay && !options.no_refresh)okay=qa_q3_presentation_supplement_draw(o->supplement,&options,frame,e) && cut(o,&f,e);
        if(okay)o->scene_submitted=true;
    }
    return end(o,okay,e);
}
bool frontend_unified_q3_runtime_hud(frontend_unified_q3_runtime *o,bool *rendered,qa_error *e)
{
    if(!rendered || !frontend_unified_q3_runtime_current(o) || !o->prepared || !o->scene_prepared || o->rendered)
        return fail(e,"Compiled HUD requires its genuine gathered frame after common view completion");
    if(o->options.scene_only){*rendered=false;o->rendered=true;return true;}
    if(!o->prediction_prepared)return draw(o,false,rendered,e);
    q3n_compiled_frame r;q3n_frame f;qa_q3_presentation_binding binding;
    if(!frontend_unified_q3_snapshots_read(o->children.snapshots,&r,e) || !begin(o,&r,&f,e))return false;
    f.third_person=o->scene_third_person;
    bool okay=qa_q3_presentation_binding_read(o->children.presentation,&binding,e) &&
        (!o->supplement || o->scene_submitted) && binding.frame && !binding.frame->group_count && !binding.frame->source_pending &&
        q3n_hud_frame(o->children.hud,&f,&o->settings.hud,o->children.commands,o->children.player_state,binding.options.viewport,e);
    const qa_q3_player *snapshot=q3n_frame_snapshot_player(&f);
    bool tournament=snapshot && snapshot->persistant[3]==3 && (snapshot->pmFlags&8192);
    if(okay && !tournament) {
        qa_native_q3_client_cvar stats;okay=cvar_read(o,QA_NATIVE_Q3_CVAR_cg_stats,&stats,e);
        if(okay && stats.integer){char text[64];snprintf(text,sizeof(text),"cg.clientFrame:%d\n",o->client_frame);
            o->options.view.print(o->options.view.context,text);okay=cut(o,&f,e);}
    }
    if(okay)*rendered=o->rendered=true;
    return end(o,okay,e);
}
bool frontend_unified_q3_runtime_frame_end(frontend_unified_q3_runtime *o,bool completed,qa_error *e)
{
    if(!o || !o->prepared || !frontend_unified_q3_runtime_idle(o))return fail(e,"Unified CG frame end still retains a child callback");
    qa_q3_presentation_supplement_release(&o->supplement);o->scene_prepared=false;o->scene_submitted=false;
    o->camera_prepared=false;
    o->view_weapon_handled=false;o->view_weapon_replaced=false;
    memset(o->packet_handled,0,sizeof(o->packet_handled));memset(o->packet_actors,0,sizeof(o->packet_actors));
    o->scene_bank=NULL;o->scene_light_count=0;
    o->prepared=false; o->prediction_prepared=false;o->prediction_applied=false; o->information_prepared=false;
    if(!completed)o->faulted=true;
    return true;
}
bool frontend_unified_q3_runtime_command_call(frontend_unified_q3_runtime *o,const q3n_compiled_frame *r,
    void *context,bool (*execute)(void *,const q3n_frame *,const frontend_unified_q3_runtime_owners *,qa_error *),qa_error *e)
{
    q3n_frame f;
    if(!o || !o->initialized || o->prepared || o->video || !r || r->stage!=Q3N_COMPILED_CONSOLE || !execute)
        return fail(e,"Unified console requires its actual returned runtime and entered CLIENT scope");
    if(!begin(o,r,&f,e))return false;
    bool okay=execute(context,&f,&o->children,e);
    if(okay)okay=cut(o,&f,e);
    o->entered=NULL; o->busy=false; return okay;
}
bool frontend_unified_q3_runtime_load_deferred(frontend_unified_q3_runtime *o,const q3n_frame *f,qa_error *e)
{ return o && hud_deferred(o,f,e); }
typedef struct unified_input_call {
    frontend_unified_q3_runtime *owner;
    int32_t kind,first,second;
    bool down;
} unified_input_call;
static bool input_command(void *ctx,const q3n_frame *f,const frontend_unified_q3_runtime_owners *children,qa_error *e)
{
    unified_input_call *call=ctx;
    if(!cut(call->owner,f,e))return false;
    if(q3n_frame_product(f)!=QA_Q3_TEAM_ARENA)return true;
    if(!children->mission)return fail(e,"Unified Team input lost its actual authored-menu owner");
    bool okay;
    switch(call->kind) {
    case 0:okay=q3n_mission_hud_key(children->mission,f,call->first,call->down,e);break;
    case 1:okay=q3n_mission_hud_mouse(children->mission,f,call->first,call->second,e);break;
    case 2:okay=q3n_mission_hud_event(children->mission,f,call->first,e);break;
    default:return fail(e,"Unknown Unified CG input operation");
    }
    return okay && cut(call->owner,f,e);
}
static bool input_scope(void *ctx,const q3n_compiled_frame *frame,qa_error *e)
{
    unified_input_call *call=ctx;
    return frontend_unified_q3_runtime_command_call(call->owner,frame,call,input_command,e);
}
static bool input_enter(unified_input_call *call,qa_error *e)
{
    frontend_unified_q3_runtime *o=call->owner;
    if(!frontend_unified_q3_runtime_current(o) || !o->initialized || o->prepared || o->video ||
        !frontend_unified_q3_runtime_idle(o))return fail(e,"Unified input requires its returned initialized CG owner");
    return frontend_unified_q3_snapshots_console(o->children.snapshots,input_scope,call,e);
}
bool frontend_unified_q3_runtime_key_event(frontend_unified_q3_runtime *o,int32_t key,bool down,qa_error *e)
{ unified_input_call call={.owner=o,.kind=0,.first=key,.down=down};return input_enter(&call,e); }
bool frontend_unified_q3_runtime_mouse_event(frontend_unified_q3_runtime *o,int32_t dx,int32_t dy,qa_error *e)
{ unified_input_call call={.owner=o,.kind=1,.first=dx,.second=dy};return input_enter(&call,e); }
bool frontend_unified_q3_runtime_event_handling(frontend_unified_q3_runtime *o,int32_t type,qa_error *e)
{ unified_input_call call={.owner=o,.kind=2,.first=type};return input_enter(&call,e); }
bool frontend_unified_q3_runtime_rebind_prepare(frontend_unified_q3_runtime *o,const frontend_unified_q3_client_frame *frame,qa_error *e)
{
    const q3n_compiled_source_rebind_ticket *ticket=frontend_unified_q3_client_frame_rebind(frame);
    const qa_command_context *context=frontend_unified_q3_client_frame_context(frame);
    if(!o || !frame || !o->complete || o->retiring || o->rebind || o->prepared ||
       !frontend_unified_q3_runtime_idle(o) || !context ||
       !frontend_unified_q3_client_frame_owned(o->options.client,frame) ||
       !frontend_unified_q3_client_ready(frame))
        return fail(e,"Unified CG round requires the genuine prepared CLIENT cohort");
    if(!ticket){o->rebind=frame;return true;}
    if(!q3n_server_commands_rebind_prepare(o->children.commands,ticket,context,e))return false;
    if(o->children.mission && !q3n_mission_hud_rebind_prepare(o->children.mission,ticket,context,e)) {
        q3n_server_commands_rebind_abort(o->children.commands,ticket); return false; }
    o->rebind=frame; return true;
}
bool frontend_unified_q3_runtime_rebind_ready(const frontend_unified_q3_runtime *o,const frontend_unified_q3_client_frame *f)
{ const q3n_compiled_source_rebind_ticket *t=frontend_unified_q3_client_frame_rebind(f);
    return o && f && o->rebind==f && frontend_unified_q3_client_frame_owned(o->options.client,f) &&
        frontend_unified_q3_client_ready(f) && (!t || (q3n_server_commands_rebind_ready(o->children.commands,t) &&
        (!o->children.mission || q3n_mission_hud_rebind_ready(o->children.mission,t)))); }
void frontend_unified_q3_runtime_rebind_commit(frontend_unified_q3_runtime *o,const frontend_unified_q3_client_frame *f)
{ if(!frontend_unified_q3_runtime_rebind_ready(o,f))return;
    const q3n_compiled_source_rebind_ticket *t=frontend_unified_q3_client_frame_rebind(f);
    q3n_server_commands_rebind_commit(o->children.commands,t);
    if(o->children.mission)q3n_mission_hud_rebind_commit(o->children.mission,t);
    o->rebind=NULL; }
void frontend_unified_q3_runtime_rebind_abort(frontend_unified_q3_runtime *o,const frontend_unified_q3_client_frame *f)
{ if(!o || o->rebind!=f)return; const q3n_compiled_source_rebind_ticket *t=frontend_unified_q3_client_frame_rebind(f);
    q3n_server_commands_rebind_abort(o->children.commands,t);
    if(o->children.mission)q3n_mission_hud_rebind_abort(o->children.mission,t);
    o->rebind=NULL; }
