#include "native_q2_delivery.h"
#include "map_players_private.h"
#include "guest_native_q2_private.h"
#include "control_frame.h"
#include "qa/game_q2_bots.h"
#include "qa/game_q2_combat.h"
#include "qa/game_q2_wire.h"
#include "qa/application_network_q2.h"

struct application_q2_audience_scratch {
    size_t capacity;
    qa_application_q2_recipient recipients[];
};

bool application_native_q2_delivery_create(qa_application *app, size_t actors, qa_error *error)
{
    if (actors > (SIZE_MAX - sizeof(*app->event_q2_capture)) /
            sizeof(qa_application_q2_recipient))
        return application_fail(error, QA_ERROR_MEMORY, "Q2 audience load capacity overflows");
    app->event_q2_capture = malloc(sizeof(*app->event_q2_capture) +
        actors * sizeof(qa_application_q2_recipient));
    if (!app->event_q2_capture)
        return application_fail(error, QA_ERROR_MEMORY, "Allocating load-sized Q2 audience scratch");
    app->event_q2_capture->capacity = actors;
    return true;
}

void application_native_q2_delivery_destroy(qa_application *app)
{
    free(app->event_q2_capture);
    app->event_q2_capture = NULL;
}

static bool roster_row_equal(const application_player_record *a,
    const application_player_record *b)
{
    return qa_actor_id_equal(a->actor,b->actor) && a->seat==b->seat &&
        a->client_slot==b->client_slot && a->source_slot==b->source_slot &&
        a->retiring==b->retiring && a->source_begin_pending==b->source_begin_pending;
}

static bool source_current(qa_application *app, application_provider *source,
    qa_world *world, struct application_player_roster *roster,
    application_provider *physical, uint64_t publication, uint64_t revision,
    qa_collision_geometry *geometry, const qa_launch_snapshot *routing, bool preparing)
{
    if (app->destroy_requested || app->world!=world || app->players!=roster ||
        app->publication_generation!=publication || app->map_revision!=revision ||
        qa_world_geometry(world)!=geometry || !roster || roster->map_provider!=physical ||
        app->routing_snapshot!=routing || app->frame_preparing!=preparing ||
        (routing ? (app->routing_providers!=app->providers ||
                    app->routing_provider_count!=app->provider_count) :
                   (app->routing_providers || app->routing_provider_count))) return false;
    bool source_present=false, physical_present=false;
    for (size_t i=0;i<app->provider_count;++i) {
        if (app->providers[i]==source) source_present=true;
        if (app->providers[i]==physical) physical_present=true;
    }
    return source_present && physical_present && source->constructed && source->attached &&
        !source->close_pending && physical->constructed && physical->attached &&
        !physical->close_pending && physical->map_bound;
}

static bool positional_point(qa_collision_geometry *geometry, const qa_collision_leaf *viewer,
    qa_vec3 point, bool *out, qa_error *error)
{
    qa_collision_leaf leaf; bool visible;
    *out=false;
    if (!qa_collision_point_leaf(geometry,point, QA_LEAF_COLLISION,&leaf,error) ||
        !qa_collision_cluster_visible(geometry,(int32_t)viewer->cluster,
            (int32_t)leaf.cluster,true,&visible,error)) return false;
    return !visible || qa_collision_areas_connected(geometry,(int32_t)viewer->area,
        (int32_t)leaf.area,out,error);
}

static bool positional_midpoints(qa_collision_geometry *geometry, const qa_collision_leaf *viewer,
    qa_vec3 start, qa_vec3 end, unsigned splits, bool *out, qa_error *error)
{
    qa_vec3 mid=qa_vec_scale(qa_vec_add(start,end),.5f);
    if (!positional_point(geometry,viewer,mid,out,error)) return false;
    if (*out || !splits) return true;
    if (!positional_midpoints(geometry,viewer,start,mid,splits-1,out,error)) return false;
    if (*out) return true;
    return positional_midpoints(geometry,viewer,mid,end,splits-1,out,error);
}

static bool positional_visible(qa_collision_geometry *geometry, qa_vec3 eye,
    qa_vec3 start, qa_vec3 end, bool *out, qa_error *error)
{
    qa_collision_leaf viewer;
    if (!qa_collision_point_leaf(geometry,eye, QA_LEAF_COLLISION,&viewer,error)) return false;
    if (!positional_point(geometry,&viewer,start,out,error)) return false;
    if (*out) return true;
    if (!positional_point(geometry,&viewer,end,out,error)) return false;
    return *out || positional_midpoints(geometry,&viewer,start,end,3,out,error);
}

static bool player_eye(qa_application *app, application_provider *physical, qa_actor_id actor,
    qa_vec3 origin, bool rerelease, qa_vec3 *out, qa_error *error)
{
    qa_vec3 offset;
    if (physical->kind==APPLICATION_PROVIDER_Q2) {
        qa_q2_wire_view view; qa_q2_player_info player;
        if (!qa_q2_player_read(physical->state.q2,actor,&player))
            return application_fail(error,QA_ERROR_NOT_FOUND,"Q2 rail recipient lost its Source player");
        if (!qa_q2_wire_view_read(physical->state.q2,actor,&view,error)) return false;
        offset=view.view.offset;
        if (rerelease) offset.z+=player.view_height;
    } else {
        qa_player_state control; application_client_outputs outputs;
        if (!qa_application_control_read(app,actor,&control))
            return application_fail(error,QA_ERROR_NOT_FOUND,"Q2 rail recipient lost its selected view");
        if (!application_control_outputs(app,actor,&outputs,error)) return false;
        offset=outputs.has_view_offset?outputs.view_offset:control.view_offset;
        if (!outputs.has_view_offset && control.state.kind==QA_RULESET_Q2_RERELEASE)
            offset.z+=control.view_height;
    }
    *out=qa_vec_add(origin,offset);
    return qa_vec_finite(*out) || application_fail(error,QA_ERROR_ARGUMENT,"Q2 rail recipient lost its actual eye");
}

static bool capture(application_provider *source, qa_vec3 origin, qa_vec3 line_end,
    qa_builtin_q2_multicast_kind kind, const qa_native_host_message *message, qa_application_q2_audience *out, qa_error *error)
{
    qa_application *app=source?source->application:NULL;
    struct application_native_q2 *engine=source && source->kind==APPLICATION_PROVIDER_NATIVE ?
        source->state.native.q2_engine:NULL;
    bool original=message!=NULL;
    bool positional=!original && kind==QA_BUILTIN_Q2_MULTICAST_PHS_LINE;
    bool positioned=!original || message->positioned;
    qa_application_q2_delivery_kind delivery=!original ?
        (kind==QA_BUILTIN_Q2_MULTICAST_ALL ? QA_APPLICATION_Q2_ALL :
         kind==QA_BUILTIN_Q2_MULTICAST_PHS || positional ? QA_APPLICATION_Q2_PHS : QA_APPLICATION_Q2_PVS) :
        message->target==QA_NATIVE_HOST_UNICAST ? QA_APPLICATION_Q2_UNICAST :
        message->destination==0 ? QA_APPLICATION_Q2_ALL :
        message->destination==1 ? QA_APPLICATION_Q2_PHS : QA_APPLICATION_Q2_PVS;
    if (!out || !app || !app->session || !app->world || !app->players ||
        !qa_vec_finite(origin) || (positional && !qa_vec_finite(line_end)) ||
        !source->product || source->product->family!=QA_GAME_Q2 ||
        (original ? (!engine || !engine->calls || !engine->initialized || !engine->map_ready ||
            engine->world!=app->world) :
            (source->kind!=APPLICATION_PROVIDER_Q2 || !source->state.q2 ||
             (qa_session_safe(app->session) && qa_combat_idle(app->combat)))) ||
        (!original && (kind<QA_BUILTIN_Q2_MULTICAST_PVS || kind>QA_BUILTIN_Q2_MULTICAST_PHS_LINE)) ||
        (original && message->target==QA_NATIVE_HOST_MULTICAST &&
            (message->destination<0 || message->destination>2)) ||
        ((delivery==QA_APPLICATION_Q2_PVS || delivery==QA_APPLICATION_Q2_PHS) && !positioned))
        return application_fail(error,QA_ERROR_ARGUMENT,
            "Q2 delivery requires its actual executing GAME or combat source");
    qa_world *world=app->world;
    struct application_player_roster *roster=app->players;
    application_provider *physical=application_world_provider(app,QA_ROLE_ENTITIES,"");
    qa_collision_geometry *geometry=qa_world_geometry(world);
    uint64_t publication=app->publication_generation,revision=app->map_revision;
    const qa_launch_snapshot *routing=app->routing_snapshot;
    bool preparing=app->frame_preparing;
    qa_clock_state clock;
    qa_q2_combat_rules rules;
    uint64_t now=0,started; bool intermission;
    qa_ruleset_id source_kind=original ?
        (engine->profile==QA_NATIVE_Q2_GAME_API3?QA_RULESET_Q2_CLASSIC:QA_RULESET_Q2_RERELEASE) :
        source->launch->selection.clock.kind;
    if (!source_current(app,source,world,roster,physical,publication,revision,geometry,routing,preparing) ||
        !geometry || !qa_session_clock(app->session,source->owner,&clock) ||
        clock.frame.provider!=source->owner ||
        clock.frame.kind!=source_kind ||
        (!original && (!qa_q2_combat_rules_read(source->state.q2,&rules) || rules.owner!=source->owner ||
            source_kind!=(rules.edition==QA_Q2_CLASSIC?QA_RULESET_Q2_CLASSIC:QA_RULESET_Q2_RERELEASE) ||
            !qa_q2_bot_clock_read(source->state.q2,&now,&intermission,&started,error) ||
            now!=clock.frame.time_ns)) ||
        (original && clock.frame.number &&
            (engine->frame.provider!=clock.frame.provider || engine->frame.kind!=clock.frame.kind ||
             engine->frame.number!=clock.frame.number || engine->frame.time_ns!=clock.frame.time_ns ||
             engine->frame.start_ns!=clock.frame.start_ns || engine->frame.elapsed_ns!=clock.frame.elapsed_ns)))
        return application_fail(error,QA_ERROR_ARGUMENT,"Q2 delivery lost its real world or source clock");
    now=clock.frame.time_ns;
    bool physical_rerelease=false;
    if (positional && physical->kind==APPLICATION_PROVIDER_Q2) {
        qa_q2_combat_rules physical_rules;
        if (physical==source) physical_rules=rules;
        else if (!qa_q2_combat_rules_read(physical->state.q2,&physical_rules))
            return application_fail(error,QA_ERROR_ARGUMENT,"Q2 rail recipient lost its actual physical edition");
        physical_rerelease=physical_rules.edition==QA_Q2_RERELEASE;
    }
    qa_collision_leaf from={0};
    if (positioned && !qa_collision_point_leaf(geometry,origin, QA_LEAF_COLLISION,&from,error)) return false;
    bool masked=delivery==QA_APPLICATION_Q2_PVS || delivery==QA_APPLICATION_Q2_PHS;
    bool before_begin=original && (message->reliable || delivery==QA_APPLICATION_Q2_UNICAST);
    size_t count=roster->count;
    qa_application_q2_recipient *recipients=app->event_q2_capture->recipients;
    qa_application_q2_audience receipt={.source=source->owner,.world_source=physical->owner,
        .source_frame=clock.frame,.source_time_ns=now,.map_identity=qa_collision_map_identity(geometry),
        .multicast_origin=origin,.area=positioned?(int32_t)from.area:-1,
        .cluster=positioned?(int32_t)from.cluster:-1,.kind=delivery,.positioned=positioned,
        .recipients=recipients,.captured=true};
    bool ok=true;
    for (size_t i=0;i<count && ok;++i) {
        application_player_record row=roster->records[i];
        qa_application_network_q2_recipient_view transport={0};
        bool has_transport=false;
        if (row.retiring || (row.source_begin_pending && !before_begin) || !row.actor.registry ||
            !qa_actors_get(qa_session_actors(app->session),row.actor)) continue;
        if (original) {
            uint32_t source_slot=0;
            for (uint32_t slot=1;slot<257;++slot)
                if (qa_actor_id_equal(engine->clients[slot].actor,row.actor)) { source_slot=slot; break; }
            if (!source_slot) continue;
            const application_native_q2_client *client=&engine->clients[source_slot];
            if ((!client->connected && delivery!=QA_APPLICATION_Q2_UNICAST) ||
                (client->disconnect_started && !(engine->calls && engine->disconnect_client == source_slot)) ||
                (!client->begun && !before_begin) ||
                (delivery==QA_APPLICATION_Q2_UNICAST && !qa_actor_id_equal(row.actor,message->client))) continue;
            qa_native_slot_binding binding;
            if (!source->state.native.host ||
                !qa_native_slot(qa_native_host_instance(source->state.native.host),source_slot,&binding,error) ||
                binding.kind==QA_NATIVE_SLOT_FREE || !qa_actor_id_equal(binding.actor,row.actor)) {
                ok=application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 recipient lost its real source slot"); break;
            }
            if (!qa_application_network_q2_recipient(app,source->owner,row.actor,
                &transport,&has_transport,error)) { ok=false; break; }
        }
        if (physical->kind==APPLICATION_PROVIDER_Q2) {
            qa_builtin_player_info player;
            if (!qa_q2_player_projection(physical->state.q2, row.actor, NULL, &player) ||
                !player.connected || player.slot!=row.client_slot) {
                ok=application_fail(error,QA_ERROR_ARGUMENT,"Q2 recipient lost its physical client binding");
                break;
            }
        } else if (physical->kind==APPLICATION_PROVIDER_NATIVE && physical->state.native.q2_engine) {
            const struct application_native_q2 *physical_engine=physical->state.native.q2_engine;
            qa_native_slot_binding binding;
            if (row.client_slot>=256 || !physical->state.native.host) {
                ok=application_fail(error,QA_ERROR_ARGUMENT,"Q2 audience lost its original physical slot");
                break;
            }
            uint32_t slot=row.client_slot+1;
            const application_native_q2_client *client=physical_engine->clients+slot;
            if ((!client->connected && delivery!=QA_APPLICATION_Q2_UNICAST) ||
                (!client->begun && !before_begin) ||
                (client->disconnect_started && !(physical_engine->calls && physical_engine->disconnect_client == slot)) ||
                !qa_actor_id_equal(client->actor,row.actor) ||
                !qa_native_slot(qa_native_host_instance(physical->state.native.host),slot,&binding,error) ||
                binding.kind==QA_NATIVE_SLOT_FREE || !qa_actor_id_equal(binding.actor,row.actor)) {
                ok=application_fail(error,QA_ERROR_ARGUMENT,"Q2 audience differs from its original physical client");
                break;
            }
        }
        uint64_t serial=qa_world_body_storage_serial(world,row.actor);
        qa_body_state body;
        if (!serial) {
            ok=application_fail(error,QA_ERROR_NOT_FOUND,"Q2 recipient has no actual world body");
            break;
        }
        if (!qa_world_body_read(world,row.actor,&body,error)) { ok=false; break; }
        qa_vec3 eye={0};
        if (positional && !player_eye(app,physical,row.actor,body.origin,physical_rerelease,&eye,error)) {
            ok=false; break;
        }
        if (!source_current(app,source,world,roster,physical,publication,revision,geometry,routing,preparing) ||
            roster->count!=count || !roster_row_equal(&roster->records[i],&row) ||
            !qa_actors_get(qa_session_actors(app->session),row.actor) ||
            qa_world_body_storage_serial(world,row.actor)!=serial) {
            ok=application_fail(error,QA_ERROR_ARGUMENT,"Q2 recipient changed during body observation");
            break;
        }
        qa_collision_leaf to; bool pvs=false,connected=true;
        if (!qa_collision_point_leaf(geometry,body.origin, QA_LEAF_COLLISION,&to,error) ||
            (positional && !positional_visible(geometry,eye,origin,line_end,&pvs,error)) ||
            (!positional && masked &&
                (!qa_collision_areas_connected(geometry,(int32_t)from.area,(int32_t)to.area,&connected,error) ||
                 !qa_collision_cluster_visible(geometry,(int32_t)from.cluster,(int32_t)to.cluster,
                    delivery==QA_APPLICATION_Q2_PHS,&pvs,error)))) {
            ok=false; break;
        }
        if (!masked || (pvs && (positional || connected))) {
            for (size_t j=0;j<receipt.count;++j)
                if (qa_actor_id_equal(recipients[j].actor,row.actor)) {
                    ok=application_fail(error,QA_ERROR_FORMAT,"Q2 audience repeats a full physical client");
                    break;
                }
            if (ok) recipients[receipt.count++]=(qa_application_q2_recipient){
                .actor=row.actor,.origin=body.origin,.area=(int32_t)to.area,.cluster=(int32_t)to.cluster,
                .connection=transport.client,.connection_seat=transport.seat,
                .connection_epoch=transport.connection_epoch,.remote_index=transport.remote_index,
                .has_connection=has_transport};
        }
    }
    if (ok && (!source_current(app,source,world,roster,physical,publication,revision,geometry,routing,preparing) ||
            roster->count!=count))
        ok=application_fail(error,QA_ERROR_ARGUMENT,"Q2 delivery retired its source snapshot");
    qa_clock_state final_clock;
    uint64_t final_now;
    if (ok && (!qa_session_clock(app->session,source->owner,&final_clock) ||
        final_clock.frame.provider!=clock.frame.provider || final_clock.frame.kind!=clock.frame.kind ||
        final_clock.frame.phase!=clock.frame.phase || final_clock.frame.number!=clock.frame.number ||
        final_clock.frame.start_ns!=clock.frame.start_ns || final_clock.frame.elapsed_ns!=clock.frame.elapsed_ns ||
        final_clock.frame.time_ns!=clock.frame.time_ns ||
        (!original && (!qa_q2_bot_clock_read(source->state.q2,&final_now,&intermission,&started,error) ||
            final_now!=now))))
        ok=application_fail(error,QA_ERROR_ARGUMENT,"Q2 delivery changed its genuine source clock");
    if (!ok) return false;
    *out=receipt;
    return true;
}

bool application_native_q2_delivery_capture(application_provider *source,
    const qa_builtin_q2_multicast *multicast, qa_vec3 line_end, qa_application_q2_audience *out, qa_error *error)
{
    if (!multicast) return application_fail(error,QA_ERROR_ARGUMENT,"Q2 delivery has no Source multicast");
    return capture(source,multicast->origin,line_end,multicast->kind,NULL,out,error);
}

bool application_native_q2_message_capture(struct application_native_q2 *engine,
    const qa_native_host_message *message, qa_application_q2_protocol_delivery *out, qa_error *error)
{
    if (!engine || !message || !out || !engine->provider || !engine->calls ||
        engine!=engine->provider->state.native.q2_engine ||
        (engine->profile!=QA_NATIVE_Q2_GAME_API3 && engine->profile!=QA_NATIVE_Q2_GAME_API2023) ||
        !qa_vec_finite(message->origin) ||
        (message->target!=QA_NATIVE_HOST_UNICAST && message->target!=QA_NATIVE_HOST_MULTICAST) ||
        (message->target==QA_NATIVE_HOST_MULTICAST && (message->destination<0 ||
            message->destination>2 || (message->destination!=0 && !message->positioned))))
        return application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 message requires its actual GAME invocation");
    qa_application_q2_protocol_delivery result={.profile=engine->profile,.original=true,
        .dupe_key=engine->profile==QA_NATIVE_Q2_GAME_API2023 &&
            message->target==QA_NATIVE_HOST_UNICAST ? message->flags:0};
    bool connected=false;
    for (uint32_t slot=1;slot<257;++slot)
        connected=connected || ((!engine->clients[slot].disconnect_started ||
            (engine->calls && engine->disconnect_client == slot)) &&
            (message->target==QA_NATIVE_HOST_UNICAST ?
                (message->client.registry && qa_actor_id_equal(engine->clients[slot].actor,message->client)) :
                engine->clients[slot].connected));
    if (connected && !capture(engine->provider,message->origin,(qa_vec3){0},
            QA_BUILTIN_Q2_MULTICAST_NONE,message,&result.audience,error)) return false;
    *out=result;
    return true;
}

void application_native_q2_delivery_dispose(qa_application_q2_audience *receipt)
{
    if (!receipt) return;
    *receipt=(qa_application_q2_audience){0};
}

bool application_native_q2_delivery_retain(qa_application *app,
    const qa_application_q2_audience *receipt, qa_application_q2_audience *out, qa_error *error)
{
    qa_application_q2_recipient *copy=NULL;
    if (receipt->count) {
        copy=application_event_stream_alloc(app,receipt->count*sizeof(*copy),
            _Alignof(qa_application_q2_recipient),error);
        if (!copy) return false;
        memcpy(copy,receipt->recipients,receipt->count*sizeof(*copy));
    }
    *out=*receipt; out->recipients=copy;
    return true;
}
