#include "bots_private.h"
#include <limits.h>
#include <math.h>
#include <string.h>

static int32_t quantity(float value) {
    return !isfinite(value)?0:value>=INT32_MAX?INT32_MAX:value<=INT32_MIN?INT32_MIN:(int32_t)value;
}
qa_actor_id application_bot_actor(void *opaque,int32_t number) {
    application_bots *bots=opaque;
    if(number<0) return (qa_actor_id){0};
    uint32_t cursor=(uint32_t)number;const qa_actor_record *record;
    return qa_actors_next(qa_session_actors(bots->application->session),&cursor,&record) &&
        record->id.slot==(uint32_t)number?record->id:(qa_actor_id){0};
}
bool application_bot_player(void *opaque,qa_actor_id actor,qa_bot_player *out,qa_error *error) {
    application_bots *bots=opaque;qa_application *application=bots->application;
    qa_builtin_services services=application_builtin_services(application,application->world,application->physics);
    qa_builtin_player_info info;qa_application_control_view control={0};qa_body_state body;qa_combat_state combat;
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
    out->inventory[QA_BOT_INV_HEALTH]=quantity(combat.health);
    out->inventory[QA_BOT_INV_ARMOR]=quantity(combat.armor.regular.points);
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
            double now=(double)source_ns/1e9;
            out->inventory[QA_BOT_INV_QUAD]=player.power_expires[QA_Q1_QUAD]>now;
            out->inventory[QA_BOT_INV_INVISIBILITY]=player.power_expires[QA_Q1_INVISIBILITY]>now;
            out->inventory[QA_BOT_INV_ENVIRO]=player.power_expires[QA_Q1_SUIT]>now;
        }
    } else if(arsenal && arsenal->kind==APPLICATION_PROVIDER_Q2) {
        qa_q2_weapon_state player;
        if(!qa_q2_weapon_read(arsenal->state.q2,actor,&player,error)) return false;
        out->current_weapon=player.weapon;out->weapon_state=player.phase==QA_Q2_ACTIVATING?1:
            player.phase==QA_Q2_DROPPING?2:player.phase==QA_Q2_FIRING?3:0;
        uint64_t now=source_ns;
        out->weapon_time_ms=player.fire_finished_ns>now?quantity((float)(player.fire_finished_ns-now)/1e6f):0;
    } else if(arsenal && arsenal->kind==APPLICATION_PROVIDER_Q3) {
        qa_q3_player_state player;
        if(!qa_q3_player_read(arsenal->state.q3,actor,&player))
            return application_fail(error,QA_ERROR_NOT_FOUND,"bot selected Q3 arsenal actor is absent");
        out->current_weapon=player.weapon;out->weapon_state=player.weapon_phase;out->weapon_time_ms=player.weapon_time_ms;
        out->deaths=player.deaths;out->spawn_sequence=player.spawn_count;out->grapple_pull=player.grapple_pull;
        out->teleported=player.teleport_lock_ms>0;
        out->air_time=(float)player.air_out_time/1000;
        static const int inventory[]={0,QA_BOT_INV_QUAD,QA_BOT_INV_ENVIRO,QA_BOT_INV_HASTE,
            QA_BOT_INV_INVISIBILITY,QA_BOT_INV_REGEN,QA_BOT_INV_FLIGHT,QA_BOT_INV_RED_FLAG,
            QA_BOT_INV_BLUE_FLAG,QA_BOT_INV_NEUTRAL_FLAG,QA_BOT_INV_SCOUT,QA_BOT_INV_GUARD,
            QA_BOT_INV_DOUBLER,QA_BOT_INV_AMMO_REGEN};
        int32_t now=(int32_t)(uint32_t)(source_ns/1000000);
        for(size_t i=1;i<sizeof(inventory)/sizeof(*inventory) && i<QA_Q3_POWERUP_COUNT;++i)
            out->inventory[inventory[i]]=player.powerups[i]>now;
        if(player.holdable==QA_Q3_H_TELEPORTER) out->inventory[QA_BOT_INV_TELEPORTER]=1;
        if(player.holdable==QA_Q3_H_MEDKIT) out->inventory[QA_BOT_INV_MEDKIT]=1;
        if(player.holdable==QA_Q3_H_KAMIKAZE) out->inventory[QA_BOT_INV_KAMIKAZE]=1;
        if(player.holdable==QA_Q3_H_PORTAL) out->inventory[QA_BOT_INV_PORTAL]=1;
        if(player.holdable==QA_Q3_H_INVULNERABILITY) out->inventory[QA_BOT_INV_INVULNERABILITY]=1;
        if(player.persistent==QA_Q3_P_SCOUT) out->inventory[QA_BOT_INV_SCOUT]=1;
        if(player.persistent==QA_Q3_P_GUARD) out->inventory[QA_BOT_INV_GUARD]=1;
        if(player.persistent==QA_Q3_P_DOUBLER) out->inventory[QA_BOT_INV_DOUBLER]=1;
        if(player.persistent==QA_Q3_P_AMMOREGEN) out->inventory[QA_BOT_INV_AMMO_REGEN]=1;
    }
    if(control.state.kind==QA_MOVEMENT_Q3) {
        memcpy(out->delta_angles,control.state.data.q3.delta_angle_words,sizeof(out->delta_angles));
        out->crouched=(control.state.data.q3.movement_flags&1)!=0;
    } else if(control.state.kind==QA_MOVEMENT_Q2_CLASSIC) {
        for(size_t i=0;i<3;++i) out->delta_angles[i]=(uint16_t)control.state.data.q2.delta_angle_shorts[i];
        out->crouched=(control.state.data.q2.flags&1)!=0;
    } else if(control.state.kind==QA_MOVEMENT_Q2_RERELEASE) {
        qa_vec3 delta=control.state.data.q2r.delta_angles;
        out->delta_angles[0]=application_bot_angle_word(delta.x);
        out->delta_angles[1]=application_bot_angle_word(delta.y);
        out->delta_angles[2]=application_bot_angle_word(delta.z);out->crouched=(control.state.data.q2r.flags&1)!=0;
    }
    out->presence=out->crouched?4:2;
    if(application->modes && application->primary_mode_ready) {
        qa_mode_view mode;if(!qa_modes_read(application->modes,application->primary_mode,&mode,error)) return false;
        out->intermission=mode.phase==QA_MODE_INTERMISSION || mode.phase==QA_MODE_FINISHED;
    }
    return arsenal && (arsenal->kind==APPLICATION_PROVIDER_Q1 || arsenal->kind==APPLICATION_PROVIDER_Q2 ||
                       arsenal->kind==APPLICATION_PROVIDER_Q3)?
        application_bot_inventory(bots,actor,out->inventory,error):true;
}
bool application_bot_entity(void *opaque,qa_actor_id actor,qa_bot_entity *out,qa_error *error) {
    application_bots *bots=opaque;qa_application *application=bots->application;
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
        out->observation.weapon=view.weapon;out->observation.flags=(int32_t)view.flags;
        out->observation.powerups=(int32_t)view.powerups;
        out->observation.legs_animation=view.legs_animation;out->observation.torso_animation=view.torso_animation;
        out->observation.type=view.kind==QA_Q3_ENTITY_PLAYER?1:view.kind==QA_Q3_ENTITY_ITEM?2:
            view.kind==QA_Q3_ENTITY_MISSILE?3:view.kind==QA_Q3_ENTITY_MOVER?4:view.kind==QA_Q3_ENTITY_GRAPPLE?11:0;
        break;
    }
    qa_builtin_services services=application_builtin_services(application,application->world,application->physics);
    qa_builtin_actor_traits traits;
    if(services.actor_traits(services.context,actor,&traits) && traits.player) {
        out->observation.type=1;qa_bot_player player;
        if(!application_bot_player(bots,actor,&player,error)) return false;
        out->observation.weapon=player.current_weapon;
        if(player.dead) out->observation.flags|=1;
    }
    return true;
}
