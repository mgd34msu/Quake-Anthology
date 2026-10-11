#include "bots_private.h"
#include "guest_q3_private.h"
#include "native_q3_wire_state.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q2_bots.h"
#include "qa/native_host_q2_wire.h"
#include "guest_native_q2_private.h"
#include "bot_world.h"
#include "bots_knowledge.h"
#include <limits.h>
#include <math.h>
#include <string.h>

static int32_t quantity(float value) {
    return !isfinite(value)?0:value>=(float)INT32_MAX?INT32_MAX:value<=(float)INT32_MIN?INT32_MIN:(int32_t)value;
}
qa_actor_id application_bot_actor(void *opaque,int32_t number) {
    application_bots *bots=opaque;
    if(number<0) return (qa_actor_id){0};
    if(bots->shared_world) {
        qa_actor_id actor;return application_bot_world_actor(bots->shared_world,number,&actor,NULL)?actor:(qa_actor_id){0};
    }
    for(application_bot_guest *guest=bots->guests;guest;guest=guest->next) {
        if((uint32_t)number<guest->entity_base || (uint32_t)number-guest->entity_base>=QA_Q3_SOURCE_ENTITIES) continue;
        struct application_q3_guest *engine=q3g_engine(guest->provider);qa_actor_id actor={0};
        return engine && engine->game &&
            qa_q3_host_actor(engine->game->host,(uint32_t)number-guest->entity_base,false,&actor,NULL)?actor:(qa_actor_id){0};
    }
    application_provider *source=application_bot_source(bots);
    if(source && source->kind==APPLICATION_PROVIDER_Q3) {
        qa_q3_source_binding binding;
        return qa_q3_source_binding_read(source->state.q3,(uint32_t)number,&binding,NULL) && binding.in_use?
            binding.actor:(qa_actor_id){0};
    }
    uint32_t cursor=(uint32_t)number;const qa_actor_record *record;
    return qa_actors_next(qa_session_actors(bots->application->session),&cursor,&record) &&
        record->id.slot==(uint32_t)number?record->id:(qa_actor_id){0};
}
static qa_vec3 source_vector(const float value[3]) {return qa_v3(value[0],value[1],value[2]);}
bool application_bot_source_weapon(application_bots *bots,qa_actor_id actor,
    int32_t *weapon,int32_t *phase,qa_error *error) {
    application_provider *arsenal=application_provider_for(bots->application,actor,QA_ROLE_ARSENAL,NULL);
    if(!arsenal || !qa_actors_get(qa_session_actors(bots->application->session),actor))
        return application_fail(error,QA_ERROR_NOT_FOUND,"bot selected weapon has no actual live arsenal owner");
    if(arsenal->kind==APPLICATION_PROVIDER_Q1) {
        qa_q1_weapon source;double finished,time;bool present;
        if(!qa_q1_bot_weapon_state_read(arsenal->state.q1,actor,&source,&finished,&time,&present,error)) return false;
        *weapon=0;*phase=present && finished>time?3:0;
        if(present && !application_bot_weapon_slot(arsenal,(int32_t)source,weapon,error)) return false;
    } else if(arsenal->kind==APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_state player;
        if(!qa_q2_weapon_read(arsenal->state.q2,actor,&player,error)) return false;
        if(!application_bot_weapon_slot(arsenal,(int32_t)player.weapon,weapon,error)) return false;
        *phase=player.phase==QA_Q2_ACTIVATING?1:
            player.phase==QA_Q2_DROPPING?2:player.phase==QA_Q2_FIRING?3:0;
    } else if(arsenal->kind==APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state player;
        if(!qa_q3_player_read(arsenal->state.q3,actor,&player))
            return application_fail(error,QA_ERROR_NOT_FOUND,"bot selected Q3 weapon continuation is absent");
        if((unsigned)player.weapon_phase>3)
            return application_fail(error,QA_ERROR_FORMAT,"bot selected Q3 weapon phase is outside its source states");
        *weapon=(int32_t)player.weapon;*phase=(int32_t)player.weapon_phase;
    } else return application_fail(error,QA_ERROR_UNSUPPORTED,"selected original arsenal has no native bot weapon observation");
    return true;
}
static bool native_items(const qa_bot_player_state_view *ps,const qa_bot_inventory_target *inventory,qa_error *error) {
    bool team_arena=ps->product==QA_Q3_TEAM_ARENA;
    int32_t value;
    if(!qa_bot_player_state_slot(ps,QA_BOT_PS_STATS,0,&value,error) ||
       !qa_bot_inventory_write(inventory,QA_BOT_INV_HEALTH,value,error)) return false;
    static const int holdable_inventory[]={QA_BOT_INV_TELEPORTER,QA_BOT_INV_MEDKIT,
        QA_BOT_INV_KAMIKAZE,QA_BOT_INV_PORTAL,QA_BOT_INV_INVULNERABILITY};
    static const int holdable_models[]={26,27,36,37,38};
    for(size_t i=0;i<(team_arena?5u:2u);++i)
        if(!qa_bot_player_state_slot(ps,QA_BOT_PS_STATS,1,&value,error) ||
           !qa_bot_inventory_write(inventory,holdable_inventory[i],value==holdable_models[i],error)) return false;
    static const int powerup_inventory[]={0,QA_BOT_INV_QUAD,QA_BOT_INV_ENVIRO,QA_BOT_INV_HASTE,
        QA_BOT_INV_INVISIBILITY,QA_BOT_INV_REGEN,QA_BOT_INV_FLIGHT,QA_BOT_INV_RED_FLAG,
        QA_BOT_INV_BLUE_FLAG,QA_BOT_INV_NEUTRAL_FLAG};
    for(size_t i=1;i<(team_arena?10u:9u);++i)
        if(!qa_bot_player_state_slot(ps,QA_BOT_PS_POWERUPS,(int32_t)i,&value,error) ||
           !qa_bot_inventory_write(inventory,powerup_inventory[i],value!=0,error)) return false;
    if(team_arena) {
        static const int persistent_inventory[]={QA_BOT_INV_SCOUT,QA_BOT_INV_GUARD,
            QA_BOT_INV_DOUBLER,QA_BOT_INV_AMMO_REGEN};
        for(size_t i=0;i<4;++i)
            if(!qa_bot_player_state_slot(ps,QA_BOT_PS_STATS,2,&value,error) ||
               !qa_bot_inventory_write(inventory,persistent_inventory[i],value==(int32_t)(42+i),error)) return false;
        int32_t team,generic;
        if(!qa_bot_player_state_slot(ps,QA_BOT_PS_PERSISTENT,3,&team,error)) return false;
        generic=0;
        if(team==1 && !qa_bot_player_state_generic(ps,&generic,error)) return false;
        if(!qa_bot_inventory_write(inventory,QA_BOT_INV_RED_CUBE,generic,error) ||
           !qa_bot_player_state_slot(ps,QA_BOT_PS_PERSISTENT,3,&team,error)) return false;
        generic=0;
        if(team!=1 && !qa_bot_player_state_generic(ps,&generic,error)) return false;
        if(!qa_bot_inventory_write(inventory,QA_BOT_INV_BLUE_CUBE,generic,error)) return false;
    }
    return true;
}
static bool shared_powers(application_bots *bots,application_provider *source,qa_actor_id actor,
    const qa_bot_inventory_target *inventory,qa_error *error) {
    bool quad=false,enviro=false;
    if(source && source->kind==APPLICATION_PROVIDER_Q1) {
        double time,started;bool intermission;
        if(!qa_q1_bot_clock_read(source->state.q1,&time,&intermission,&started,error)) return false;
        quad=qa_q1_game_power_expires(source->state.q1,actor,QA_Q1_QUAD)>time;
        enviro=qa_q1_game_power_expires(source->state.q1,actor,QA_Q1_SUIT)>time;
    } else if(source && source->kind==APPLICATION_PROVIDER_Q2) {
        uint64_t time,started;bool intermission;qa_q2_powerups powers;
        if(!qa_q2_bot_clock_read(source->state.q2,&time,&intermission,&started,error)) return false;
        if(qa_actors_get(qa_session_actors(bots->application->session),actor)) {
            if(!qa_q2_powerups_read(source->state.q2,actor,&powers,error)) return false;
            quad=powers.quad_until_ns>time;
            enviro=powers.breather_until_ns>time || powers.enviro_until_ns>time;
        }
    } else return application_fail(error,QA_ERROR_ARGUMENT,"Shared bot powers require their actual Q1 or Q2 source");
    return qa_bot_inventory_write(inventory,QA_BOT_INV_QUAD,quad,error) &&
        qa_bot_inventory_write(inventory,QA_BOT_INV_ENVIRO,enviro,error);
}
static bool native_player(application_bots *bots,application_provider *source,qa_actor_id actor,
    qa_bot_player *out,qa_q3_player *state,qa_error *error) {
    uint32_t slot;qa_q3_player ps;qa_q3_player_state source_player;
    application_native_q3_wire_client_view client;bool present;
    if(!qa_q3_native_client_slot(source->state.q3,actor,&slot,error) ||
       !application_native_q3_wire_client_read(source,slot,&client,&present,error)) return false;
    if(!present || !qa_actor_id_equal(client.actor,actor))
        return application_fail(error,QA_ERROR_NOT_FOUND,"bot player has no actual native source PS owner");
    if(!qa_q3_wire_player_read(source->state.q3,slot,&ps,error)) return false;
    if(!application_bot_source_weapon(bots,actor,&ps.weapon,&ps.weaponState,error)) return false;
    if(!qa_q3_player_read(source->state.q3,actor,&source_player))
        return application_fail(error,QA_ERROR_NOT_FOUND,"bot player native GAME continuation is absent");
    *out=(qa_bot_player){.connected=client.begun,.observer=ps.pmType==2,
        .intermission=ps.pmType==5 || ps.pmType==6,.dead=ps.pmType==3,
        .grounded=ps.groundEntityNum!=QA_Q3_ENTITY_NONE,.crouched=(ps.pmFlags&1)!=0,
        .water_jump=(ps.pmFlags&256)!=0,.grapple_pull=(ps.pmFlags&2048)!=0,
        .firing=(ps.eFlags&256)!=0,.chatting=(ps.eFlags&4096)!=0,.invisible=ps.powerups[4]!=0,
        .origin=source_vector(ps.origin),.velocity=source_vector(ps.velocity),
        .eye=source_vector(ps.origin),.view_angles=source_vector(ps.viewangles),
        .presence=(ps.pmFlags&1)?4:2,.current_weapon=ps.weapon,.weapon_state=ps.weaponState,
        .weapon_time_ms=ps.weaponTime,.deaths=ps.persistant[8],.spawn_sequence=source_player.spawn_count,
        .teleport_sequence=source_player.teleport_revision,.teleported=source_player.teleport_lock_ms>0,
        .air_time=(float)source_player.air_out_time/1000,.last_damage_cause=source_player.last_hurt_mod};
    if(state) *state=ps;
    out->eye.z+=(float)ps.viewheight;memcpy(out->delta_angles,ps.deltaAngles,sizeof(out->delta_angles));
    out->last_attacker=application_bot_actor(bots,ps.persistant[6]);
    out->last_victim=application_bot_actor(bots,source_player.last_killed_client);
    return true;
}
bool application_bot_inventory_update(void *opaque,qa_actor_id actor,const qa_bot_player *sample,
    const qa_bot_player_state_view *state,const qa_bot_inventory_target *inventory,qa_error *error) {
    application_bots *bots=opaque;application_provider *source=application_bot_source(bots);
    if(!application_bots_knowledge_update(bots,actor,error)) return false;
    if(bots->shared_world && sample && state && state->bytes && inventory)
        return application_bot_inventory(bots,actor,inventory,error) &&
            shared_powers(bots,source,actor,inventory,error);
    if(!source || source->kind!=APPLICATION_PROVIDER_Q3 || !sample || !state || !state->bytes ||
       !inventory || !qa_actors_get(qa_session_actors(bots->application->session),actor))
        return application_fail(error,QA_ERROR_UNSUPPORTED,"bot inventory requires its retained actual Q3 PS sample");
    application_provider *arsenal=application_provider_for(bots->application,actor,QA_ROLE_ARSENAL,NULL);
    if(!arsenal) return application_fail(error,QA_ERROR_NOT_FOUND,"bot inventory selected arsenal is absent");
    unsigned shift=state->product==QA_Q3_TEAM_ARENA?1u:0u;int32_t armor;
    return application_bot_inventory(bots,actor,inventory,error) &&
        qa_bot_player_state_slot(state,QA_BOT_PS_STATS,(int32_t)(3+shift),&armor,error) &&
        qa_bot_inventory_write(inventory,QA_BOT_INV_ARMOR,armor,error) &&
        native_items(state,inventory,error);
}
bool application_bot_player(void *opaque,qa_actor_id actor,qa_bot_player *out,qa_q3_player *state,qa_error *error) {
    application_bots *bots=opaque;qa_application *application=bots->application;
    application_provider *source=application_bot_source(bots);
    if(bots->shared_world) {
        int32_t number;application_bot_world_entity actual;
        if(!application_bot_world_entity_id(bots->shared_world,actor,&number,error) ||
           !application_bot_world_read(bots->shared_world,number,&actual,error)) return false;
        if(!actual.has_player) return application_fail(error,QA_ERROR_NOT_FOUND,"shared bot has no actual source PS view");
        const qa_q3_player *ps=&actual.player;
        *out=(qa_bot_player){.connected=actual.connected,.observer=ps->pmType==2,.dead=ps->pmType==3,
            .grounded=ps->groundEntityNum!=QA_Q3_ENTITY_NONE,.origin=source_vector(ps->origin),
            .velocity=source_vector(ps->velocity),.eye=source_vector(ps->origin),.view_angles=source_vector(ps->viewangles),
            .presence=2,.current_weapon=ps->weapon,.weapon_state=ps->weaponState};
        if(state) *state=*ps;
        out->eye.z+=(float)ps->viewheight;
        return true;
    }
    if(source && source->kind==APPLICATION_PROVIDER_Q3) return native_player(bots,source,actor,out,state,error);
    if(state) return application_fail(error,QA_ERROR_UNSUPPORTED,"bot source player copy requires its genuine GAME player state");
    qa_builtin_services services=application_builtin_services(application,application->world,application->physics);
    qa_builtin_player_info info;qa_player_state control={0};qa_body_state body;qa_combat_state combat;
    if(!services.player_info(services.context,actor,&info) ||
       !qa_world_body_read(application->world,actor,&body,error) ||
       !qa_combat_read_traits(application->combat,actor,&combat,error))
        return application_fail(error,QA_ERROR_NOT_FOUND,"bot actor has no live selected player projection");
    bool controlled=qa_application_control_read(application,actor,&control);
    if(!controlled) {control.view_angles=body.angles;control.view_height=info.view_height;}
    *out=(qa_bot_player){.connected=info.connected,.observer=info.spectator,.dead=info.dead || combat.health<=0,
        .grounded=control.ground.hit!=QA_TRACE_HIT_NONE,.origin=body.origin,.velocity=body.velocity,
        .eye=qa_vec_add(body.origin,control.view_offset),.view_angles=control.view_angles,
        .presence=2,.firing=(control.buttons&1)!=0,.air_time=(float)qa_session_elapsed(application->session)/1e9f};
    out->eye.z+=control.view_height;
    if(actor.slot<application->motion_capacity) {
        application_motion_record *motion=&application->motion[actor.slot];
        if(motion->active && qa_actor_id_equal(motion->actor,actor) && motion->reason==QA_BUILTIN_MOTION_TELEPORT)
            out->teleport_sequence=motion->revision;
    }
    qa_builtin_actor_traits traits;
    if(services.actor_traits(services.context,actor,&traits)) out->invisible=traits.invisible;
    application_provider *arsenal=application_provider_for(application,actor,QA_ROLE_ARSENAL,NULL);
    qa_clock_state source_clock={0};
    uint64_t source_ns=arsenal && qa_session_clock(application->session,arsenal->owner,&source_clock)?
        source_clock.frame.time_ns:qa_session_elapsed(application->session);
    if(arsenal && arsenal->kind==APPLICATION_PROVIDER_Q1) {
        qa_q1_player_view player;
        if(qa_q1_player_read(arsenal->state.q1,actor,&player)) {
            out->current_weapon=(int32_t)player.weapon+1;out->weapon_state=out->firing?3:0;
        }
    } else if(arsenal && arsenal->kind==APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_state player;
        if(!qa_q2_weapon_read(arsenal->state.q2,actor,&player,error)) return false;
        out->current_weapon=(int32_t)player.weapon;out->weapon_state=player.phase==QA_Q2_ACTIVATING?1:
            player.phase==QA_Q2_DROPPING?2:player.phase==QA_Q2_FIRING?3:0;
        uint64_t now=source_ns;
        out->weapon_time_ms=player.fire_finished_ns>now?quantity((float)(player.fire_finished_ns-now)/1e6f):0;
    } else if(arsenal && arsenal->kind==APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state player;
        if(!qa_q3_player_read(arsenal->state.q3,actor,&player))
            return application_fail(error,QA_ERROR_NOT_FOUND,"bot selected Q3 arsenal actor is absent");
        out->current_weapon=(int32_t)player.weapon;out->weapon_state=(int32_t)player.weapon_phase;out->weapon_time_ms=player.weapon_time_ms;
        out->deaths=player.deaths;out->spawn_sequence=player.spawn_count;out->grapple_pull=player.grapple_pull;
        out->teleported=player.teleport_lock_ms>0;
        out->air_time=(float)player.air_out_time/1000;
    }
    if(control.state.kind==QA_RULESET_Q3) {
        memcpy(out->delta_angles,control.state.data.q3.delta_angle_words,sizeof(out->delta_angles));
        out->crouched=(control.state.data.q3.movement_flags&1)!=0;
    } else if(control.state.kind==QA_RULESET_Q2_CLASSIC) {
        for(size_t i=0;i<3;++i) out->delta_angles[i]=(uint16_t)control.state.data.q2.delta_angle_shorts[i];
        out->crouched=(control.state.data.q2.flags&1)!=0;
    } else if(control.state.kind==QA_RULESET_Q2_RERELEASE) {
        qa_vec3 delta=control.state.data.q2r.delta_angles;
        out->delta_angles[0]=qa_angle_to_word(delta.x);
        out->delta_angles[1]=qa_angle_to_word(delta.y);
        out->delta_angles[2]=qa_angle_to_word(delta.z);out->crouched=(control.state.data.q2r.flags&1)!=0;
    }
    out->presence=out->crouched?4:2;
    if(application->modes && application->primary_mode_ready) {
        qa_mode_view mode;if(!qa_modes_read(application->modes,application->primary_mode,&mode,error)) return false;
        out->intermission=mode.phase==QA_MODE_INTERMISSION || mode.phase==QA_MODE_FINISHED;
    }
    return true;
}
static bool original_q2_entity(application_bots *bots,qa_actor_id actor,
    qa_bot_entity *out,bool *handled,qa_error *error)
{
    qa_application *app=bots->application;
    const qa_actor_record *record=qa_actors_get(qa_session_actors(app->session),actor);
    *handled=false;
    if(!record || !record->has_source) return true;
    application_provider *source=application_actor_source_provider(app,actor);
    if(!source || source->owner!=record->owner || source->kind!=APPLICATION_PROVIDER_NATIVE ||
       !source->product || source->product->family!=QA_GAME_Q2 || !source->state.native.q2_engine ||
       source->state.native.q2_engine->profile!=QA_NATIVE_Q2_GAME_API2023) return true;
    *handled=true;
    qa_native_host_q2_entity view;
    if(!qa_native_host_q2_bot_entity(source->state.native.host,record->source_slot,&view,error)) return false;
    *out=(qa_bot_entity){0};
    if(!view.in_use || !view.bot.registered || !qa_actor_id_equal(view.binding.actor,actor)) return true;
    if(!application_bot_entity_number(bots,actor,&out->number,error)) return false;
    out->present=true;out->linked=view.linked;
    out->hidden=(view.bot.flags&(UINT64_C(1)<<10))!=0;
    out->proximity_trigger=(view.bot.flags&(UINT64_C(1)<<27))!=0;
    out->observation=(qa_bot_entity_update){.actor=actor,
        .type=view.bot.player?1:(view.bot.flags&(UINT64_C(1)<<6))?2:out->proximity_trigger?3:0,
        .flags=(view.bot.player || (view.server_flags&4u)) && view.bot.health<=0?1:0,
        .origin=source_vector(view.state.origin),.old_origin=source_vector(view.state.old_origin),
        .angles=view.bot.player?view.bot.view_angles:source_vector(view.state.angles),
        .mins=view.bounds.mins,.maxs=view.bounds.maxs,.ground_entity=-1,.solid=view.solid,
        .model_index=(int32_t)view.state.modelindex,.model_index2=(int32_t)view.state.modelindex2,
        .frame=(int32_t)view.state.frame};
    return true;
}

bool application_bot_entity(void *opaque,qa_actor_id actor,qa_bot_entity *out,qa_error *error) {
    application_bots *bots=opaque;qa_application *application=bots->application;
    bool original;
    if(!original_q2_entity(bots,actor,out,&original,error)) return false;
    if(original) return true;
    application_provider *source=application_bot_source(bots);
    if(bots->shared_world) {
        int32_t number;application_bot_world_entity actual;
        if(!application_bot_world_entity_id(bots->shared_world,actor,&number,error) ||
           !application_bot_world_read(bots->shared_world,number,&actual,error)) return false;
        const qa_q3_entity *state=&actual.state;
        *out=(qa_bot_entity){.number=number,.present=actual.present,.linked=actual.linked,.hidden=actual.hidden,
            .observation={.actor=actor,.type=state->eType,.flags=state->eFlags,.origin=actual.origin,
                .angles=actual.has_player?source_vector(state->apos.base):actual.angles,
                .old_origin=source_vector(state->origin2),.mins=actual.bounds.mins,.maxs=actual.bounds.maxs,
                .ground_entity=state->groundEntityNum,.solid=actual.has_inline_model?3:2,
                .model_index=state->modelindex,.model_index2=state->modelindex2,.frame=state->frame,
                .event=state->event,.event_parameter=state->eventParm,.powerups=state->powerups,.weapon=state->weapon,
                .legs_animation=state->legsAnim,.torso_animation=state->torsoAnim}};
        return true;
    }
    if(source && source->kind==APPLICATION_PROVIDER_Q3) {
        uint32_t slot;qa_q3_source_binding binding;qa_q3_entity state;qa_q3_wire_visibility visibility;
        *out=(qa_bot_entity){0};
        if(!qa_q3_source_actor_slot(source->state.q3,actor,&slot,error) ||
           !qa_q3_source_binding_read(source->state.q3,slot,&binding,error) ||
           !qa_actor_id_equal(binding.actor,actor) ||
           !qa_q3_wire_entity_read(source->state.q3,slot,&state,&visibility,error)) return false;
        out->number=(int32_t)slot;out->present=binding.in_use;out->linked=visibility.linked;
        if(!out->present) return true;
        qa_q3_wire_body body;if(!qa_q3_wire_body_read(source->state.q3,slot,&body,error) ||
            !qa_actor_id_equal(body.actor,actor)) return false;
        out->hidden=(visibility.server_flags&1)!=0;out->missile=state.eType==3;
        out->grapple=out->missile && state.weapon==QA_Q3_W_GRAPPLE;out->temporary_event=state.eType>13;
        out->proximity_trigger=source->product && source->product->campaign_id==QA_CAMPAIGN_MISSIONPACK &&
            body.colliding && qa_collision_bits_equal(body.collision.contents,
                qa_collision_bit(QA_CONTENT_TRIGGER)) && body.proximity_trigger;
        if(slot<QA_Q3_SOURCE_CLIENTS) {
            int32_t phase;
            if(!application_bot_source_weapon(bots,actor,&state.weapon,&phase,error)) return false;
        }
        out->observation=(qa_bot_entity_update){.actor=actor,.type=state.eType,.flags=state.eFlags,
            .origin=body.current.origin,.angles=slot<QA_Q3_SOURCE_CLIENTS?source_vector(state.apos.base):body.current.angles,
            .old_origin=source_vector(state.origin2),.mins=body.current.bounds.mins,.maxs=body.current.bounds.maxs,
            .ground_entity=state.groundEntityNum,.solid=body.colliding && body.collision.inline_model?3:2,
            .model_index=state.modelindex,.model_index2=state.modelindex2,.frame=state.frame,
            .event=state.event,.event_parameter=state.eventParm,.powerups=state.powerups,.weapon=state.weapon,
            .legs_animation=state.legsAnim,.torso_animation=state.torsoAnim};
        return true;
    }
    qa_body_state body;qa_linked_body linked;qa_actor_collision collision;
    if(actor.slot>INT32_MAX) return application_fail(error,QA_ERROR_UNSUPPORTED,"bot entity number exceeds source range");
    *out=(qa_bot_entity){.number=(int32_t)actor.slot};
    if(!qa_actors_get(qa_session_actors(application->session),actor)) return true;
    if(!qa_world_body_read(application->world,actor,&body,error)) return false;
    out->present=true;out->linked=qa_world_linked(application->world,actor,&linked);
    bool colliding=qa_world_get_collision(application->world,actor,&collision,NULL);
    out->observation=(qa_bot_entity_update){.actor=actor,.origin=body.origin,.old_origin=body.origin,
        .angles=body.angles,.mins=body.bounds.mins,.maxs=body.bounds.maxs,
        .ground_entity=-1,.solid=colliding?1:0,.model_index=colliding?(int32_t)collision.model:0};
    const qa_actor_record *record=qa_actors_get(qa_session_actors(application->session),actor);
    application_provider **providers=application->routing_providers?application->routing_providers:application->providers;
    size_t provider_count=application->routing_providers?application->routing_provider_count:application->provider_count;
    for(size_t i=0;record && i<provider_count;++i) {
        application_provider *provider=providers[i];
        if(provider->kind!=APPLICATION_PROVIDER_Q3 || provider->owner!=record->owner) continue;
        qa_q3_entity_view view;
        if(!qa_q3_entity_read(provider->state.q3,actor,&view,error)) return false;
        out->hidden=view.kind==QA_Q3_ENTITY_HIDDEN;out->missile=view.kind==QA_Q3_ENTITY_MISSILE;
        out->grapple=view.kind==QA_Q3_ENTITY_GRAPPLE;
        out->observation.weapon=(int32_t)view.weapon;out->observation.flags=(int32_t)view.flags;
        out->observation.powerups=(int32_t)view.powerups;
        out->observation.legs_animation=view.legs_animation;out->observation.torso_animation=view.torso_animation;
        out->observation.type=view.kind==QA_Q3_ENTITY_PLAYER?1:view.kind==QA_Q3_ENTITY_ITEM?2:
            view.kind==QA_Q3_ENTITY_MISSILE?3:view.kind==QA_Q3_ENTITY_MOVER?4:
            view.kind==QA_Q3_ENTITY_BEAM?5:view.kind==QA_Q3_ENTITY_SPEAKER?7:
            view.kind==QA_Q3_ENTITY_GRAPPLE?11:0;
        break;
    }
    qa_builtin_services services=application_builtin_services(application,application->world,application->physics);
    qa_builtin_actor_traits traits;
    if(services.actor_traits(services.context,actor,&traits) && traits.player) {
        out->observation.type=1;qa_bot_player player;
        if(!application_bot_player(bots,actor,&player,NULL,error)) return false;
        out->observation.weapon=player.current_weapon;
        if(player.dead) out->observation.flags|=1;
    }
    return true;
}
