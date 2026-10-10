#include "native_q1_wire_qw.h"
#include "native_q1_console.h"
#include "map_players_private.h"
#include "control_frame.h"
#include "network_q1_signon.h"
#include "qa/application_equipment.h"
#include "qa/game_q1_weapons.h"
#include "qa/network_q1_channel.h"
#include "qa/text.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
static uint8_t byte(double);

bool application_native_q1_qw_selected(qa_application *app) {
    application_provider *p = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    return p && p->kind == APPLICATION_PROVIDER_Q1 && p->launch &&
        p->launch->selection.clock.kind == QA_RULESET_QUAKEWORLD;
}
static const application_player_record *record(qa_application *app, qa_actor_id actor) {
    for (size_t i = 0; app->players && i < app->players->count; ++i) {
        const application_player_record *row = &app->players->records[i];
        if (!row->retiring && qa_actor_id_equal(row->actor, actor)) return row;
    }
    return NULL;
}
static bool binding(application_native_q1_wire_source *source, qa_actor_id actor,
    uint32_t *slot, const application_player_record **row, qa_error *error) {
    const application_player_record *actual = record(source->provider->application, actor);
    uint32_t physical;
    if (!actual || !qa_q1_native_client_slot(source->provider->state.q1, actor, &physical, error) ||
        physical >= 32 || actual->client_slot != physical) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native QuakeWorld client lost its genuine physical row");
        return false;
    }
    *slot = physical + 1;
    if (row) *row = actual;
    return true;
}
static void vector(float out[3], qa_vec3 value) { out[0]=value.x; out[1]=value.y; out[2]=value.z; }
static const char *text(qa_application *app, qa_string_id id) {
    return id ? qa_strings_cstr(qa_session_strings(app->session), id) : "";
}
static bool source_view(application_native_q1_wire_source *source,
    qa_application_network_qw_source *out, qa_error *error) {
    qa_clock_state clock; uint64_t time_ns; double elapsed;
    qa_cvars *cvars = application_native_q1_console_registry(source->provider);
    if (!cvars || !qa_q1_game_clock_read(source->provider->state.q1, &time_ns, &elapsed) ||
        !qa_session_clock(source->provider->application->session, source->provider->owner, &clock))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native QuakeWorld source lost its clock or cvar owner");
    const char *serverinfo;
    if (!application_native_q1_source_info(source->provider, false, &serverinfo, error)) return false;
    *out = (qa_application_network_qw_source){.owner=source->provider->owner,
        .entity_count=source->receipt.entity_slots, .source_time_ns=time_ns,
        .completed_time_ns=clock.frame.time_ns, .cvars=cvars, .serverinfo=serverinfo};
    return true;
}
bool application_native_q1_qw_source(qa_application *app, qa_application_network_qw_source *out, qa_error *error) {
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native QuakeWorld source output");
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_qw_begin(app, &source, error)) return false;
    bool okay=source_view(&source,out,error);
    application_native_q1_wire_end(&source); return okay;
}
bool application_native_q1_qw_world(qa_application *app, qa_application_network_qw_world *out, qa_error *error) {
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native QuakeWorld world output");
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_qw_begin(app,&source,error)) return false;
    qa_q1_wire_world world; qa_application_map_view map;
    qa_application_network_qw_world value={0};
    bool okay=source_view(&source,&value.source,error) && qa_q1_wire_world_read(&source.receipt,&world) &&
        qa_application_map_read(app,&map) && map.resource &&
        application_native_q1_source_visible_gamedir(source.provider,&value.game_directory,error);
    if (okay) {
        value.map=text(app,world.map); value.level=text(app,world.level); value.cd_track=world.cd_track;
        value.map_bytes=qa_resource_bytes(map.resource);
        value.protocol=(qa_net_protocol_id){.kind=QA_NET_QW28}; value.max_clients=source.receipt.client_slots;
        static const qa_q1_source_setting settings[]={QA_Q1_SOURCE_GRAVITY,QA_Q1_SOURCE_STOPSPEED,
            QA_Q1_SOURCE_MAXSPEED,QA_Q1_SOURCE_SPECTATORMAXSPEED,QA_Q1_SOURCE_ACCELERATE,
            QA_Q1_SOURCE_AIRACCELERATE,QA_Q1_SOURCE_WATERACCELERATE,QA_Q1_SOURCE_FRICTION,QA_Q1_SOURCE_WATERFRICTION};
        float *const values[]={&value.movement.gravity,&value.movement.stop_speed,&value.movement.max_speed,
            &value.movement.spectator_max_speed,&value.movement.accelerate,&value.movement.air_accelerate,
            &value.movement.water_accelerate,&value.movement.friction,&value.movement.water_friction};
        for (size_t i=0;okay && i<sizeof(settings)/sizeof(*settings);++i) {
            const qa_cvar_view *variable=qa_q1_source_read(source.provider->state.q1,settings[i]);
            if (!variable || !isfinite(variable->number))
                okay=application_fail(error,QA_ERROR_FORMAT,"Native QuakeWorld move cvar is absent or nonfinite");
            else *values[i]=variable->number;
        }
        value.movement.entity_gravity=1;
        for (size_t i=0;i<64;++i) value.lightstyles[i]=text(app,world.lightstyles[i]);
        if (!*value.game_directory || !value.map || !value.level || !value.map_bytes.data || !value.map_bytes.size)
            okay=application_fail(error,QA_ERROR_FORMAT,"Native QuakeWorld world lost its retained content identity");
    }
    if (okay) *out=value;
    else if (error && error->code==QA_OK) application_fail(error,QA_ERROR_NOT_FOUND,"Native QuakeWorld world observation is incomplete");
    application_native_q1_wire_end(&source); return okay;
}
static bool entity(application_native_q1_wire_source *source,qa_actor_id actor,
    qa_application_network_qw_entity *out,qa_error *error) {
    qa_application *app=source->provider->application; uint32_t slot,index;
    qa_application_visual_view visual;
    if (!qa_q1_wire_actor_slot(&source->receipt,actor,&slot) || !slot ||
        !qa_application_visual_read(app,actor,&visual,error)) return false;
    const char *model=visual.models[0];
    qa_string_id resource=model?qa_strings_find(qa_session_strings(app->session),
        (qa_bytes){(const uint8_t *)model,strlen(model)}):0;
    if (!qa_q1_wire_index(&source->receipt,true,resource,&index))
        return application_fail(error,QA_ERROR_FORMAT,"Native QuakeWorld entity model leaves its ordered source precache");
    if (index && slot>=512)
        return application_fail(error,QA_ERROR_UNSUPPORTED,
            "Native QuakeWorld modeled Source edict exceeds the actual QW28 packet entity field");
    uint32_t client_slot;
    bool player=qa_q1_native_client_slot(source->provider->state.q1,actor,&client_slot,NULL);
    qa_application_network_qw_entity value={.number=slot,.model=index,.frame=trunc((double)visual.frame),
        .colormap=player?slot:(double)visual.colormap,.skin=trunc((double)visual.skin),.effects=trunc((double)visual.effects)};
    vector(value.origin,visual.body.origin); vector(value.angles,visual.body.angles);
    uint32_t current;
    if (!qa_q1_wire_receipt_current(&source->receipt) ||
        !qa_q1_wire_actor_slot(&source->receipt,actor,&current) || current!=slot)
        return application_fail(error,QA_ERROR_ARGUMENT,"Native QuakeWorld entity changed during visual observation");
    *out=value;return true;
}
static bool client_read(application_native_q1_wire_source *held,qa_actor_id actor,
    qa_application_network_qw_client *out,qa_error *error) {
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native QuakeWorld client output");
    application_native_q1_wire_source source=*held;
    qa_application *app=source.provider->application;
    uint32_t slot; const application_player_record *row; qa_body_state body;
    qa_q1_source_client_view client; qa_q1_wire_world world;
    bool okay=binding(&source,actor,&slot,&row,error) &&
        qa_q1_source_client_read(source.provider->state.q1,actor,&client) &&
        qa_world_body_read(app->world,actor,&body,error) && qa_q1_wire_world_read(&source.receipt,&world);
    qa_application_network_qw_client value={.actor=actor,.stat_mask=UINT16_C(0xfffd)};
    if (okay) {
        value.source_slot=slot; value.begun=!row->source_begin_pending && !row->deferred;
        value.spectator=row->spectator; value.frags=client.frags;
        if (client.spectator_track_slot>32)
            okay=application_fail(error,QA_ERROR_FORMAT,"Native QuakeWorld tracker leaves its physical client table");
        else if (client.spectator_track_slot)
            (void)qa_q1_source_client_actor(source.provider->state.q1,client.spectator_track_slot-1,&value.spectator_track);
        value.entity.number=slot; value.entity.colormap=slot;
        vector(value.entity.origin,body.origin); vector(value.entity.angles,body.angles);
        vector(value.minimum,body.bounds.mins); vector(value.velocity,body.velocity);
        if (actor.slot>=app->control_capacity || !app->controls[actor.slot].active ||
            app->controls[actor.slot].retired || app->controls[actor.slot].moving ||
            !qa_actor_id_equal(app->controls[actor.slot].player.actor,actor))
            okay=application_fail(error,QA_ERROR_UNSUPPORTED,"Native QuakeWorld client lacks its actual selected control");
        else vector(value.view_offset,app->controls[actor.slot].player.view_offset);
        uint32_t server_items=world.server_flags<<28;
        int32_t signed_server_items;memcpy(&signed_server_items,&server_items,sizeof(server_items));
        value.stats[15]=signed_server_items;
        qa_actor_id stats_actor=actor;
        bool has_stats=value.begun && !value.spectator;
        if (okay && value.begun && value.spectator && client.spectator_track_slot) {
            uint32_t tracked_slot;const application_player_record *tracked;
            okay=qa_q1_wire_qw_stats_read(&source.receipt,client.spectator_track_slot,value.stats,error);
            if (okay && value.spectator_track.registry) {
                okay=binding(&source,value.spectator_track,&tracked_slot,&tracked,error);
                if (okay) {stats_actor=value.spectator_track;has_stats=!tracked->spectator &&
                    !tracked->source_begin_pending && !tracked->deferred;}
            }
            value.stats[10]=0;
        }
        if (okay && has_stats) {
            qa_q1_wire_player player; qa_combat_state combat; qa_application_equipment_view equipment; uint32_t model;
            okay=qa_q1_wire_player_read(&source.receipt,stats_actor,&player,error) &&
                qa_combat_read(app->combat,stats_actor,&combat,error) &&
                qa_application_equipment_read(app,stats_actor,&equipment,error) &&
                qa_q1_wire_index(&source.receipt,true,player.weapon_model,&model) &&
                qa_application_equipment_current(app,&equipment);
            if (okay && !value.spectator) okay=entity(&source,actor,&value.entity,error);
            if (okay) {
                if (!value.spectator) {value.health=combat.health;value.weapon_frame=player.weapon_frame;}
                value.stats[0]=trunc((double)combat.health);value.stats[2]=model;
                value.stats[3]=trunc(player.ammo);
                value.stats[4]=trunc((double)(combat.armor.regular.kind==QA_ARMOR_NONE?0:combat.armor.regular.points));
                value.stats[5]=player.weapon_frame; value.stats[6]=trunc(player.shells);value.stats[7]=trunc(player.nails);
                value.stats[8]=trunc(player.rockets);value.stats[9]=trunc(player.cells);
                if (!value.spectator) value.stats[10]=player.weapon;
                uint32_t items=player.items|(player.items2<<23)|(world.server_flags<<28);
                int32_t signed_items;memcpy(&signed_items,&items,sizeof(items));value.stats[15]=signed_items;
            }
        }
        value.stats[11]=world.total_secrets;value.stats[12]=world.total_monsters;
        value.stats[13]=world.found_secrets;value.stats[14]=world.killed_monsters;
        uint32_t items=(uint32_t)(int32_t)value.stats[15] | server_items;
        int32_t signed_items;memcpy(&signed_items,&items,sizeof(items));value.stats[15]=signed_items;
        if (okay) okay=application_control_last_qw_command(app,actor,&value.command,
            &value.command_time_ns,&value.command_present,error) && qa_q1_wire_receipt_current(&source.receipt);
    }
    if (okay) *out=value;
    else if (error && error->code==QA_OK) application_fail(error,QA_ERROR_NOT_FOUND,"Native QuakeWorld player observation lost its held source");
    return okay;
}
bool application_native_q1_qw_client(qa_application *app,qa_actor_id actor,
    qa_application_network_qw_client *out,qa_error *error) {
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_qw_begin(app,&source,error)) return false;
    bool okay=client_read(&source,actor,out,error);
    application_native_q1_wire_end(&source);return okay;
}
bool application_native_q1_qw_retire_capture(application_provider *p,qa_actor_id actor,qa_error *error) {
    if (!p || !p->launch || p->kind!=APPLICATION_PROVIDER_Q1 ||
        p->launch->selection.clock.kind!=QA_RULESET_QUAKEWORLD || !p->constructed || !p->attached ||
        p->close_pending || application_world_provider(p->application,QA_ROLE_ENTITIES,"")!=p)
        return application_fail(error,QA_ERROR_ARGUMENT,"QW physical retirement lost its actual source owner");
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_retain(p,&source,error)) return false;
    uint32_t slot;const application_player_record *row;
    bool okay=binding(&source,actor,&slot,&row,error);
    if (okay && !row->spectator && !row->source_begin_pending && !row->deferred) {
        qa_application_network_qw_client value;
        okay=client_read(&source,actor,&value,error);
        if (okay) {
            /* Native ID1 QW entity items occupy the low 28 bits. Serverflags
             * belong to the live world and are merged by each stats read. */
            uint32_t items=(uint32_t)(int32_t)value.stats[15] & UINT32_C(0x0fffffff);
            value.stats[15]=items;
            okay=qa_q1_wire_qw_stats_store(&source.receipt,slot,value.stats,error);
        }
    }
    application_native_q1_wire_end(&source);return okay;
}
bool application_native_q1_qw_client_next(qa_application *app,uint32_t *cursor,bool *present,
    qa_application_network_qw_client *out,qa_error *error) {
    if (!cursor || !present || !out) return application_fail(error,QA_ERROR_ARGUMENT,"Missing native QuakeWorld client inventory");
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_qw_begin(app,&source,error)) return false;
    *present=false;if (!*cursor) *cursor=1;
    bool okay=true;
    while (*cursor<=source.receipt.client_slots) {
        uint32_t slot=(*cursor)++;qa_actor_id actor;
        if (!qa_q1_source_client_actor(source.provider->state.q1,slot-1,&actor)) continue;
        okay=application_native_q1_qw_client(app,actor,out,error);
        if (okay && out->source_slot!=slot) okay=application_fail(error,QA_ERROR_FORMAT,"Native QuakeWorld client inventory changed its physical row");
        if (okay) *present=true;
        break;
    }
    application_native_q1_wire_end(&source);return okay;
}
bool application_native_q1_qw_entity_next(qa_application *app,uint32_t *cursor,bool *present,
    qa_actor_id *actor,qa_application_network_qw_entity *out,qa_error *error) {
    if (!cursor || !present || !actor || !out) return application_fail(error,QA_ERROR_ARGUMENT,"Missing native QuakeWorld entity inventory");
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_qw_begin(app,&source,error)) return false;
    *present=false;if (*cursor<33) *cursor=33;bool okay=true;
    while (*cursor<source.receipt.entity_slots) {
        uint32_t slot=(*cursor)++;qa_actor_id candidate;
        if (!qa_q1_wire_actor_at(&source.receipt,slot,&candidate)) continue;
        qa_application_network_qw_entity value;
        if (!entity(&source,candidate,&value,error)) {okay=false;break;}
        if (value.model == 0) continue;
        *actor=candidate;*out=value;*present=true;break;
    }
    application_native_q1_wire_end(&source);return okay;
}

bool application_native_q1_qw_visible(qa_application *app,qa_actor_id viewer,qa_actor_id target,
    qa_bytes pvs,bool *out,qa_error *error) {
    if (!out) return application_fail(error,QA_ERROR_ARGUMENT,"Missing native QuakeWorld visibility output");
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_qw_begin(app,&source,error)) return false;
    uint32_t slot;const application_player_record *row;
    bool okay=binding(&source,viewer,&slot,&row,error) && qa_actors_get(qa_session_actors(app->session),target);
    qa_q1_source_client_view client;
    if (okay) okay=qa_q1_source_client_read(source.provider->state.q1,viewer,&client);
    uint32_t target_slot;
    bool tracked=okay && row->spectator && client.spectator_track_slot &&
        qa_q1_wire_actor_slot(&source.receipt,target,&target_slot) && target_slot==client.spectator_track_slot;
    if (okay && (qa_actor_id_equal(viewer,target) || tracked)) *out=true;
    else if (okay) {
        qa_linked_body linked;
        if (!qa_world_linked(app->world,target,&linked)) *out=false;
        else okay=qa_world_q1_visible(app->world,target,&linked.absolute_bounds,pvs,out,error);
    }
    if (!okay && error && error->code==QA_OK) application_fail(error,QA_ERROR_NOT_FOUND,"Native QuakeWorld visibility lost its source actor");
    application_native_q1_wire_end(&source);return okay;
}
bool application_native_q1_qw_receives(qa_application *app,qa_actor_id actor,
    const qa_application_protocol_event *event,bool *out,qa_error *error) {
    if (!event || !out) return application_fail(error,QA_ERROR_ARGUMENT,"Missing native QuakeWorld routing output");
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_qw_begin(app,&source,error)) return false;
    uint32_t slot;const application_player_record *row;
    bool okay=binding(&source,actor,&slot,&row,error);*out=false;
    if (okay && event->provider==source.provider->owner && !event->signon) {
        if (event->dialect!=QA_RULESET_QUAKEWORLD) okay=application_fail(error,QA_ERROR_FORMAT,"Native QuakeWorld message changes its source dialect");
        else if (!event->multicast) {
            if (event->destination<0 || event->destination>3) okay=application_fail(error,QA_ERROR_FORMAT,"Native QuakeWorld message destination is invalid");
            else *out=!event->recipient.registry || qa_actor_id_equal(event->recipient,actor);
        } else if (event->destination<0 || event->destination>5 || !qa_vec_finite(event->origin))
            okay=application_fail(error,QA_ERROR_FORMAT,"Native QuakeWorld multicast destination is invalid");
        else {
            int32_t mode=event->destination%3;qa_body_state body;
            if (!mode) *out=true;
            else if (!(okay=qa_world_body_read(app->world,actor,&body,error))) { }
            else {
                qa_vec3 delta=qa_vec_sub(body.origin,event->origin);
                if (mode==1 && qa_vec_dot(delta,delta)<=1024.0f*1024.0f) *out=true;
                else {
                    qa_collision_leaf from,to;qa_collision_geometry *geometry=qa_world_geometry(app->world);
                    okay=qa_collision_point_leaf(geometry,event->origin, QA_LEAF_Q1,&from,error) &&
                        qa_collision_point_leaf(geometry,body.origin, QA_LEAF_Q1,&to,error) &&
                        qa_collision_cluster_visible(geometry,(int32_t)from.cluster,(int32_t)to.cluster,mode==1,out,error);
                }
            }
        }
    }
    application_native_q1_wire_end(&source);return okay;
}
bool application_native_q1_qw_precache(qa_application *app,bool models,const char *names[255],
    size_t *count,qa_error *error) {
    if (!names || !count) return application_fail(error,QA_ERROR_ARGUMENT,"Missing native QuakeWorld precache output");
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_qw_begin(app,&source,error)) return false;
    const qa_string_id *rows=models?source.receipt.models:source.receipt.sounds;
    size_t size=models?source.receipt.model_count:source.receipt.sound_count;bool okay=true;
    const char *values[255]={0};
    for (size_t i=1;i<size;++i) {
        values[i-1]=text(app,rows[i]);
        if (!values[i-1] || !*values[i-1]) {okay=application_fail(error,QA_ERROR_FORMAT,"Native QuakeWorld precache has no actual indexed path");break;}
    }
    if (okay) {memcpy(names,values,sizeof(values));*count=size-1;}
    application_native_q1_wire_end(&source);return okay;
}
qa_vfs *application_native_q1_qw_content(qa_application *app,qa_error *error) {
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_qw_begin(app,&source,error)) return NULL;
    qa_vfs *content=application_native_q1_wire_content(source.provider,error);
    application_native_q1_wire_end(&source);
    if (!content) application_fail(error,QA_ERROR_NOT_FOUND,"Native QuakeWorld source content owner is absent");
    return content;
}
bool application_native_q1_qw_prepare(qa_application *app,qa_actor_id actor,qa_error *error) {
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_qw_begin(app,&source,error)) return false;
    uint32_t slot;const application_player_record *row;
    bool okay=app->operation==APPLICATION_IDLE && binding(&source,actor,&slot,&row,error);
    if (okay && (!row->source_begin_pending || row->deferred))
        okay=application_fail(error,QA_ERROR_ARGUMENT,"Native QuakeWorld Prepare requires its actual inactive source reservation");
    if (okay) {
        /* SV_Spawn clears this actual physical edict before genuine Begin. */
        double cleared[16]={0};
        okay=qa_q1_check_client_eye_clear(source.provider->state.q1,actor,error) &&
            qa_q1_wire_qw_stats_store(&source.receipt,slot,cleared,error) &&
            application_native_q1_wire_client_userinfo(source.provider,actor,error);
    }
    application_native_q1_wire_end(&source);return okay;
}
bool application_native_q1_qw_commands(qa_application *app,const qa_network_command_group *group,qa_error *error) {
    if (!group || !group->commands || !group->count || group->count>20)
        return application_fail(error,QA_ERROR_ARGUMENT,"Missing native QuakeWorld literal command group");
    if (!qa_application_network_controlled(app,group->client,group->seat,group->actor,group->movement,(qa_bytes){0},error)) return false;
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_qw_begin(app,&source,error)) return false;
    uint32_t slot;const application_player_record *row;
    bool okay=binding(&source,group->actor,&slot,&row,error);
    if (okay && (row->source_begin_pending || row->deferred))
        okay=application_fail(error,QA_ERROR_ARGUMENT,"Native QuakeWorld commands require genuine source Begin");
    for (size_t i=0;okay && i<group->count;++i)
        if (group->commands[i].sequence!=group->commands[0].sequence || group->commands[i].kind!=QA_RULESET_QUAKEWORLD)
            okay=application_fail(error,QA_ERROR_ARGUMENT,"Native QuakeWorld command group changes its literal source packet");
    application_native_q1_wire_end(&source);
    return okay && qa_application_control_qw_commands(app,group->actor,group->commands,group->count,error);
}
bool application_native_q1_qw_kill(qa_application *app,qa_actor_id actor,bool *killed,qa_error *error) {
    if (!killed) return application_fail(error,QA_ERROR_ARGUMENT,"Missing native QuakeWorld ClientKill result");
    *killed=false;application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_qw_begin(app,&source,error)) return false;
    uint32_t slot;const application_player_record *row;qa_combat_state combat;
    bool okay=app->operation==APPLICATION_IDLE && binding(&source,actor,&slot,&row,error);
    if (okay && !row->source_begin_pending && !row->deferred && !row->spectator) {
        okay=qa_combat_read(app->combat,actor,&combat,error);
        if (okay && combat.health>0) {
            const char *args[]={"kill"};qa_console *console;qa_cvars *cvars;qa_command_context context;
            bool handled=false;
            okay=application_native_q1_console_at(source.provider,&console,&cvars,&context);
            if (okay) okay=qa_q1_game_console_command(source.provider->state.q1,actor,
                &(qa_command_invocation){.console=console,.context=context,.argc=1,.argv=args,.raw="kill",.args_text=""},&handled,error);
            if (okay && !handled) okay=application_fail(error,QA_ERROR_NOT_FOUND,"Native QuakeWorld source has no actual suicide owner");
            if (okay) *killed=true;
        }
    }
    application_native_q1_wire_end(&source);
    if (!okay && error && error->code==QA_OK) application_fail(error,QA_ERROR_ARGUMENT,"Native QuakeWorld ClientKill lost its idle source client");
    if (!okay) application_fault(app,error);
    return okay;
}
bool application_native_q1_qw_pause(qa_application *app,qa_actor_id actor,qa_buffer *out,bool *changed,qa_error *error) {
    if (!out || out->data || out->size || !changed) return application_fail(error,QA_ERROR_ARGUMENT,"Native QuakeWorld pause requires empty announcement output");
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_qw_begin(app,&source,error)) return false;
    uint32_t slot;const application_player_record *row;qa_q1_source_client_view client;
    bool okay=app->operation==APPLICATION_IDLE && binding(&source,actor,&slot,&row,error) &&
        qa_q1_source_client_read(source.provider->state.q1,actor,&client);
    const qa_cvar_view *policy=qa_q1_source_read(source.provider->state.q1,QA_Q1_SOURCE_PAUSABLE);
    const char *denial=policy && policy->number==0?"Pause not allowed.\n":okay && row->spectator?"Spectators can not pause.\n":NULL;
    const char *suffix=app->q1_paused?" unpaused the game\n":" paused the game\n";
    size_t prefix=okay && !denial?strlen(client.name):0;
    size_t size=denial?strlen(denial):prefix+strlen(suffix);qa_buffer result={0};
    if (okay && (prefix>1395 || size>1395)) okay=application_fail(error,QA_ERROR_FORMAT,"Native QuakeWorld pause announcement exceeds its source extent");
    if (okay) {
        result=(qa_buffer){.data=malloc(size+1),.size=size};
        if (!result.data) okay=application_fail(error,QA_ERROR_MEMORY,"Retaining native QuakeWorld pause announcement");
        else if (denial) memcpy(result.data,denial,size+1);
        else {memcpy(result.data,client.name,prefix);memcpy(result.data+prefix,suffix,size-prefix+1);}
    }
    application_provider *provider=source.provider;
    application_native_q1_wire_end(&source);
    if (okay && !denial) okay=application_q1_pause_set(app,provider,!app->q1_paused,error);
    if (okay) {*out=result;*changed=denial==NULL;} else qa_buffer_free(&result);
    return okay;
}
bool application_native_q1_qw_userinfo(qa_application *app,qa_actor_id actor,const char *raw,qa_error *error) {
    application_native_q1_wire_source source={0};
    if (!raw || !application_native_q1_wire_qw_begin(app,&source,error)) return false;
    uint32_t slot;const application_player_record *row;qa_qw_info info={0};
    bool okay=app->operation==APPLICATION_IDLE && binding(&source,actor,&slot,&row,error) && qa_qw_info_parse(raw,&info,error);
    if (okay) {
        const char *spectator=qa_qw_info_get(&info,"*spectator");
        if ((spectator && !strcmp(spectator,"1"))!=row->spectator)
            okay=application_fail(error,QA_ERROR_ARGUMENT,"Native QuakeWorld userinfo changes its trusted source role");
    }
    char *copies[4]={0};
    if (okay) {
        const char *name=qa_qw_info_get(&info,"name"),*team=qa_qw_info_get(&info,"team"),*skin=qa_qw_info_get(&info,"skin");
        const char *values[]={raw,name?name:"unnamed",team?team:"",skin?skin:""};
        for (size_t i=0;okay && i<4;++i) {
            size_t size=strlen(values[i])+1;copies[i]=malloc(size);
            if (copies[i]) memcpy(copies[i],values[i],size);
            else okay=application_fail(error,QA_ERROR_MEMORY,"Retaining native QuakeWorld source userinfo");
        }
    }
    if (okay) {
        application_player_record *actual=&app->players->records[row-app->players->records];
        free(actual->userinfo);free(actual->name);free(actual->team);free(actual->skin);
        actual->userinfo=copies[0];actual->name=copies[1];actual->team=copies[2];actual->skin=copies[3];
        memset(copies,0,sizeof(copies));
        okay=application_native_q1_wire_client_userinfo(source.provider,actor,error);
        if (!okay) application_fault(app,error);
    }
    for (size_t i=0;i<4;++i) free(copies[i]);
    qa_qw_info_free(&info);application_native_q1_wire_end(&source);return okay;
}

bool application_native_q1_qw_emit(application_provider *p,const qa_builtin_event *event,
    const qa_nq_message *message,qa_actor_id recipient,bool reliable,bool signon,
    const qa_application_protocol_reference *reference,qa_error *error) {
    qa_qw_service service={0};bool multicast=false;int32_t destination=signon?3:recipient.registry?1:reliable?2:0;
    qa_application_protocol_reference mapped;const qa_application_protocol_reference *ref=reference;
    if (reference) mapped=*reference;
    switch (message->op) {
    case QA_NQ_NOP:service.kind=QA_QW_NOP;break;
    case QA_NQ_SETANGLE:service.kind=QA_QW_SET_ANGLE;
        memcpy(service.data.angles,message->data.angles,sizeof(service.data.angles));break;
    case QA_NQ_SETVIEW:service.kind=event->kind==QA_BUILTIN_MUZZLE?QA_QW_MUZZLE_FLASH:QA_QW_SET_VIEW;
        service.data.entity=(uint16_t)message->data.value;
        if (event->kind==QA_BUILTIN_MUZZLE) {multicast=true;destination=2;}
        break;
    case QA_NQ_SOUND: {
        service.kind=QA_QW_SOUND;service.data.sound=message->data.sound;
        const qa_cvar_view *phs=qa_cvars_read(application_native_q1_console_registry(p),p->sv_phs);
        if (!phs || !isfinite(phs->number)) return application_fail(error,QA_ERROR_NOT_FOUND,"Native QuakeWorld sound has no source PHS policy");
        if (service.data.sound.channel>15) return application_fail(error,QA_ERROR_FORMAT,"Native QuakeWorld sound channel exceeds its source range");
        bool global=(service.data.sound.channel&8)!=0 || phs->number==0;
        if (service.data.sound.channel&8) reliable=true;
        service.data.sound.channel&=7;
        qa_physics_properties physics;
        if (qa_q1_game_physics_read(p->state.q1,event->actor,&physics) && physics.solid==QA_PHYSICS_BRUSH) {
            qa_body_state body;
            if (!qa_world_body_read(p->application->world,event->actor,&body,error)) return false;
            vector(service.data.sound.origin,qa_vec_add(body.origin,qa_vec_scale(qa_vec_add(body.bounds.mins,body.bounds.maxs),.5f)));
        }
        if (reference) {mapped.offset=1;ref=&mapped;}
        multicast=true;destination=(global?0:1)+(reliable?3:0);break;
    }
    case QA_NQ_STATICSOUND:service.kind=QA_QW_STATIC_SOUND;service.data.sound=message->data.sound;break;
    case QA_NQ_STOPSOUND:service.kind=QA_QW_STOP_SOUND;
        service.data.stop_sound.entity=message->data.stop_sound.entity;
        service.data.stop_sound.channel=message->data.stop_sound.channel;break;
    case QA_NQ_PRINT:case QA_NQ_CENTERPRINT:case QA_NQ_STUFFTEXT:case QA_NQ_FINALE:
        service.kind=message->op==QA_NQ_PRINT?QA_QW_PRINT:message->op==QA_NQ_CENTERPRINT?QA_QW_CENTER_PRINT:
            message->op==QA_NQ_STUFFTEXT?QA_QW_STUFFTEXT:QA_QW_FINALE;
        service.data.text.value=message->data.text;
        service.data.text.level=event->kind==QA_BUILTIN_MESSAGE && event->code<=3?(uint8_t)event->code:2;break;
    case QA_NQ_LIGHTSTYLE:service.kind=QA_QW_LIGHT_STYLE;
        service.data.light_style.index=message->data.indexed_text.index;
        service.data.light_style.value=message->data.indexed_text.text;break;
    case QA_NQ_FRAGS:service.kind=QA_QW_FRAGS;service.data.score.slot=message->data.indexed.index;
        service.data.score.value=(int16_t)message->data.indexed.value;break;
    case QA_NQ_NAME:case QA_NQ_COLORS: {
        uint32_t slot=message->op==QA_NQ_NAME?message->data.indexed_text.index:message->data.indexed.index;
        qa_actor_id actor;const application_player_record *row=NULL;
        if (qa_q1_source_client_actor(p->state.q1,slot,&actor)) row=record(p->application,actor);
        if (row && !row->userinfo) return application_fail(error,QA_ERROR_NOT_FOUND,"Native QuakeWorld board lost its real source userinfo");
        uint32_t id=row?((uint32_t)(row->remote?row->remote_client.generation:actor.generation)*32u+slot+1u):0;
        int32_t signed_id;memcpy(&signed_id,&id,sizeof(id));
        service.kind=QA_QW_USERINFO;service.data.userinfo.slot=(uint8_t)slot;
        service.data.userinfo.user_id=signed_id;service.data.userinfo.value=row?row->userinfo:"";break;
    }
    case QA_NQ_STATIC:service.kind=QA_QW_STATIC;service.data.baseline=message->data.entity;break;
    case QA_NQ_BASELINE:service.kind=QA_QW_BASELINE;service.data.baseline=message->data.entity;break;
    case QA_NQ_TEMPENTITY:
        service.kind=QA_QW_TEMPORARY_ENTITY;service.data.temporary=message->data.temporary;
        if (service.data.temporary.kind==QA_Q1_TEMP_BEAM && service.data.temporary.type==13)
            return application_fail(error,QA_ERROR_UNSUPPORTED,"Native QuakeWorld beam has no declared source protocol representation");
        if (service.data.temporary.type==2 && event->kind==QA_BUILTIN_IMPACT)
            service.data.temporary.count=byte(event->value);
        else service.data.temporary.count=1;
        multicast=true;destination=event->kind==QA_BUILTIN_EXPLOSION || event->kind==QA_BUILTIN_TELEPORT ||
            service.data.temporary.type==0 || service.data.temporary.type==1?1:2;break;
    case QA_NQ_PARTICLE:
        if (event->kind!=QA_BUILTIN_IMPACT || event->code!=1) return true;
        service.kind=QA_QW_TEMPORARY_ENTITY;
        service.data.temporary=(qa_q1_temp){.kind=QA_Q1_TEMP_POINT,.type=12,
            .count=event->flags&QA_Q1_IMPACT_GROUPED?byte(event->value):1};
        memcpy(service.data.temporary.origin,message->data.particle.origin,sizeof(service.data.temporary.origin));
        multicast=true;destination=2;break;
    case QA_NQ_KILLEDMONSTER:service.kind=QA_QW_KILLED_MONSTER;break;
    case QA_NQ_FOUNDSECRET:service.kind=QA_QW_FOUND_SECRET;break;
    case QA_NQ_CDTRACK:service.kind=QA_QW_CD_TRACK;service.data.byte=message->data.cd.track;break;
    case QA_NQ_SELLSCREEN:service.kind=QA_QW_SELL_SCREEN;break;
    case QA_NQ_INTERMISSION: {
        service.kind=QA_QW_INTERMISSION;bool found=false;
        for (uint32_t i=0;i<32;++i) {
            qa_actor_id actor;qa_body_state body;
            if (!qa_q1_source_client_actor(p->state.q1,i,&actor)) continue;
            const application_player_record *row=record(p->application,actor);
            if (!row || row->source_begin_pending || row->deferred) continue;
            qa_player_state control;
            if (!qa_world_body_read(p->application->world,actor,&body,error) ||
                !qa_application_control_read(p->application,actor,&control)) return false;
            vector(service.data.intermission.origin,body.origin);vector(service.data.intermission.angles,control.view_angles);
            found=true;break;
        }
        if (!found) return application_fail(error,QA_ERROR_NOT_FOUND,"Native QuakeWorld intermission has no actual player camera");
        break;
    }
    default:return application_fail(error,QA_ERROR_UNSUPPORTED,"Native QuakeWorld event has no actual source service");
    }
    uint8_t bytes[8192];qa_net_writer writer;qa_net_writer_init(&writer,bytes,sizeof(bytes),error);
    if (!qa_qw_service_write(&writer,(qa_net_protocol_id){.kind=QA_NET_QW28},&service,NULL)) return false;
    qa_application_protocol_event output={.provider=p->owner,.dialect=QA_RULESET_QUAKEWORLD,.time_ns=event->time_ns,
        .recipient=recipient,.origin=event->kind==QA_BUILTIN_IMPACT && event->flags&QA_Q1_IMPACT_GROUPED?
            event->end:event->origin,.payload={bytes,qa_net_writer_size(&writer)},
        .references=ref,.reference_count=ref?1:0,.destination=destination,.reliable=reliable,.signon=signon,.multicast=multicast};
    return application_emit_protocol(p,&output,error);
}
static uint8_t byte(double number) {
    return (uint8_t)(uint32_t)qa_source_float_to_i32((float)number);
}
bool application_native_q1_qw_setangle(application_provider *p,qa_actor_id actor,
    qa_vec3 angles,qa_error *error) {
    qa_q1_options options;double elapsed;uint64_t time_ns;
    if (!p || p->kind!=APPLICATION_PROVIDER_Q1 || !p->constructed || !p->attached ||
        p->close_pending || !p->launch || p->launch->selection.clock.kind!=QA_RULESET_QUAKEWORLD ||
        !qa_vec_finite(angles) || !application_native_q1_qw_selected(p->application) ||
        !qa_q1_source_respawn_options_read(p->state.q1,&options,&elapsed,error) ||
        !options.quakeworld || options.program!=QA_Q1_ID1 || options.edition!=QA_Q1_CLASSIC)
        return application_fail(error,QA_ERROR_ARGUMENT,"Native QuakeWorld setangle lost its actual source owner");
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_retain(p,&source,error)) return false;
    uint32_t slot;const application_player_record *row;
    bool okay=source.receipt.client_slots==32 && binding(&source,actor,&slot,&row,error);
    if (okay && (row->source_begin_pending || row->deferred))
        okay=application_fail(error,QA_ERROR_ARGUMENT,"Native QuakeWorld setangle requires genuine source Begin");
    if (okay) okay=qa_q1_game_clock_read(p->state.q1,&time_ns,&elapsed);
    if (okay) {
        qa_nq_message message={.op=QA_NQ_SETANGLE};vector(message.data.angles,angles);
        qa_builtin_event event={.family=QA_GAME_Q1,.provider=p->owner,.actor=actor,.time_ns=time_ns};
        /* Source consumes fixangle into this frame's unreliable datagram. */
        okay=application_native_q1_qw_emit(p,&event,&message,actor,false,false,NULL,error) &&
            qa_q1_wire_receipt_current(&source.receipt);
    }
    application_native_q1_wire_end(&source);
    if (!okay && error && error->code==QA_OK)
        application_fail(error,QA_ERROR_ARGUMENT,"Native QuakeWorld setangle changed its held source client");
    return okay;
}
bool application_native_q1_qw_flush(qa_application *app,qa_error *error) {
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_qw_begin(app,&source,error)) return false;
    uint64_t time_ns;double elapsed;
    bool okay=app->operation==APPLICATION_IDLE && qa_q1_game_clock_read(source.provider->state.q1,&time_ns,&elapsed) &&
        application_native_q1_wire_observe(app,error) &&
        application_native_q1_source_info_flush(source.provider,error);
    for (uint32_t i=0;okay && i<32;++i) {
        qa_actor_id actor;qa_q1_wire_feedback feedback;
        if (!qa_q1_source_client_actor(source.provider->state.q1,i,&actor)) continue;
        const application_player_record *row=record(app,actor);
        if (!row || row->source_begin_pending || row->deferred) continue;
        if (!qa_q1_wire_feedback_consume(&source.receipt,actor,&feedback)) {
            okay=application_fail(error,QA_ERROR_ARGUMENT,"Native QuakeWorld damage lost its held source client");break;
        }
        if (feedback.armor == 0 && feedback.blood == 0) continue;
        uint8_t bytes[32];qa_net_writer writer;qa_net_writer_init(&writer,bytes,sizeof(bytes),error);
        /* svc_damage and its fixed-coordinate layout are shared by NQ15/QW28.
         * Preserve the source's binary64 center until coordinate conversion. */
        okay=qa_nq_write_damage(&writer,byte(feedback.armor),byte(feedback.blood),feedback.origin);
        if (okay) okay=application_emit_protocol(source.provider,&(qa_application_protocol_event){
            .provider=source.provider->owner,.dialect=QA_RULESET_QUAKEWORLD,.time_ns=time_ns,
            .recipient=actor,.payload={bytes,qa_net_writer_size(&writer)},.destination=1,.reliable=true},error);
    }
    application_native_q1_wire_end(&source);return okay;
}

static bool admission_limits(application_provider *provider,uint32_t clients,uint32_t spectators,
    bool spectator,bool *allowed,qa_error *error) {
    qa_cvars *cvars=application_native_q1_console_registry(provider);
    if (!cvars || !allowed || clients>32 || spectators>32 || clients>32-spectators)
        return application_fail(error,QA_ERROR_ARGUMENT,"Native QuakeWorld admission needs its actual physical client counts");
    const qa_cvar_view *client_limit=qa_q1_source_read(provider->state.q1,QA_Q1_SOURCE_MAXCLIENTS),
        *spectator_limit=qa_q1_source_read(provider->state.q1,QA_Q1_SOURCE_MAXSPECTATORS);
    if (!client_limit || !spectator_limit || !isfinite(client_limit->number) || !isfinite(spectator_limit->number))
        return application_fail(error,QA_ERROR_NOT_FOUND,"Native QuakeWorld admission limits are absent or nonfinite");
    float maximum_clients=client_limit->number,maximum_spectators=spectator_limit->number;
    if (maximum_clients>32) {
        if (!qa_cvars_set_number(cvars,"maxclients",32,error)) return false;
        maximum_clients=32;
    }
    if (maximum_spectators>32) {
        if (!qa_cvars_set_number(cvars,"maxspectators",32,error)) return false;
        maximum_spectators=32;
    }
    if (maximum_spectators+maximum_clients>32) {
        /* SV_DirectConnect deliberately uses this expression, once per attempt. */
        maximum_spectators=32-maximum_spectators+maximum_clients;
        if (!qa_cvars_set_number(cvars,"maxspectators",maximum_spectators,error)) return false;
    }
    *allowed=clients+spectators<32 && (spectator?
        (double)spectators<trunc((double)maximum_spectators):(double)clients<trunc((double)maximum_clients));
    return true;
}
bool application_native_q1_qw_admission(qa_application *app,bool spectator,bool *allowed,qa_error *error) {
    if (!allowed) return application_fail(error,QA_ERROR_ARGUMENT,"Missing native QuakeWorld admission result");
    application_native_q1_wire_source source={0};
    if (!application_native_q1_wire_qw_begin(app,&source,error)) return false;
    uint32_t clients=0,spectators=0;bool okay=true;
    for (uint32_t i=0;i<32;++i) {
        qa_actor_id actor;
        if (!qa_q1_source_client_actor(source.provider->state.q1,i,&actor)) continue;
        const application_player_record *row=NULL;
        /* Source counts every nonfree row, including one awaiting retirement. */
        for (size_t j=0;app->players && j<app->players->count;++j)
            if (qa_actor_id_equal(app->players->records[j].actor,actor)) {row=&app->players->records[j];break;}
        if (!row || row->client_slot!=i) {okay=application_fail(error,QA_ERROR_ARGUMENT,"Native QuakeWorld admission lost an occupied physical row");break;}
        if (row->spectator) ++spectators;else ++clients;
    }
    if (okay) okay=admission_limits(source.provider,
        clients,spectators,spectator,allowed,error);
    if (okay && (!qa_q1_wire_receipt_current(&source.receipt) || source.provider->close_pending ||
        source.provider!=application_world_provider(app,QA_ROLE_ENTITIES,"")))
        okay=application_fail(error,QA_ERROR_ARGUMENT,"Native QuakeWorld admission limits retired their held source owner");
    application_native_q1_wire_end(&source);return okay;
}
