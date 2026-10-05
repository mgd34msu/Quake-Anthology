#include "native_internal.h"
#include "local_entities.h"
#include "marks.h"
#include "trajectory.h"
#include "qa/game_q3_source.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

bool q3nn_fail(qa_error *error, qa_status status, const char *text)
{ qa_error_set(error,status,0,"%s",text); return false; }
static int32_t signed_word(uint32_t value)
{ int32_t out; memcpy(&out,&value,sizeof(out)); return out; }
static int32_t subtract(int32_t a,int32_t b)
{ return signed_word((uint32_t)a-(uint32_t)b); }
static int32_t increment(int32_t a)
{ return signed_word((uint32_t)a+1u); }
static qa_vec3 array_vector(const float v[3])
{ return qa_v3(v[0],v[1],v[2]); }

bool q3nn_children_idle(const q3n_native *o)
{
    return o && qa_native_q3_client_service_idle(o->options.client) &&
        qa_native_q3_wire_reader_idle(o->options.reader) &&
        qa_q3_presentation_idle(o->options.presentation) &&
        (!o->clients || q3n_clients_idle(o->clients)) && (!o->media || q3n_media_idle(o->media)) &&
        (!o->weapons || q3n_weapons_idle(o->weapons)) && (!o->events || q3n_events_idle(o->events)) &&
        (!o->particles || q3n_particles_idle(o->particles)) && (!o->view || q3n_view_idle(o->view)) &&
        (!o->player_state || q3n_player_state_idle(o->player_state)) &&
        (!o->hud || q3n_hud_idle(o->hud)) && (!o->commands || q3n_server_commands_idle(o->commands));
}
bool q3n_native_idle(const q3n_native *o)
{ return !o || (!o->busy && q3nn_children_idle(o)); }
bool q3n_native_retire_ready(const q3n_native *o,qa_error *e)
{ return q3n_native_idle(o) || q3nn_fail(e,QA_ERROR_ARGUMENT,"Native CGAME has an active frame or child callback"); }
void q3nn_free_children(q3n_native *o)
{
    q3n_server_commands_destroy(o->commands); q3n_hud_destroy(o->hud);
    q3n_player_state_destroy(o->player_state); q3n_view_destroy(o->view);
    q3n_particles_destroy(o->particles); q3n_events_destroy(o->events);
    q3n_weapons_destroy(o->weapons); q3n_media_destroy(o->media); q3n_clients_destroy(o->clients);
    qa_launch_instance_lease_release(o->source_lease);
    o->commands=NULL; o->hud=NULL; o->player_state=NULL; o->view=NULL; o->particles=NULL;
    o->events=NULL; o->weapons=NULL; o->media=NULL; o->clients=NULL; o->source_lease=NULL;
}
bool q3n_native_destroy(q3n_native *o,qa_error *e)
{
    if(!o)return true;
    if(!q3n_native_retire_ready(o,e) || !qa_native_q3_client_service_retire_ready(o->options.client,e))return false;
    q3nn_free_children(o);
    if(!qa_native_q3_client_service_destroy(o->options.client,e)) { o->faulted=true; return false; }
    free(o); return true;
}
static bool structure_current(const q3n_native *o);
bool q3n_native_video_close(q3n_native **slot,qa_error *e)
{
    if(!slot)return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native video close requires its retained core slot");
    q3n_native *o=*slot;
    if(!o)return true;
    if(!q3n_native_retire_ready(o,e) || !structure_current(o))return false;
    q3nn_free_children(o);
    free(o); *slot=NULL; return true;
}
static bool structure_current(const q3n_native *o)
{
    const qa_native_q3_client_services *services=o?qa_native_q3_client_services_read(o->options.client):NULL;
    return services && services->wire_reader==o->options.reader &&
        qa_native_q3_wire_reader_current(o->options.reader) &&
        services->client.session==o->session &&
        services->client.source_owner==o->source_owner && services->map_revision==o->map_revision &&
        services->client.seat==o->seat && services->client.source_client==o->physical_client &&
        qa_actor_id_equal(services->client.source_actor,o->viewing_actor);
}
bool q3n_native_current(const q3n_native *o)
{ return o && !o->faulted && structure_current(o); }
bool q3nn_source(q3n_native *o,qa_application_native_q3_presentation *source,qa_error *e)
{
    if(!q3n_native_current(o) || !qa_application_native_q3_presentation_read(o->options.application,o->source_owner,source,e))
        return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native CGAME lost its actual source or recipient");
    const qa_launch_instance *launch=qa_launch_instance_lease_view(o->source_lease);
    return source->source_game==o->source_game && source->session==o->session &&
        source->launch->storage==launch->storage && source->content==launch->content &&
        source->product==o->product && source->content_product==o->content_product && source->map_revision==o->map_revision ? true :
        q3nn_fail(e,QA_ERROR_ARGUMENT,"Native CGAME source storage or map has changed");
}
bool q3n_native_recipient(q3n_native *o,qa_application_q3_client_context *out,qa_error *e)
{
    return out && q3n_native_current(o) && qa_native_q3_client_context_read(o->options.client,out,e);
}
bool q3n_native_owners_read(const q3n_native *o,q3n_native_owners *out,qa_error *e)
{
    if(!o || !out || !o->source_lease || !o->clients || !q3n_native_current(o))return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native CGAME owner inventory has a superseded recipient");
    *out=(q3n_native_owners){.client=o->options.client,.reader=o->options.reader,
        .presentation=o->options.presentation,.assets=o->assets,
        .clients=o->clients,.media=o->media,.weapons=o->weapons,.events=o->events,.particles=o->particles,
        .view=o->view,.player_state=o->player_state,.hud=o->hud,.commands=o->commands,
        .source_launch=qa_launch_instance_lease_view(o->source_lease),
        .content=qa_launch_instance_lease_view(o->source_lease)->content,.source_owner=o->source_owner,
        .seat=o->seat,.physical_client=o->physical_client,.physical_presentation_seat=o->physical_presentation_seat,
        .viewing_actor=o->viewing_actor};
    return true;
}
static bool particle_explosion(void *context,const q3n_frame *f,const char *name,
    qa_vec3 origin,qa_vec3 velocity,int32_t duration,float first,float last,qa_error *e)
{ (void)context; return q3n_particles_explosion(f,name,origin,velocity,duration,first,last,e); }

bool q3nn_allocate(const q3n_native_options *options,bool restoring,q3n_native **out,qa_error *e)
{
    if(!options || !out || *out || !options->application || !options->client || !options->reader || !options->presentation || !options->frame_settings ||
        (options->begin_frame==NULL)!=(options->end_frame==NULL) ||
        ((options->before_render || options->camera_ready) && !options->begin_frame) ||
        !qa_native_q3_wire_reader_idle(options->reader) ||
        !qa_native_q3_client_service_idle(options->client) || (!restoring && !qa_q3_presentation_idle(options->presentation)))
        return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native CGAME requires its genuine idle client and backend");
    qa_q3_presentation_binding backend;
    if(!qa_q3_presentation_binding_read(options->presentation,&backend,e) ||
        backend.options.seat!=options->physical_presentation_seat)return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native CGAME backend has another physical presentation seat");
    const qa_native_q3_client_services *services=qa_native_q3_client_services_read(options->client);
    qa_native_q3_client_basis basis;
    qa_native_q3_wire_basis wire;
    qa_application_native_q3_presentation source={0};
    if(!services || services->wire_reader!=options->reader ||
        !qa_native_q3_client_basis_read(options->client,&basis,e) || basis.application!=options->application ||
        !qa_native_q3_wire_reader_basis(options->reader,&wire,e))return false;
    if(wire.application!=basis.application || wire.session!=basis.session || wire.source_game!=basis.source_game ||
        wire.source_owner!=basis.source_owner || wire.receiver!=basis.receiver || wire.product!=basis.product ||
        wire.seat!=basis.seat || wire.physical_client!=basis.physical_client ||
        wire.publication_generation!=basis.publication_generation || wire.map_revision!=basis.map_revision ||
        wire.source_cvars!=services->client.source_cvars || !qa_actor_id_equal(wire.actor,basis.viewing_actor))
        return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native CGAME reader differs from its actual installed client lease");
    if(!restoring && !qa_application_native_q3_presentation_read(options->application,basis.source_owner,&source,e))return false;
    q3n_native *o=calloc(1,sizeof(*o));
    if(!o)return q3nn_fail(e,QA_ERROR_MEMORY,"Allocating native CGAME");
    o->options=*options; o->options.view.source=NULL; o->options.player_state.source=NULL; o->options.hud.source=NULL;
    o->source_game=basis.source_game; o->session=basis.session; o->source_owner=basis.source_owner;
    o->content_product=basis.content_product; o->product=basis.product; o->map_revision=basis.map_revision;
    o->seat=services->client.seat; o->physical_client=services->client.source_client; o->viewing_actor=services->client.source_actor;
    o->physical_presentation_seat=options->physical_presentation_seat;
    o->assets=qa_q3_presentation_resources(options->presentation);
    q3n_client_options clients={.content=basis.content,.assets=o->assets,.product=o->product,.reader=options->reader,
        .context=options->events.context,.print=options->events.print};
    q3n_media_options media={.assets=o->assets,.product=o->product};
    q3n_weapon_options weapons=options->weapons;
    weapons.assets=o->assets; weapons.product=o->product; weapons.particle_explosion=particle_explosion;
    q3n_event_options events=options->events;
    events.assets=o->assets; events.product=o->product; events.weapon_event=q3n_weapons_event;
    q3n_view_options view=options->view;
    view.application=options->application; view.source=restoring?NULL:&source; view.client=options->client; view.assets=o->assets; view.seat=o->seat;
    q3n_player_state_options ps=options->player_state;
    ps.application=options->application; ps.source=restoring?NULL:&source; ps.client=options->client; ps.assets=o->assets; ps.seat=o->seat;
    q3n_hud_options hud=options->hud;
    hud.application=options->application; hud.source=restoring?NULL:&source; hud.client=options->client; hud.assets=o->assets; hud.seat=o->seat;
    hud.presentation_seat=o->physical_presentation_seat;
    bool ok=qa_launch_instance_retain_metadata(basis.source_launch,&o->source_lease,e) &&
        q3n_clients_create(&clients,&o->clients,e) && q3n_media_create(&media,&o->media,e) &&
        q3n_weapons_create(&weapons,&o->weapons,e) && q3n_events_create(&events,&o->events,e) &&
        q3n_particles_create(o->assets,o->product,&o->particles,e) &&
        (restoring?q3n_view_create_restored(&view,&o->view,e):q3n_view_create(&view,&o->view,e)) &&
        (restoring?q3n_player_state_create_restored(&ps,&o->player_state,e):q3n_player_state_create(&ps,&o->player_state,e)) &&
        (restoring?q3n_hud_create_restored(&hud,&o->hud,e):q3n_hud_create(&hud,&o->hud,e));
    q3n_server_command_options commands=options->commands;
    commands.application=options->application; commands.client=options->client; commands.reader=options->reader; commands.recipient=services->client;
    commands.publication_generation=basis.publication_generation; commands.map_revision=basis.map_revision;
    commands.product=o->product; commands.content=basis.content; commands.assets=o->assets;
    commands.presentation=options->presentation; commands.clients=o->clients; commands.media=o->media; commands.events=o->events;
    if(ok)ok=q3n_server_commands_create(&commands,&o->commands,e);
    if(!ok) { q3nn_free_children(o); free(o); return false; }
    *out=o; return true;
}
bool q3n_native_create(const q3n_native_options *options,q3n_native **out,qa_error *e)
{ return q3nn_allocate(options,false,out,e); }

static q3n_frame frame_base(q3n_native *o,const qa_application_native_q3_presentation *source)
{
    return (q3n_frame){.application=o->options.application,.source=*source,.presentation=o->options.presentation,
        .assets=o->assets,.clients=o->clients,.media=o->media,.weapons=o->weapons,.events=o->events,
        .particles=o->particles,.view=o->view,.player_state=o->player_state,.server_commands=o->commands,
        .client_service=o->options.client,.reader=o->options.reader,.entities=o->entities,.seat=o->seat,.viewing_client=o->physical_client,
        .physical_presentation_seat=o->physical_presentation_seat,
        .viewing_actor=o->viewing_actor,.time=source->source_time_ms,.frame_milliseconds=o->frame_milliseconds,
        .client_frame=o->client_frame,.refdef=o->previous_refdef,.view_angles=o->previous_view_angles};
}
bool q3n_native_command_frame(q3n_native *o,q3n_frame *out,qa_error *e)
{
    qa_application_native_q3_presentation source; uint32_t physical; qa_actor_id actor;
    qa_q3_player player; bool found;
    if(!o || !out || !o->initialized || !q3n_native_idle(o) || !q3nn_source(o,&source,e) ||
        !qa_application_native_q3_presentation_local(o->options.application,&source,o->seat,
            &physical,&actor,&player,&found,e) || !found || physical!=o->physical_client ||
        !qa_actor_id_equal(actor,o->viewing_actor))
        return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native console requires its idle completed source and actual local player");
    *out=frame_base(o,&source); out->local_player=player; out->has_local_player=true;
    return true;
}
static bool initialize(q3n_native *o,int32_t baseline,bool video,qa_error *e)
{
    if(!o || o->initialized || !q3n_native_idle(o))return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native CGAME Init requires an unused idle constructor");
    qa_application_native_q3_presentation source;
    if(!q3nn_source(o,&source,e))return false;
    o->busy=true;
    q3n_frame f=frame_base(o,&source);
    bool ok=qa_native_q3_client_prepare(o->options.client,e) &&
        (video?q3n_server_commands_initialize_video(o->commands,&f,baseline,e):
            q3n_server_commands_initialize(o->commands,&f,baseline,e)) &&
        qa_native_q3_client_initialized(o->options.client,e);
    if(ok)o->initialized=true; else o->faulted=true;
    o->busy=false; return ok;
}
bool q3n_native_initialize(q3n_native *o,int32_t baseline,qa_error *e)
{ return initialize(o,baseline,false,e); }
bool q3n_native_initialize_video(q3n_native *o,int32_t baseline,qa_error *e)
{ return initialize(o,baseline,true,e); }
bool q3n_native_reload_client(q3n_native *o,uint32_t physical,const q3n_client_settings *settings,qa_error *e)
{
    qa_application_native_q3_presentation source;
    if(!o || !settings || o->faulted || !q3nn_source(o,&source,e))return false;
    return q3n_clients_register_one(o->clients,o->options.application,&source,settings,physical,e);
}
bool q3nn_entity(q3n_native *o,const q3n_frame *f,uint32_t physical,
    qa_application_native_q3_entity *actual,q3n_entity **out,qa_error *e)
{
    if(physical>=QA_Q3_SOURCE_ENTITIES || !qa_application_native_q3_presentation_entity(f->application,&f->source,physical,actual,e))return false;
    if(!actual->present || actual->binding.number!=(int32_t)physical || actual->state.number!=(int32_t)physical)
        return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native centity lacks its physical source binding");
    q3n_entity *cent=&o->entities[physical];
    bool generation=!qa_actor_id_equal(cent->actor,actual->binding.actor);
    bool teleport=((uint32_t)actual->state.eFlags&4u)!=0;
    bool reset=generation || !cent->valid || cent->teleport_bit!=teleport;
    if(generation) { memset(cent,0,sizeof(*cent)); cent->actor=actual->binding.actor; cent->physical=physical; }
    if(reset) {
        if(cent->snapshot_time<subtract(f->time,300))cent->previous_event=0;
        cent->trail_time=f->time; cent->lerp_origin=array_vector(actual->state.origin); cent->lerp_angles=array_vector(actual->state.angles);
        if(actual->state.eType==1)q3n_player_reset(&cent->player,cent->lerp_angles);
    }
    cent->valid=true; cent->teleport_bit=teleport;
    if(actual->state.eType==1 && actual->state.clientNum>=0 && actual->state.clientNum<64) {
        const q3n_client_info *ci=q3n_clients_get(o->clients,(uint32_t)actual->state.clientNum);
        if(ci && ci->media_revision!=cent->client_media_revision) {
            q3n_player_reset(&cent->player,array_vector(actual->state.angles)); cent->client_media_revision=ci->media_revision;
        }
    }
    o->seen[physical]=true; *out=cent; return true;
}
static int32_t packet_rand(void *context)
{ return q3n_events_rand(((q3n_native *)context)->events); }
static bool packet_current(const q3n_frame *f,const qa_application_native_q3_entity *saved,const q3n_entity *cent,qa_error *e)
{
    qa_application_native_q3_entity actual;
    return cent->valid && qa_application_native_q3_presentation_entity(f->application,&f->source,cent->physical,&actual,e) &&
        actual.present && actual.binding.number==(int32_t)cent->physical &&
        qa_actor_id_equal(actual.binding.actor,saved->binding.actor) && qa_actor_id_equal(actual.binding.actor,cent->actor) ? true :
        q3nn_fail(e,QA_ERROR_ARGUMENT,"Native packet submission left its physical actor generation");
}
static bool packet_body(void *context,const q3n_frame *f,const qa_application_native_q3_entity *actual,
    q3n_entity *cent,const qa_q3_ref_entity *ref,qa_error *e)
{
    q3n_native *o=context; bool consumed=false;
    if(!packet_current(f,actual,cent,e))return false;
    if(o->options.packet_body && (!o->options.packet_body(o->options.packet_context,f,actual,cent,ref,&consumed,e) ||
        !packet_current(f,actual,cent,e)))return false;
    return consumed || (qa_q3_presentation_entity(f->presentation,ref,e) && packet_current(f,actual,cent,e));
}
static bool player_weapon(void *context,const q3n_frame *f,const qa_application_native_q3_entity *actual,
    q3n_entity *cent,const qa_q3_ref_entity *torso,int32_t team,qa_error *e)
{
    (void)context; (void)team; return q3n_weapons_player(f,torso,NULL,cent,&actual->state,e);
}
static bool packet_player(void *context,const q3n_frame *f,const qa_application_native_q3_entity *actual,q3n_entity *cent,qa_error *e)
{
    q3n_native *o=context; const q3n_native_frame_options *s=o->frame_options;
    if(actual->state.clientNum<0 || actual->state.clientNum>=64)return q3nn_fail(e,QA_ERROR_FORMAT,"Source player has an invalid client-info index");
    const q3n_client_info *ci=q3n_clients_get(f->clients,(uint32_t)actual->state.clientNum);
    if(!ci || !ci->info_valid)return true;
    q3n_body_options body_options={.time=f->time,.frame_milliseconds=f->frame_milliseconds,
        .local_view_client=f->local_player.clientNum,.shadow_mode=s->player_fx.shadow_mode,.swing_speed=s->swing_speed,
        .no_player_animations=s->no_player_animations,.animations_disabled=s->animations_disabled,
        .third_person=f->third_person,.camera_mode=s->view.camera_mode};
    q3n_player_body body;
    if(!q3n_player_body_build(f->assets,&cent->player,ci,&actual->state,cent->lerp_origin,cent->lerp_angles,&body_options,&body,e))return false;
    q3n_player_fx_backend backend=o->options.player_fx;
    backend.player_weapon=player_weapon;
    return q3n_player_fx_submit(f,actual,cent,ci,&body,&s->player_fx,&backend,e);
}
static bool packet_trail(void *context,const q3n_frame *f,const qa_application_native_q3_entity *actual,
    q3n_entity *cent,const q3n_weapon_media *media,bool grapple,qa_error *e)
{
    (void)context; (void)media; (void)grapple; return q3n_weapons_trail(f,cent,&actual->state,e);
}
static bool packet_powerups(void *context,const q3n_frame *f,const qa_application_native_q3_entity *actual,
    q3n_entity *cent,const qa_q3_ref_entity *base,int32_t team,qa_error *e)
{
    const q3n_media_view *media=q3n_media_read(f->media); qa_q3_ref_entity ref=*base;
    uint32_t powers=(uint32_t)actual->state.powerups;
    if(powers&(1u<<4)) { ref.custom_shader=media->graphics[Q3N_G_INVIS]; return packet_body(context,f,actual,cent,&ref,e); }
    if(!packet_body(context,f,actual,cent,&ref,e))return false;
    if(powers&(1u<<1)) { ref.custom_shader=media->graphics[team==1?Q3N_G_RED_QUAD:Q3N_G_QUAD]; if(!packet_body(context,f,actual,cent,&ref,e))return false; }
    if((powers&(1u<<5)) && (f->time/100)%10==1) { ref.custom_shader=media->graphics[Q3N_G_REGEN]; if(!packet_body(context,f,actual,cent,&ref,e))return false; }
    if(powers&(1u<<2)) { ref.custom_shader=media->graphics[Q3N_G_BATTLE_SUIT]; if(!packet_body(context,f,actual,cent,&ref,e))return false; }
    return true;
}

static bool required_weapon(q3n_native *o,int32_t number,bool registered[16],qa_error *e)
{
    if(number<0 || number>=16)return q3nn_fail(e,QA_ERROR_FORMAT,"Native frame weapon index is outside its actual registry");
    if(registered[number])return true;
    if(!q3n_media_register_weapon(o->media,(uint32_t)number,e))return false;
    registered[number]=true; return q3n_native_current(o);
}
static bool required_media(q3n_native *o,const q3n_frame *f,const qa_q3_visible_entities *visible,qa_error *e)
{
    bool registered[16]={0}; int32_t extent=o->product==QA_Q3_TEAM_ARENA?14:11;
    if(!required_weapon(o,f->local_player.weapon,registered,e))return false;
    unsigned stat=o->product==QA_Q3_TEAM_ARENA?3u:2u;
    uint32_t owned=(uint32_t)f->local_player.stats[stat];
    for(int32_t i=1;i<extent;++i)if((owned&(1u<<(unsigned)i)) && !required_weapon(o,i,registered,e))return false;
    for(size_t i=0;i<visible->count;++i) {
        qa_application_native_q3_entity actual;
        uint32_t physical=(uint32_t)visible->entities[i].number;
        if(!qa_application_native_q3_presentation_entity(f->application,&f->source,physical,&actual,e))return false;
        int32_t type=actual.state.eType,weapon=actual.state.weapon;
        if(type==1 || type==3 || type==11) {
            if(type!=1 && weapon>extent)weapon=0;
            if(!required_weapon(o,weapon,registered,e))return false;
        }
    }
    return true;
}
static bool powerup_audio(q3n_native *o,const q3n_frame *f,qa_error *e)
{
    int32_t sound=q3n_media_read(o->media)->sounds[Q3N_S_WEAR_OFF];
    for(unsigned i=0;i<16;++i) {
        int32_t expiry=f->local_player.powerups[i];
        if(expiry<=f->time)continue;
        int32_t remaining=subtract(expiry,f->time),previous=subtract(expiry,o->old_time);
        if(remaining<5000 && remaining/1000!=previous/1000 &&
            (!qa_q3_presentation_sound(f->presentation,sound,NULL,f->local_player.clientNum,4,false,e) ||
             !qa_application_native_q3_presentation_current(f->application,&f->source)))return false;
    }
    return true;
}
static bool timescale(q3n_native *o,qa_error *e)
{
    qa_native_q3_client_cvar end,speed,current;
    qa_native_q3_client_service *client=o->options.client;
    if(!qa_native_q3_client_cvar_read(client,"cg_timescaleFadeEnd",&end,e) ||
        !qa_native_q3_client_cvar_read(client,"cg_timescaleFadeSpeed",&speed,e) ||
        !qa_native_q3_client_cvar_read(client,"cg_timescale",&current,e))return false;
    if(current.number==end.number)return true;
    float delta=(speed.number * (float)o->frame_milliseconds)/1000;
    float value=current.number<end.number?fminf(end.number,(current.number + delta)):fmaxf(end.number,(current.number + -delta));
    return qa_native_q3_client_cvar_number(client,"cg_timescale",value,e) &&
        (speed.number==0 || qa_native_q3_client_set_timescale(client,value,e));
}
static bool draw(q3n_native *o,int32_t latest,bool *rendered,bool *begun,
    q3n_frame *f,q3n_native_frame_options *settings,qa_error *e)
{
    if(!qa_native_q3_client_refresh(o->options.client,e) || !qa_native_q3_client_update(o->options.client,e))return false;
    qa_application_native_q3_presentation source;
    if(!q3nn_source(o,&source,e))return false;
    if(!o->options.frame_settings(o->options.settings_context,o,&source,settings,e) ||
        !qa_application_native_q3_presentation_current(o->options.application,&source) || settings->stereo>2 ||
        !isfinite(settings->stereo_separation))return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native frame requires its current cached settings projection");
    o->frame_options=settings;
    *f=frame_base(o,&source);
    f->weapon_settings=&settings->weapons; f->event_settings=&settings->events;
    if(!qa_ui_preferences_read(qa_application_cvars(o->options.application),o->physical_presentation_seat,&f->preferences,e))return false;
    qa_application_native_q3_view local; bool found;
    if(!qa_application_native_q3_presentation_visible(f->application,&source,o->seat,&local,&found,e))return false;
    if(!found)return true;
    if(local.physical_client!=o->physical_client || !qa_actor_id_equal(local.actor,o->viewing_actor))
        return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native visible frame differs from its actual installed recipient");
    f->local_player=local.player; f->has_local_player=true;
    if(f->local_player.clientNum<0 || (uint32_t)f->local_player.clientNum>=source.max_clients)
        return q3nn_fail(e,QA_ERROR_FORMAT,"Native followed player is outside its physical client extent");
    qa_q3_presentation_binding backend;
    if(!qa_q3_presentation_binding_read(f->presentation,&backend,e) ||
        backend.options.seat!=o->physical_presentation_seat)return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native draw backend has another physical presentation seat");
    if(!qa_q3_presentation_clear_loops(f->presentation,false,e) || !qa_q3_presentation_clear(f->presentation,e) ||
        !q3n_server_commands_execute(o->commands,f,latest,e))return false;
    const q3n_command_state *commands=q3n_server_commands_state(o->commands);
    if(!commands)return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native frame command owner is still active");
    settings->view.dm_flags=commands->dm_flags;
    if(commands->map_restart) {
        q3n_player_state_round(o->player_state);
        if(!q3n_server_commands_map_restart_taken(o->commands,f,e))return false;
    }
    if(o->options.begin_frame) {
        *begun=true;
        if(!o->options.begin_frame(o->options.frame_context,f,e) ||
            !qa_application_native_q3_presentation_current(f->application,&source))return false;
    }
    bool fresh=!o->has_source_frame || o->source_frame_number!=source.source_frame.number;
    memset(o->seen,0,sizeof(o->seen));
    if(fresh)for(unsigned i=0;i<QA_Q3_SOURCE_ENTITIES;++i)o->entities[i].loop_stopped=false;
    qa_application_native_q3_entity followed; q3n_entity *predicted;
    if(!q3nn_entity(o,f,(uint32_t)f->local_player.clientNum,&followed,&predicted,e) ||
        !q3n_trajectory(&followed.state.pos,f->time,&predicted->lerp_origin,e) ||
        !q3n_trajectory(&followed.state.apos,f->time,&predicted->lerp_angles,e))return false;
    if(fresh) {
        for(size_t i=0;i<local.visible.count;++i) {
            uint32_t physical=(uint32_t)local.visible.entities[i].number;
            qa_application_native_q3_entity actual; q3n_entity *cent;
            if(!q3nn_entity(o,f,physical,&actual,&cent,e))return false;
            if(physical!=(uint32_t)f->local_player.clientNum && !q3n_events_apply(f,&actual,cent,e))return false;
            cent->snapshot_time=f->time;
        }
        for(unsigned i=0;i<QA_Q3_SOURCE_ENTITIES;++i)if(!o->seen[i])o->entities[i].valid=false;
        o->source_frame_number=source.source_frame.number; o->has_source_frame=true;
    }
    const q3n_weapon_selection *selection=q3n_weapons_selection(o->weapons);
    const q3n_view_state *camera=q3n_view_read(o->view);
    if(!selection || !camera || !qa_native_q3_client_command_values(o->options.client,selection->weapon,camera->zoom_sensitivity,e))return false;
    o->client_frame=increment(o->client_frame); f->client_frame=o->client_frame;
    q3n_player_state_context transition={.warmup=commands->warmup,.timelimit=commands->timelimit,
        .fraglimit=commands->fraglimit,.scores1=commands->scores1,.intermission_started=commands->intermission_started};
    qa_native_q3_client_cvar show_miss;
    if(!qa_native_q3_client_cvar_read(o->options.client,"cg_showmiss",&show_miss,e))return false;
    transition.show_miss=show_miss.integer!=0;
    bool in_water;
    if(!q3n_player_state_transition(o->player_state,f,&transition,e) ||
        !q3n_view_frame(o->view,f,&settings->view,o->player_state,backend.options.viewport,&in_water,e) ||
        !required_media(o,f,&local.visible,e))return false;
    memcpy(f->refdef.area_mask,local.visible.area_mask,sizeof(f->refdef.area_mask));
    if(o->options.camera_ready && (!o->options.camera_ready(o->options.frame_context,f,e) ||
        !qa_application_native_q3_presentation_current(f->application,&source)))return false;
    if(!f->third_person && !q3n_view_damage_blob(o->view,f,&settings->view,o->player_state,e))return false;
    q3n_packet_imports imports={.context=o,.rand=packet_rand,.body=packet_body,.player=packet_player,
        .trail=packet_trail,.powerups=packet_powerups};
    if(!camera->hyperspace) {
        /* The local body uses its actual GAME S. No BG conversion or predicted
         * simulation is introduced by this completed-cut renderer. */
        if(!q3n_packet_entity(f,&followed,predicted,&settings->packet,&imports,e))return false;
        for(size_t i=0;i<local.visible.count;++i) {
            uint32_t physical=(uint32_t)local.visible.entities[i].number;
            if(physical==(uint32_t)f->local_player.clientNum)continue;
            qa_application_native_q3_entity actual; q3n_entity *cent;
            if(!q3nn_entity(o,f,physical,&actual,&cent,e) || !q3n_packet_entity(f,&actual,cent,&settings->packet,&imports,e))return false;
        }
        if(!q3n_marks_submit(f,e) || !q3n_particles_add(f,e) || !q3n_local_submit(f,e))return false;
    }
    const q3n_event_state *events=q3n_events_state(o->events);
    q3n_weapon_view weapon_view={.predicted_entity=predicted,.predicted_state=&followed.state,
        .bob_cycle=camera->bob_cycle,.xy_speed=camera->xy_speed,.bob_fraction_sin=camera->bob_fraction_sin,
        .land_time=events->land_time,.land_change=events->land_change,.test_gun=camera->test_gun};
    if(!q3n_weapons_view(f,&weapon_view,e) || !q3n_events_finish(f,e) ||
        !q3n_server_commands_finish(o->commands,f,e) || !q3n_view_test_submit(o->view,f,&settings->view,e) ||
        !powerup_audio(o,f,e) || !qa_q3_presentation_listener(f->presentation,f->local_player.clientNum,f->refdef.origin,f->refdef.axis,e))return false;
    (void)in_water;
    if(settings->stereo!=2) {
        int32_t milliseconds=subtract(f->time,o->old_time);
        o->frame_milliseconds=milliseconds<0?0:milliseconds; o->old_time=f->time;
        q3n_hud_frame_sample(o->hud,0);
    }
    if(!timescale(o,e))return false;
    if(o->options.before_render &&
        (!o->options.before_render(o->options.frame_context,f,e) ||
         !qa_application_native_q3_presentation_current(f->application,&source)))return false;
    o->previous_refdef=f->refdef;
    o->previous_view_angles=f->view_angles;
    bool tournament=f->local_player.persistant[3]==3 && (f->local_player.pmFlags&8192);
    if(!tournament) {
        if(!q3n_hud_tile_clear(o->hud,f,backend.options.viewport,e))return false;
        qa_q3_refdef render=f->refdef;
        float separation=settings->stereo==0?0:(settings->stereo_separation * (settings->stereo==1?-0.5f:0.5f));
        render.origin.x=(render.origin.x + (render.axis[1].x * -separation));
        render.origin.y=(render.origin.y + (render.axis[1].y * -separation));
        render.origin.z=(render.origin.z + (render.axis[1].z * -separation));
        if(!qa_q3_presentation_render(f->presentation,&render,e))return false;
    }
    if(!q3n_hud_frame(o->hud,f,&settings->hud,o->commands,o->player_state,backend.options.viewport,e) ||
        !qa_application_native_q3_presentation_current(f->application,&source))return false;
    if(!tournament) {
        qa_native_q3_client_cvar stats;
        if(!qa_native_q3_client_cvar_read(o->options.client,"cg_stats",&stats,e))return false;
        if(stats.integer) {
            if(!o->options.events.print)return q3nn_fail(e,QA_ERROR_UNSUPPORTED,"Native cg_stats requires its actual print service");
            char message[64]; snprintf(message,sizeof(message),"cg.clientFrame:%d\n",o->client_frame);
            o->options.events.print(o->options.events.context,message);
            if(!qa_application_native_q3_presentation_current(f->application,&source))return false;
        }
    }
    *rendered=true; return true;
}
bool q3n_native_draw(q3n_native *o,int32_t latest,bool *rendered,qa_error *e)
{
    if(!o || !rendered || !o->initialized || !q3n_native_idle(o) || !q3n_native_current(o))
        return q3nn_fail(e,QA_ERROR_ARGUMENT,"Native draw requires its actual initialized idle recipient");
    *rendered=false; o->busy=true;
    bool begun=false;
    q3n_frame frame={0}; q3n_native_frame_options settings={0};
    bool ok=draw(o,latest,rendered,&begun,&frame,&settings,e);
    if(begun)o->options.end_frame(o->options.frame_context);
    o->frame_options=NULL; o->busy=false;
    if(!ok)o->faulted=true;
    return ok;
}
bool q3n_native_round(q3n_native *o,qa_error *e)
{
    qa_application_native_q3_presentation source;
    if(!o || !q3n_native_idle(o) || !q3nn_source(o,&source,e))return false;
    q3n_events_round(o->events); q3n_particles_round(o->particles,source.source_time_ms);
    q3n_player_state_round(o->player_state); q3n_view_round(o->view); q3n_hud_round(o->hud);
    return true;
}
