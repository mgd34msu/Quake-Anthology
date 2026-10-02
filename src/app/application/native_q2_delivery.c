#include "native_q2_delivery.h"
#include "map_players_private.h"
#include "guest_native_q2_private.h"
#include "qa/game_q2_bots.h"
#include "qa/game_q2_combat.h"

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

static bool capture(application_provider *source, qa_vec3 origin,
    const qa_native_host_message *message, qa_application_q2_audience *out, qa_error *error)
{
    qa_application *app=source?source->application:NULL;
    struct application_native_q2 *engine=source && source->kind==APPLICATION_PROVIDER_NATIVE ?
        source->state.native.q2_engine:NULL;
    bool original=message!=NULL;
    bool positioned=!original || message->positioned;
    qa_application_q2_delivery_kind delivery=!original ? QA_APPLICATION_Q2_PVS :
        message->target==QA_NATIVE_HOST_UNICAST ? QA_APPLICATION_Q2_UNICAST :
        message->destination==0 ? QA_APPLICATION_Q2_ALL :
        message->destination==1 ? QA_APPLICATION_Q2_PHS : QA_APPLICATION_Q2_PVS;
    if (!out || !app || !app->session || !app->world || !app->players ||
        !qa_vec_finite(origin) || !source->product || source->product->family!=QA_GAME_Q2 ||
        (original ? (!engine || !engine->calls || !engine->initialized || !engine->map_ready ||
            engine->world!=app->world) :
            (source->kind!=APPLICATION_PROVIDER_Q2 || !source->state.q2 ||
             (qa_session_safe(app->session) && qa_combat_idle(app->combat)))) ||
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
    const qa_launch_snapshot *routing=original?app->routing_snapshot:NULL;
    bool preparing=original && routing && app->frame_preparing;
    qa_clock_state clock;
    qa_q2_combat_rules rules;
    uint64_t now=0,started; bool intermission;
    qa_clock_kind source_kind=original ?
        (engine->profile==QA_NATIVE_Q2_GAME_API3?QA_CLOCK_Q2_CLASSIC:QA_CLOCK_Q2_RERELEASE) :
        source->launch->selection.clock.kind;
    if (!source_current(app,source,world,roster,physical,publication,revision,geometry,routing,preparing) ||
        !geometry || !qa_session_clock(app->session,source->owner,&clock) ||
        clock.frame.provider!=source->owner ||
        clock.frame.kind!=source_kind ||
        (!original && (!qa_q2_combat_rules_read(source->state.q2,&rules) || rules.owner!=source->owner ||
            source_kind!=(rules.edition==QA_Q2_CLASSIC?QA_CLOCK_Q2_CLASSIC:QA_CLOCK_Q2_RERELEASE) ||
            !qa_q2_bot_clock_read(source->state.q2,&now,&intermission,&started,error) ||
            now!=clock.frame.time_ns)) ||
        (original && clock.frame.number &&
            (engine->frame.provider!=clock.frame.provider || engine->frame.kind!=clock.frame.kind ||
             engine->frame.number!=clock.frame.number || engine->frame.time_ns!=clock.frame.time_ns ||
             engine->frame.start_ns!=clock.frame.start_ns || engine->frame.elapsed_ns!=clock.frame.elapsed_ns)))
        return application_fail(error,QA_ERROR_ARGUMENT,"Q2 delivery lost its real world or source clock");
    now=clock.frame.time_ns;
    qa_collision_leaf from={0};
    if (positioned && !qa_collision_point_leaf(geometry,origin,&from,error)) return false;
    bool masked=delivery==QA_APPLICATION_Q2_PVS || delivery==QA_APPLICATION_Q2_PHS;
    bool before_begin=original && (message->reliable || delivery==QA_APPLICATION_Q2_UNICAST);
    uint32_t area_count=qa_collision_area_count(geometry);
    uint8_t *connected=masked && area_count?malloc(area_count):NULL;
    if (masked && area_count && !connected)
        return application_fail(error,QA_ERROR_MEMORY,"Retaining emission-time area connectivity");
    for (uint32_t area=0;masked && area<area_count;++area) {
        bool present;
        if (!qa_collision_areas_connected(geometry,(int32_t)from.area,(int32_t)area,&present,error)) {
            free(connected); return false;
        }
        connected[area]=(uint8_t)present;
    }
    size_t count=roster->count;
    if (count>SIZE_MAX/sizeof(application_player_record) ||
        count>SIZE_MAX/sizeof(qa_application_q2_recipient)) {
        free(connected);
        return application_fail(error,QA_ERROR_MEMORY,"Q2 recipient snapshot extent overflows");
    }
    application_player_record *rows=count?malloc(count*sizeof(*rows)):NULL;
    qa_application_q2_recipient *recipients=count?malloc(count*sizeof(*recipients)):NULL;
    if (count && (!rows || !recipients)) {
        free(rows); free(recipients); free(connected);
        return application_fail(error,QA_ERROR_MEMORY,"Retaining actual Q2 recipient snapshot");
    }
    if (count) memcpy(rows,roster->records,count*sizeof(*rows));
    qa_application_q2_audience receipt={.source=source->owner,.world_source=physical->owner,
        .source_frame=clock.frame,.source_time_ns=now,.map_identity=qa_collision_map_identity(geometry),
        .multicast_origin=origin,.area=positioned?(int32_t)from.area:-1,
        .cluster=positioned?(int32_t)from.cluster:-1,.kind=delivery,.positioned=positioned,
        .recipients=recipients,.captured=true};
    bool ok=true;
    for (size_t i=0;i<count && ok;++i) {
        application_player_record row=rows[i];
        if (row.retiring || (row.source_begin_pending && !before_begin) || !row.actor.registry ||
            !qa_actors_get(qa_session_actors(app->session),row.actor)) continue;
        if (original) {
            uint32_t source_slot=0;
            for (uint32_t slot=1;slot<257;++slot)
                if (qa_actor_id_equal(engine->clients[slot].actor,row.actor)) { source_slot=slot; break; }
            if (!source_slot) continue;
            const application_native_q2_client *client=&engine->clients[source_slot];
            if ((!client->connected && delivery!=QA_APPLICATION_Q2_UNICAST) ||
                client->disconnect_started || (!client->begun && !before_begin) ||
                (delivery==QA_APPLICATION_Q2_UNICAST && !qa_actor_id_equal(row.actor,message->client))) continue;
            qa_native_slot_binding binding;
            if (!source->state.native.host ||
                !qa_native_slot(qa_native_host_instance(source->state.native.host),source_slot,&binding,error) ||
                binding.kind==QA_NATIVE_SLOT_FREE || !qa_actor_id_equal(binding.actor,row.actor)) {
                ok=application_fail(error,QA_ERROR_ARGUMENT,"Original Q2 recipient lost its real source slot"); break;
            }
        }
        if (physical->kind==APPLICATION_PROVIDER_Q2) {
            qa_builtin_player_info player;
            if (!qa_q2_player_projection(physical->state.q2,row.actor,&player) ||
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
                (!client->begun && !before_begin) || client->disconnect_started ||
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
        if (!source_current(app,source,world,roster,physical,publication,revision,geometry,routing,preparing) ||
            roster->count!=count || !roster_row_equal(&roster->records[i],&row) ||
            !qa_actors_get(qa_session_actors(app->session),row.actor) ||
            qa_world_body_storage_serial(world,row.actor)!=serial) {
            ok=application_fail(error,QA_ERROR_ARGUMENT,"Q2 recipient changed during body observation");
            break;
        }
        qa_collision_leaf to; bool pvs=false;
        if (!qa_collision_point_leaf(geometry,body.origin,&to,error) ||
            (masked && !qa_collision_cluster_visible(geometry,(int32_t)from.cluster,(int32_t)to.cluster,
                delivery==QA_APPLICATION_Q2_PHS,&pvs,error))) {
            ok=false; break;
        }
        if (!masked || (to.area<area_count && connected[to.area] && pvs)) {
            for (size_t j=0;j<receipt.count;++j)
                if (qa_actor_id_equal(recipients[j].actor,row.actor)) {
                    ok=application_fail(error,QA_ERROR_FORMAT,"Q2 audience repeats a full physical client");
                    break;
                }
            if (ok) recipients[receipt.count++]=(qa_application_q2_recipient){row.actor,body.origin,
                (int32_t)to.area,(int32_t)to.cluster};
        }
    }
    if (ok && (!source_current(app,source,world,roster,physical,publication,revision,geometry,routing,preparing) ||
            roster->count!=count))
        ok=application_fail(error,QA_ERROR_ARGUMENT,"Q2 delivery retired its source snapshot");
    for (size_t i=0;i<count && ok;++i)
        if (!roster_row_equal(&roster->records[i],&rows[i]))
            ok=application_fail(error,QA_ERROR_ARGUMENT,"Q2 delivery changed its actual client roster");
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
    free(rows); free(connected);
    if (!ok) { free(recipients); return false; }
    *out=receipt;
    return true;
}

bool application_native_q2_delivery_capture(application_provider *source,
    qa_vec3 origin, qa_application_q2_audience *out, qa_error *error)
{ return capture(source,origin,NULL,out,error); }

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
        connected=connected || (!engine->clients[slot].disconnect_started &&
            (message->target==QA_NATIVE_HOST_UNICAST ?
                (message->client.registry && qa_actor_id_equal(engine->clients[slot].actor,message->client)) :
                engine->clients[slot].connected));
    if (connected && !capture(engine->provider,message->origin,message,&result.audience,error)) return false;
    *out=result;
    return true;
}

void application_native_q2_delivery_dispose(qa_application_q2_audience *receipt)
{
    if (!receipt) return;
    free((void *)receipt->recipients);
    *receipt=(qa_application_q2_audience){0};
}

bool application_native_q2_delivery_retain(qa_application *app,
    const qa_application_q2_audience *receipt, qa_application_q2_audience *out, qa_error *error)
{
    qa_application_q2_recipient *copy=NULL;
    if (receipt->count) {
        copy=qa_arena_alloc(&app->event_arena,receipt->count*sizeof(*copy),
            _Alignof(qa_application_q2_recipient),error);
        if (!copy) return false;
        memcpy(copy,receipt->recipients,receipt->count*sizeof(*copy));
    }
    *out=*receipt; out->recipients=copy;
    return true;
}
