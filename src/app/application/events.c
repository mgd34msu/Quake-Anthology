#include "internal.h"
#include "map_travel_private.h"
#include "network_q1_signon.h"
#include "native_q1_wire.h"
#include "equipment_events.h"
#include "equipment_runtime.h"
#include "native_q2_delivery.h"
#include "native_q1_composition.h"
#include "unified_q1_events.h"
#include "unified_q2_events.h"
#include "unified_q2_native_events.h"
#include "guest_q3_weapons_services.h"
#include "guest_native_q2_private.h"
#include "native_q2_protocol_resources.h"
#include "network_q2_private.h"
#include "qa/application_q3_round.h"
#include "qa/network_q1_qw.h"
#include "qa/qc_observation.h"
#include "guest_qc_internal.h"
#include "map_players_private.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool valid_string(const qa_application *application, qa_string_id id)
{
    return id == QA_STRING_NONE ||
           qa_strings_text(qa_session_strings(application->session), id).data != NULL;
}

static bool valid_q1_source_event(qa_application *app,
    const qa_builtin_event *event, qa_error *error)
{
    if (event->family != QA_GAME_Q1 || !event->provider || event->argument_count ||
        !qa_actor_id_equal(event->other, (qa_actor_id){0}) || event->resource)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 source event has invalid source payload");
    if (!application_native_q1_composition_current(app, event->provider,
            QA_MODE_THREEWAVE, error)) return false;
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    uint64_t time_ns;
    double seconds;
    if (!qa_q1_game_clock_read(source->state.q1, &time_ns, &seconds) ||
        event->time_ns != time_ns)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 source event differs from its genuine source clock");
    if (event->kind == QA_BUILTIN_SOURCE_LOG) {
        bool observer;
        if (!event->text)
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Q1 source log has no source action");
        return application_native_q1_composition_player_current(app, event->provider,
            QA_MODE_THREEWAVE, event->actor, &observer, error);
    }
    if (event->kind == QA_BUILTIN_SOURCE_PROMPT || event->kind == QA_BUILTIN_CLEAR_PROMPT) {
        bool observer;
        if ((event->kind == QA_BUILTIN_SOURCE_PROMPT && !event->text) ||
            (event->kind == QA_BUILTIN_CLEAR_PROMPT && (event->text || event->prompt_choice_count)))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q1 source prompt has invalid title or choices");
        return application_native_q1_composition_player_current(app, event->provider,
            QA_MODE_THREEWAVE, event->actor, &observer, error);
    }
    return (qa_actor_id_equal(event->actor, (qa_actor_id){0}) && !event->text &&
            isfinite(event->ctf_capture.total)) ||
        application_fail(error, QA_ERROR_ARGUMENT,
            "CTF capture requires its genuine team total without an addressed actor");
}

static bool valid_event(qa_application *application,
                        const qa_builtin_event *event, qa_error *error)
{
    if (event == NULL || (unsigned)event->kind > QA_BUILTIN_Q2_ENTITY_EVENT ||
        (unsigned)event->family > QA_GAME_Q3 ||
        (event->argument_count != 0 && event->arguments == NULL) ||
        event->argument_count > SIZE_MAX / sizeof(*event->arguments) ||
        (event->prompt_choice_count && !event->prompt_choices) ||
        event->prompt_choice_count > SIZE_MAX / sizeof(*event->prompt_choices) ||
        !valid_string(application, event->resource) ||
        !valid_string(application, event->text) ||
        !qa_vec_finite(event->origin) || !qa_vec_finite(event->end) ||
        !qa_vec_finite(event->direction) || !isfinite(event->volume) ||
        !isfinite(event->attenuation) || !isfinite(event->value))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "gameplay emitted an invalid event");
    if ((unsigned)event->q2_multicast.kind>QA_BUILTIN_Q2_MULTICAST_PHS_LINE ||
        !qa_vec_finite(event->q2_multicast.origin) ||
        (event->q2_multicast.kind!=QA_BUILTIN_Q2_MULTICAST_NONE &&
         (event->family!=QA_GAME_Q2 || !event->provider)))
        return application_fail(error,QA_ERROR_ARGUMENT,"Gameplay event has invalid Source multicast input");
    if (event->kind == QA_BUILTIN_LOG &&
        (event->family != QA_GAME_Q3 || !event->provider ||
         event->text == QA_STRING_NONE || event->argument_count ||
         !qa_actor_id_equal(event->actor, (qa_actor_id){0}) ||
         !qa_actor_id_equal(event->other, (qa_actor_id){0})))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 source log requires provider-owned text");
    if (event->has_muzzle_pose &&
        (event->family != QA_GAME_Q2 || event->kind != QA_BUILTIN_MUZZLE ||
         !event->provider || !qa_actors_get(qa_session_actors(application->session), event->actor) ||
         !qa_vec_finite(event->muzzle_angles) || !isfinite(event->muzzle_scale) || event->muzzle_scale <= 0))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 muzzle pose has no genuine finite Source receipt");
    if (!event->has_muzzle_pose &&
        (event->muzzle_scale != 0.0f || event->muzzle_angles.x != 0.0f || event->muzzle_angles.y != 0.0f || event->muzzle_angles.z != 0.0f))
        return application_fail(error, QA_ERROR_ARGUMENT, "Absent muzzle pose has retained Source fields");
    if (event->kind == QA_BUILTIN_CTF_STATUS &&
        (event->family != QA_GAME_Q1 || !event->provider ||
         !qa_actors_get(qa_session_actors(application->session), event->actor) ||
         event->argument_count || !isfinite(event->ctf_status.red) ||
         !isfinite(event->ctf_status.blue) || !isfinite(event->ctf_status.flags) ||
         !isfinite(event->ctf_status.rune_items)))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "CTF status requires its actual source and addressed actor");
    if ((event->kind == QA_BUILTIN_SOURCE_LOG || event->kind == QA_BUILTIN_CTF_CAPTURE ||
         event->kind == QA_BUILTIN_SOURCE_PROMPT || event->kind == QA_BUILTIN_CLEAR_PROMPT) &&
        !valid_q1_source_event(application, event, error)) return false;
    if (event->kind != QA_BUILTIN_SOURCE_PROMPT && event->prompt_choice_count)
        return application_fail(error, QA_ERROR_ARGUMENT, "Non-prompt event has prompt choices");
    for (size_t i = 0; i < event->prompt_choice_count; ++i)
        if (!event->prompt_choices[i].label || !valid_string(application, event->prompt_choices[i].label))
            return application_fail(error, QA_ERROR_ARGUMENT, "Source prompt has an invalid label alias");
    if (event->kind == QA_BUILTIN_Q1_POWERUP &&
        (event->family != QA_GAME_Q1 || !event->provider || !event->actor.registry ||
         event->other.registry || event->text || event->resource || event->argument_count ||
         event->q1_powerup.power >= QA_Q1_POWER_COUNT || !isfinite(event->q1_powerup.expires)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 powerup requires its actual typed source expiry");
    if (event->family == QA_GAME_Q2 && event->kind == QA_BUILTIN_ITEM && event->code == 0) {
        if (!event->item || !valid_string(application, event->item))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 pickup requires its authored canonical item");
    } else if (event->item)
        return application_fail(error, QA_ERROR_ARGUMENT, "Non-pickup event has a Q2 pickup item");
    if (event->kind == QA_BUILTIN_Q2_PLAYER_ANIMATION || event->kind == QA_BUILTIN_Q2_ENTITY_EVENT) {
        if (event->family != QA_GAME_Q2 || !event->provider ||
            !qa_actors_get(qa_session_actors(application->session), event->actor) ||
            (event->kind == QA_BUILTIN_Q2_PLAYER_ANIMATION && (event->code < 0 || event->code > 2)))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 animation requires its actual source and live actor");
        bool found = false;
        for (const application_provider *source = application->live_providers; source; source = source->next_live)
            if (source->owner == event->provider && source->kind == APPLICATION_PROVIDER_Q2 && !source->close_pending)
                found = true;
        if (!found) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 animation lost its actual native source");
    }
    for (size_t index = 0; index < event->argument_count; ++index) {
        const qa_builtin_message_arg *argument = &event->arguments[index];
        if ((argument->kind == QA_BUILTIN_MESSAGE_STRING &&
             !valid_string(application, argument->value.text)) ||
            (argument->kind == QA_BUILTIN_MESSAGE_NUMBER &&
             !isfinite(argument->value.number)) ||
            (argument->kind != QA_BUILTIN_MESSAGE_STRING &&
             argument->kind != QA_BUILTIN_MESSAGE_NUMBER))
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "gameplay emitted an invalid message argument");
    }
    return true;
}

bool application_event_stream_create(qa_application *app, size_t actors, qa_error *error)
{
    if (actors > SIZE_MAX / 4096 || actors > SIZE_MAX / 4 /
            sizeof(*app->unified_persistent))
        return application_fail(error, QA_ERROR_MEMORY, "Application event load capacity overflows");
    size_t records = actors * 4;
    app->event_ring = qa_event_ring_create(actors * 4096, 16384, records, error);
    if (!app->event_ring) return false;
    app->unified_persistent = calloc(records, sizeof(*app->unified_persistent));
    if (!app->unified_persistent)
        return application_fail(error, QA_ERROR_MEMORY, "Allocating load-sized persistent event slots");
    app->unified_persistent_capacity = records;
    app->event_local_cursor = 1;
    app->event_peer_cursor = UINT64_MAX;
    return true;
}

bool application_event_stream_begin(qa_application *app, qa_application_event_kind kind,
    application_event_write *write, qa_error *error)
{
    *write = (application_event_write){.presentation_before = app->presentation_event_sequence,
        .simulation_before = app->simulation_event_sequence};
    if (!qa_event_ring_begin(app->event_ring, &write->transaction)) return false;
    write->envelope = qa_event_ring_alloc(&write->transaction,
        sizeof(*write->envelope), _Alignof(application_event_envelope), error);
    if (!write->envelope) { qa_event_ring_abort(&write->transaction); return false; }
    *write->envelope = (application_event_envelope){
        .id = qa_event_ring_next(app->event_ring), .kind = kind};
    app->event_write = write;
    return true;
}

void *application_event_stream_alloc(qa_application *app, size_t bytes, size_t alignment,
    qa_error *error)
{
    return qa_event_ring_alloc(&app->event_write->transaction, bytes, alignment, error);
}

void application_event_stream_abort(qa_application *app, application_event_write *write,
    qa_error *error)
{
    bool blocked = write->transaction.blocked;
    app->event_write = NULL;
    app->presentation_event_sequence = write->presentation_before;
    app->simulation_event_sequence = write->simulation_before;
    qa_event_ring_abort(&write->transaction);
    if (blocked && error) *error = (qa_error){0};
}

bool application_event_stream_commit(qa_application *app, application_event_write *write,
    qa_error *error)
{
    if (write->transaction.blocked ||
        !application_unified_persistent_prepare(app, write->envelope, error)) {
        application_event_stream_abort(app, write, error);
        return false;
    }
    uint64_t id = qa_event_ring_commit(&write->transaction, write->envelope);
    app->event_write = NULL;
    if (!id) {
        app->presentation_event_sequence = write->presentation_before;
        app->simulation_event_sequence = write->simulation_before;
        if (error) *error = (qa_error){0};
        return false;
    }
    application_unified_persistent_publish(app, write->envelope);
    for (application_protocol_record *record = write->envelope->protocols; record; record = record->next)
        if (record->event.signon && !application_q1_signon_retain(app, record, error)) return false;
    return true;
}

bool application_event_stream_decline(qa_application *app,const application_event_write *write,
    bool transient,qa_error *error)
{
    if (!transient || !write->transaction.blocked || app->state != QA_APPLICATION_RUNNING)
        return false;
    if (error) *error=(qa_error){0};
    if (!app->event_transient_declines)
        qa_application_feature_report(app,"transient output",&(qa_error){
            .code=QA_ERROR_MEMORY,.message="Output event storage is full; new transient effects are omitted"});
    if (app->event_transient_declines != UINT64_MAX) ++app->event_transient_declines;
    return true;
}

static bool reliable_capacity_report(qa_application *app, qa_error *error)
{
    if (error) *error = (qa_error){0};
    if (!app->event_reliable_declines)
        qa_application_feature_report(app, "reliable output", &(qa_error){
            .code = QA_ERROR_MEMORY, .message = "Output event storage is full; affected client channels will close"});
    if (app->event_reliable_declines != UINT64_MAX) ++app->event_reliable_declines;
    return true;
}

bool application_event_stream_close_recipients(qa_application *app,
    const application_event_write *write, qa_actor_id recipient,
    const qa_application_q2_audience *audience, uint16_t channels, qa_error *error)
{
    bool captured = audience && audience->captured;
    if (!write->transaction.blocked || app->state != QA_APPLICATION_RUNNING ||
        (!recipient.registry && !captured)) return false;
    bool addressed = captured;
    for (size_t i = 0; app->players && i < app->players->count; ++i) {
        application_player_record *player = app->players->records + i;
        bool receives = !captured && qa_actor_id_equal(player->actor, recipient);
        for (size_t j = 0; captured && !receives && j < audience->count; ++j) {
            const qa_application_q2_recipient *target = audience->recipients + j;
            receives = qa_actor_id_equal(player->actor, target->actor) &&
                (!target->has_connection || qa_net_client_id_equal(player->remote_client, target->connection));
        }
        if (receives) {
            player->output_incomplete |= channels;
            addressed = true;
        }
    }
    if (!addressed) return false;
    return reliable_capacity_report(app, error);
}

bool application_event_stream_close_subscribers(qa_application *app,
    const application_event_write *write, qa_error *error)
{
    if (!write->transaction.blocked || app->state != QA_APPLICATION_RUNNING) return false;
    for (size_t i = 0; app->players && i < app->players->count; ++i)
        app->players->records[i].output_incomplete |= QA_APPLICATION_OUTPUT_UNIFIED;
    return reliable_capacity_report(app, error);
}

static uint16_t protocol_channels(const application_provider *provider)
{
    switch (provider->launch->selection.clock.kind) {
    case QA_RULESET_NETQUAKE:
        return (1u << QA_NET_NQ15) | (1u << QA_NET_FITZ666) | (1u << QA_NET_RMQ999);
    case QA_RULESET_QUAKEWORLD:
        return (1u << QA_NET_QW28) | (1u << QA_NET_QW29);
    case QA_RULESET_Q2_CLASSIC: case QA_RULESET_Q2_RERELEASE:
        return (1u << QA_NET_Q2_34) | (1u << QA_NET_R1Q2_35) | (1u << QA_NET_Q2PRO_36) |
            (1u << QA_NET_Q2REPRO_1038) | (1u << QA_NET_Q2KEX_2023) |
            (1u << QA_NET_Q2KEX_DEMO_2022) | (1u << QA_NET_Q2PRIVATE_4038) |
            QA_APPLICATION_OUTPUT_UNIFIED;
    case QA_RULESET_Q3: return 1u << QA_NET_Q3_68;
    }
    return 0;
}

bool application_q1_multicast_receives(application_provider *provider,
    const qa_application_protocol_event *event, qa_actor_id actor, uint32_t slot,
    bool *out, qa_error *error)
{
    int32_t mode = event->destination % 3;
    if (!mode) { *out = true; return true; }
    qa_application *app = provider->application;
    qa_vec3 point;
    if (provider->kind == APPLICATION_PROVIDER_QC) {
        const qa_qc_definition *field = qa_qc_program_find_field(provider->state.qc.program, "origin");
        if (!field || field->type != QA_QC_VECTOR ||
            !qa_qc_actor_observation_vector(provider->state.qc.instance, slot, actor, field->offset, &point, error))
            return false;
    } else {
        qa_body_state body;
        if (!qa_world_body_read(app->world, actor, &body, error)) return false;
        point = body.origin;
    }
    qa_vec3 delta = qa_vec_sub(point, event->origin);
    if (mode == 1 && qa_vec_dot(delta, delta) <= 1024.0f * 1024.0f) { *out = true; return true; }
    qa_collision_leaf from, to;
    qa_collision_geometry *geometry = qa_world_geometry(app->world);
    return qa_collision_point_leaf(geometry, event->origin, QA_LEAF_Q1, &from, error) &&
        qa_collision_point_leaf(geometry, point, QA_LEAF_Q1, &to, error) &&
        qa_collision_cluster_visible(geometry, (int32_t)from.cluster, (int32_t)to.cluster, mode == 1, out, error);
}

static bool protocol_capacity(application_provider *provider,
    const qa_application_protocol_event *event,
    const qa_application_q2_protocol_delivery *delivery,
    const application_event_write *write, qa_error *error)
{
    qa_application *app = provider->application;
    if (event->signon) return false;
    if (!event->reliable) return application_event_stream_decline(app, write, true, error);
    if (!write->transaction.blocked || app->state != QA_APPLICATION_RUNNING) return false;
    uint16_t channels = protocol_channels(provider);
    if (event->recipient.registry || (delivery && delivery->audience.captured))
        return application_event_stream_close_recipients(app, write,
            event->recipient, delivery ? &delivery->audience : NULL, channels, error);
    bool qw = provider->launch->selection.clock.kind == QA_RULESET_QUAKEWORLD;
    if ((event->multicast ? !qw || event->destination < 3 || event->destination > 5 : event->destination != 2) ||
        (provider->kind != APPLICATION_PROVIDER_Q1 && provider->kind != APPLICATION_PROVIDER_QC))
        return false;
    for (size_t i = 0; app->players && i < app->players->count; ++i) {
        application_player_record *player = app->players->records + i;
        bool receives = false;
        uint32_t source_slot = 0;
        if (provider->kind == APPLICATION_PROVIDER_QC) {
            const struct application_qc_state *engine = provider->state.qc.engine;
            for (uint32_t slot = 1; slot <= engine->max_clients; ++slot) {
                const application_qc_client *client = engine->clients + slot;
                if (client->connected && qa_actor_id_equal(client->actor, player->actor) &&
                    (!qw || client->spawned)) {
                    receives = true;
                    source_slot = slot;
                    break;
                }
            }
        } else {
            receives = qa_q1_native_client_slot_prepared(provider->state.q1, player->actor, &source_slot, NULL) &&
                (!qw || !player->source_begin_pending);
        }
        if (receives && event->multicast &&
            !application_q1_multicast_receives(provider, event, player->actor, source_slot, &receives, error))
            return false;
        if (receives) player->output_incomplete |= channels;
    }
    return reliable_capacity_report(app, error);
}

const application_event_envelope *application_event_stream_at(const qa_application *app,
    uint64_t id)
{
    return app && app->event_ring ? qa_event_ring_at(app->event_ring, id) : NULL;
}

static bool valid_arguments(const qa_application *application,
                            const qa_builtin_message_arg *arguments,
                            size_t count, qa_error *error)
{
    if ((count != 0 && arguments == NULL) ||
        count > SIZE_MAX / sizeof(*arguments))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "map event has invalid message arguments");
    for (size_t index = 0; index < count; ++index) {
        const qa_builtin_message_arg *argument = &arguments[index];
        if ((argument->kind == QA_BUILTIN_MESSAGE_STRING &&
             !valid_string(application, argument->value.text)) ||
            (argument->kind == QA_BUILTIN_MESSAGE_NUMBER &&
             !isfinite(argument->value.number)) ||
            (argument->kind != QA_BUILTIN_MESSAGE_STRING &&
             argument->kind != QA_BUILTIN_MESSAGE_NUMBER))
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "map event has an invalid message argument");
    }
    return true;
}

bool application_emit_q2_map(application_provider *provider,
                             const qa_q2_map_event *event, qa_error *error)
{
    qa_application *application =
        provider == NULL ? NULL : provider->application;
    if (application == NULL || event == NULL ||
        application->destroy_requested || application->session == NULL ||
        (unsigned)event->kind > QA_Q2_MAP_HELP_COMPUTER ||
        !valid_string(application, event->text) ||
        !valid_string(application, event->resource) ||
        !qa_vec_finite(event->origin) || !qa_vec_finite(event->direction) ||
        !qa_vec_finite(event->color) || !qa_vec_finite(event->fog.color) ||
        !qa_vec_finite(event->fog.start_color) ||
        !qa_vec_finite(event->fog.end_color) || !isfinite(event->value) ||
        !isfinite(event->duration) || !isfinite(event->radius) ||
        !isfinite(event->alpha) || !isfinite(event->intensity) ||
        !isfinite(event->fade_start) || !isfinite(event->fade_end) ||
        !isfinite(event->cone_cosine) || !isfinite(event->fog.density) ||
        !isfinite(event->fog.sky_factor) ||
        !isfinite(event->fog.start_distance) ||
        !isfinite(event->fog.end_distance) ||
        !isfinite(event->fog.falloff) ||
        !isfinite(event->fog.height_density))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "gameplay emitted an invalid Q2 map event");
    if (!valid_arguments(application, event->arguments,
                         event->argument_count, error))
        return false;
    if ((event->level_count && !event->levels) || event->level_count > QA_Q2_CAMPAIGN_LEVEL_LIMIT ||
        (event->kind != QA_Q2_MAP_END_UNIT && (event->level_count || event->button_time_ns)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 map event has an invalid unit report");
    for (size_t i = 0; i < event->level_count; ++i)
        if (!event->levels[i].map || !valid_string(application, event->levels[i].map) ||
            !valid_string(application, event->levels[i].name) || !isfinite(event->levels[i].time_seconds))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 unit report lost its actual level or clock");
    if (event->kind == QA_Q2_MAP_WORLD_TEXT &&
        !application_unified_world_text_emit(application, provider->owner, event, error)) return false;
    if (event->kind == QA_Q2_MAP_AUTOSAVE &&
        !application_map_autosave_request(application,error)) return false;
    qa_application_q2_audience audience={0},retained={0};
    if (event->kind==QA_Q2_MAP_STEAM || event->kind==QA_Q2_MAP_FORCE_WALL) {
        qa_vec3 multicast_origin=event->origin;
        if (event->kind==QA_Q2_MAP_FORCE_WALL &&
            (provider->kind!=APPLICATION_PROVIDER_Q2 ||
             !qa_q2_force_wall_multicast_origin(provider->state.q2,event->actor,&multicast_origin)))
            return application_fail(error,QA_ERROR_ARGUMENT,"Forcewall delivery lost its genuine spawn midpoint");
        if (!application_native_q2_delivery_capture(provider,
                &(qa_builtin_q2_multicast){QA_BUILTIN_Q2_MULTICAST_PVS,multicast_origin},
                (qa_vec3){0},&audience,error)) return false;
    }
    application_event_write write;
    if (!application_event_stream_begin(application, QA_APPLICATION_EVENT_Q2_MAP, &write, error)) {
        application_native_q2_delivery_dispose(&audience);
        return false;
    }
    bool ready = application_native_q2_delivery_retain(application, &audience, &retained, error);
    application_native_q2_delivery_dispose(&audience);
    if (!ready) goto abort;
    qa_builtin_message_arg *arguments = NULL;
    if (event->argument_count != 0) {
        size_t bytes = event->argument_count * sizeof(*event->arguments);
        arguments = application_event_stream_alloc(application, bytes,
                                   _Alignof(qa_builtin_message_arg), error);
        if (arguments == NULL)
            goto abort;
        memcpy(arguments, event->arguments, bytes);
    }
    qa_q2_campaign_level *levels = NULL;
    if (event->level_count) {
        size_t bytes = event->level_count * sizeof(*levels);
        levels = application_event_stream_alloc(application, bytes, _Alignof(qa_q2_campaign_level), error);
        if (!levels) goto abort;
        memcpy(levels, event->levels, bytes);
    }
    if (!application_unified_q2_native_map(provider, event, &retained, error)) goto abort;
    application_q2_map_event_record *record =
        &write.envelope->raw.q2_map;
    *record = (application_q2_map_event_record){.audience=retained,.source={
        .provider = provider->owner,
        .time_ns = qa_session_elapsed(application->session),
        .event = *event,
    }};
    record->source.event.arguments = arguments;
    record->source.event.levels = levels;
    return application_event_stream_commit(application, &write, error);
abort:
    application_event_stream_abort(application, &write, error);
    return false;
}

bool application_emit_q3_map(application_provider *provider,
                             const qa_q3_map_event *event, qa_error *error)
{
    qa_application *application =
        provider == NULL ? NULL : provider->application;
    if (application == NULL || event == NULL ||
        application->destroy_requested || application->session == NULL ||
        (unsigned)event->kind > QA_Q3_MAP_AREA_PORTAL ||
        !valid_string(application, event->name) ||
        !valid_string(application, event->text) ||
        !qa_vec_finite(event->origin) || !qa_vec_finite(event->angles) ||
        !qa_vec_finite(event->direction) ||
        !qa_vec_finite(event->destination) || !isfinite(event->value))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "gameplay emitted an invalid Q3 map event");
    application_event_write write;
    if (!application_event_stream_begin(application, QA_APPLICATION_EVENT_Q3_MAP, &write, error))
        return false;
    write.envelope->raw.q3_map = (qa_application_q3_map_event){
        .provider = provider->owner, .time_ns = qa_session_elapsed(application->session), .event = *event};
    return application_event_stream_commit(application, &write, error);
}

static bool record_progress(application_provider *provider,
                            qa_actor_id participant_actor,
                            qa_progress_kind kind, qa_string_id value,
                            qa_error *error)
{
    qa_application *application =
        provider == NULL ? NULL : provider->application;
    if (application == NULL || application->session == NULL || value == 0)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "progress event has no application identity");
    if (application->progress == NULL)
        return true;
    qa_bytes value_text = qa_strings_text(
        qa_session_strings(application->session), value);
    if (value_text.data == NULL || value_text.size == 0)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "progress event has empty content");
    const qa_launch_snapshot *snapshot =
        qa_configuration_current(application->configuration);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    const qa_launch_instance *instance =
        snapshot == NULL || provider->launch == NULL
            ? NULL
            : qa_launch_snapshot_find(
                  snapshot, provider->launch->selection.instance);
    const qa_product *product = qa_catalog_product(
        qa_launch_snapshot_catalog(snapshot),
        instance == NULL ? QA_PRODUCT_NONE : instance->selection.product);
    if (choices == NULL || instance == NULL || product == NULL ||
        product->identity == NULL)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "progress event lost its launch identity");
    const char *prefix = kind == QA_PROGRESS_ACHIEVEMENT
                             ? "achievement:"
                             : "level:";
    size_t prefix_length = strlen(prefix);
    size_t identity_length = strlen(product->identity);
    if (identity_length > SIZE_MAX - prefix_length - 1 ||
        value_text.size >
            SIZE_MAX - prefix_length - identity_length - 1)
        return application_fail(error, QA_ERROR_MEMORY,
                                "progress event identity is too large");
    size_t event_length =
        prefix_length + identity_length + 1 + value_text.size;
    uint8_t *event_text = malloc(event_length);
    if (event_text == NULL)
        return application_fail(error, QA_ERROR_MEMORY,
                                "cannot retain progress event identity");
    memcpy(event_text, prefix, prefix_length);
    memcpy(event_text + prefix_length, product->identity, identity_length);
    event_text[prefix_length + identity_length] = ':';
    memcpy(event_text + prefix_length + identity_length + 1,
           value_text.data, value_text.size);

    const qa_ruleset_descriptor *ruleset = qa_ruleset_read(instance->selection.clock.kind);
    bool ok = true;
    for (size_t index = 0; index < choices->seat_count; ++index) {
        const qa_launch_seat *seat = &choices->seats[index];
        if (!seat->local ||
            (participant_actor.registry != 0 &&
             !qa_actor_id_equal(participant_actor, seat->actor)))
            continue;
        char participant[32];
        int length = snprintf(participant, sizeof(participant),
                              "local-seat:%" PRIu32, seat->id);
        if (length < 0 || (size_t)length >= sizeof(participant)) {
            ok = application_fail(error, QA_ERROR_MEMORY,
                                  "local seat progress identity is too large");
            break;
        }
        qa_progress_event progress = {
            .kind = kind,
            .source = ruleset ? ruleset->family : QA_GAME_Q3,
            .participant = {(const uint8_t *)participant, (size_t)length},
            .event = {event_text, event_length},
        };
        if (kind == QA_PROGRESS_ACHIEVEMENT)
            progress.value.award = value_text;
        else
            progress.value.map = value_text;
        bool inserted;
        if (!qa_player_progress_record(application->progress, &progress,
                                       &inserted, error)) {
            ok = false;
            break;
        }
    }
    free(event_text);
    return ok;
}

bool application_record_achievement(application_provider *provider,
                                    qa_actor_id participant,
                                    qa_string_id award, qa_error *error)
{
    return record_progress(provider, participant, QA_PROGRESS_ACHIEVEMENT,
                           award, error);
}

bool application_record_level(application_provider *provider,
                              qa_string_id map, qa_error *error)
{
    return record_progress(provider, (qa_actor_id){0},
                           QA_PROGRESS_LEVEL_COMPLETED, map, error);
}

static bool transient_event(const qa_builtin_event *event)
{
    switch (event->kind) {
    case QA_BUILTIN_SOUND: return (event->flags & 1u) == 0;
    case QA_BUILTIN_PARTICLES: case QA_BUILTIN_IMPACT: case QA_BUILTIN_EXPLOSION:
    case QA_BUILTIN_MUZZLE: case QA_BUILTIN_TRAIL:
        return true;
    default: return false;
    }
}

static bool builtin_capacity(qa_application *app, const qa_builtin_event *event,
    const qa_application_q2_audience *audience, const application_event_write *write, qa_error *error)
{
    if (application_event_stream_decline(app, write, transient_event(event), error)) return true;
    if (write->transaction.blocked && app->state == QA_APPLICATION_RUNNING && event->family == QA_GAME_Q1 &&
        (event->kind == QA_BUILTIN_MESSAGE || event->kind == QA_BUILTIN_CENTERPRINT)) {
        application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
        if (source && (source->kind == APPLICATION_PROVIDER_Q1 || source->kind == APPLICATION_PROVIDER_QC)) {
            qa_application_protocol_event packet = {.recipient = event->actor, .origin = event->origin,
                .destination = event->actor.registry ? 1 : 2, .reliable = true};
            if (!protocol_capacity(source, &packet, NULL, write, error)) return false;
        }
    }
    switch (event->kind) {
    case QA_BUILTIN_MESSAGE: case QA_BUILTIN_CENTERPRINT:
    case QA_BUILTIN_SOURCE_PROMPT: case QA_BUILTIN_CLEAR_PROMPT:
        return event->actor.registry || (audience && audience->captured) ?
            application_event_stream_close_recipients(app, write, event->actor, audience,
                QA_APPLICATION_OUTPUT_UNIFIED, error) :
            application_event_stream_close_subscribers(app, write, error);
    default: return false;
    }
}

static bool emit_event(qa_application *application, const qa_builtin_event *event,
    const qa_application_q2_audience *audience, qa_error *error)
{
    application_event_write write;
    if (!application_event_stream_begin(application, QA_APPLICATION_EVENT_BUILTIN, &write, error))
        return builtin_capacity(application,event,audience,&write,error);
    application_event_record *record = &write.envelope->raw.builtin;
    record->event = *event;
    if (event->argument_count) {
        size_t bytes = event->argument_count * sizeof(*event->arguments);
        qa_builtin_message_arg *arguments = application_event_stream_alloc(application, bytes,
            _Alignof(qa_builtin_message_arg), error);
        if (!arguments) goto abort;
        memcpy(arguments, event->arguments, bytes);
        record->event.arguments = arguments;
    }
    if (event->prompt_choice_count) {
        size_t bytes = event->prompt_choice_count * sizeof(*event->prompt_choices);
        qa_builtin_prompt_choice *choices = application_event_stream_alloc(application, bytes,
            _Alignof(qa_builtin_prompt_choice), error);
        if (!choices) goto abort;
        memcpy(choices, event->prompt_choices, bytes);
        record->event.prompt_choices = choices;
    }
    if (!application_native_q2_delivery_retain(application, audience, &record->q2_audience, error) ||
        !application_native_q1_wire_emit(application, event, error) ||
        !application_unified_q1_event(application, event, (qa_actor_id){0}, error) ||
        !application_unified_q2_native_builtin(application, event, audience, error)) goto abort;
    return application_event_stream_commit(application, &write, error) ||
        builtin_capacity(application,event,audience,&write,error);
abort:
    application_event_stream_abort(application, &write, error);
    return builtin_capacity(application,event,audience,&write,error);
}

bool application_emit(void *opaque, const qa_builtin_event *event, qa_error *error)
{
    qa_application *application=opaque;
    qa_application_q2_audience audience={0};
    if (!application || application->destroy_requested || !application->session)
        return application_fail(error,QA_ERROR_ARGUMENT,"Gameplay event has no live application owner");
    if (!valid_event(application,event,error)) return false;
    if (event->q2_multicast.kind!=QA_BUILTIN_Q2_MULTICAST_NONE) {
        application_provider *source=NULL;
        for (application_provider *provider=application->live_providers;provider;provider=provider->next_live)
            if (provider->owner==event->provider && provider->constructed && provider->attached && !provider->close_pending) {
                source=provider; break;
            }
        if (!application_native_q2_delivery_capture(source,&event->q2_multicast,event->end,
                &audience,error)) return false;
    }
    bool ok=application_q3_weapons_services_q2_muzzle(application,event,error) &&
        emit_event(application,event,&audience,error);
    application_native_q2_delivery_dispose(&audience);
    return ok;
}

bool application_q1_music_cue(qa_application *application, bool fresh, qa_error *error)
{
    application_provider *source = application_world_provider(application, QA_ROLE_ENTITIES, "");
    if (!source || !source->product || source->product->family != QA_GAME_Q1) return true;
    if (!fresh) for (size_t i = 0; i < application->unified_persistent_count; ++i) {
        const application_unified_event_record *row = &application->unified_persistent[i].event;
        const qa_unified_presentation_payload *payload = row->presentation;
        if (row->provider != source->owner || row->recipient.registry ||
            !payload || payload->kind != QA_UNIFIED_PRESENTATION_BUILTIN) continue;
        const qa_builtin_event *event = &payload->value.builtin;
        if (event->family == QA_GAME_Q1 && event->kind == QA_BUILTIN_EFFECT &&
            event->resource && !strcmp(qa_strings_cstr(qa_session_strings(application->session), event->resource), "music")) return true;
    }
    uint8_t cd_track;
    if (source->kind == APPLICATION_PROVIDER_Q1) {
        qa_q1_wire_receipt receipt = {0}; qa_q1_wire_world world;
        if (!qa_q1_wire_read_begin(source->state.q1, &receipt, error)) return false;
        bool read = qa_q1_wire_world_read(&receipt, &world);
        qa_q1_wire_read_end(&receipt);
        if (!read) return application_fail(error, QA_ERROR_NOT_FOUND, "Q1 level cue has no authored world sounds");
        cd_track = world.cd_track;
    } else if (source->kind == APPLICATION_PROVIDER_QC) {
        float sounds;
        if (!application_qc_float(source->state.qc.engine, 0, "sounds", &sounds, error)) return false;
        cd_track = (uint8_t)(uint32_t)qa_source_float_to_i32(sounds);
    } else return application_fail(error, QA_ERROR_UNSUPPORTED, "Q1 level cue has no native or QC Source");
    qa_clock_state clock; qa_string_id music;
    if (!qa_session_clock(application->session, source->owner, &clock))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Q1 level cue has no completed Source clock");
    return qa_strings_intern_cstr(qa_session_strings(application->session), "music", &music, error) &&
        application_emit(application, &(qa_builtin_event){.kind = QA_BUILTIN_EFFECT,
            .family = QA_GAME_Q1, .provider = source->owner, .time_ns = clock.frame.time_ns,
            .resource = music, .code = cd_track, .count = cd_track}, error);
}

static bool event_text(qa_application *application, const char *source,
                        const char **out, qa_error *error)
{
    if (!source) {
        *out = NULL;
        return true;
    }
    size_t length = strlen(source);
    if (length == SIZE_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "application event text overflows");
    char *copy = application_event_stream_alloc(application, length + 1, 1, error);
    if (!copy) return false;
    memcpy(copy, source, length + 1);
    *out = copy;
    return true;
}

bool application_emit_q2_player(application_provider *provider,
                                 const qa_q2_player_event *event, qa_error *error)
{
    qa_application *application = provider ? provider->application : NULL;
    if (!application || !application->session || application->destroy_requested ||
        !event || (unsigned)event->kind > QA_Q2_PLAYER_ALPHA ||
        !qa_vec_finite(event->origin) || !qa_vec_finite(event->direction) ||
        !isfinite(event->damage) || !isfinite(event->alpha) ||
        (event->kind == QA_Q2_PLAYER_SCOREBOARD && event->score_count && !event->scores) ||
        (event->kind == QA_Q2_PLAYER_INVENTORY && event->inventory_count && !event->inventory) ||
        event->score_count > SIZE_MAX / sizeof(qa_q2_score_row) ||
        event->inventory_count > SIZE_MAX / sizeof(qa_inventory_entry))
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q2 player event");
    application_event_write write;
    qa_actor_id recipient = event->kind == QA_Q2_PLAYER_USERINFO ? (qa_actor_id){0} : event->actor;
    uint16_t channels = event->kind == QA_Q2_PLAYER_PRINT ? protocol_channels(provider) : QA_APPLICATION_OUTPUT_UNIFIED;
    if (!application_event_stream_begin(application, QA_APPLICATION_EVENT_Q2_PLAYER, &write, error))
        return application_event_stream_close_recipients(application, &write, recipient, NULL, channels, error);
    const qa_application_network_q2_recipient_view *recipients = NULL;
    size_t recipient_count = 0;
    if (event->kind == QA_Q2_PLAYER_PRINT && provider->q2_recipient_binding &&
        !application_network_q2_print_recipients(provider, event->actor,
            &recipients, &recipient_count, error)) goto abort;
    if (!application_unified_q2_native_player(provider, event, error)) goto abort;
    qa_q2_player_event copied = *event;
    if (!event_text(application, event->text, &copied.text, error) ||
        !event_text(application, event->skin, &copied.skin, error))
        goto abort;
    copied.scores = NULL;
    copied.inventory = NULL;
    if (event->scores && event->score_count) {
        qa_q2_score_row *scores = application_event_stream_alloc(application,
            event->score_count * sizeof(*scores), _Alignof(qa_q2_score_row), error);
        if (!scores) goto abort;
        memcpy(scores, event->scores, event->score_count * sizeof(*scores));
        for (size_t i = 0; i < event->score_count; ++i)
            if (!event_text(application, event->scores[i].name, &scores[i].name, error))
                goto abort;
        copied.scores = scores;
    }
    if (event->inventory && event->inventory_count) {
        qa_inventory_entry *inventory = application_event_stream_alloc(application,
            event->inventory_count * sizeof(*inventory), _Alignof(qa_inventory_entry), error);
        if (!inventory) goto abort;
        memcpy(inventory, event->inventory, event->inventory_count * sizeof(*inventory));
        copied.inventory = inventory;
    }
    write.envelope->raw.q2_player = (qa_application_q2_player_event){
        .provider = provider->owner, .time_ns = qa_session_elapsed(application->session),
        .event = copied, .recipients = recipients, .recipient_count = recipient_count};
    return application_event_stream_commit(application, &write, error) ||
        application_event_stream_close_recipients(application, &write, recipient, NULL, channels, error);
abort:
    application_event_stream_abort(application, &write, error);
    return application_event_stream_close_recipients(application, &write, recipient, NULL, channels, error);
}

static bool retain_protocol_text(qa_application *app, const char **text, qa_error *error)
{
    if (!*text) return true;
    size_t length = strlen(*text);
    char *copy = application_event_stream_alloc(app, length + 1, 1, error);
    if (!copy) return false;
    memcpy(copy, *text, length + 1);
    *text = copy;
    return true;
}

static bool retain_protocol_names(qa_application *app, const char *const **names, size_t count, qa_error *error)
{
    if (!count) return true;
    const char **copy = application_event_stream_alloc(app, count * sizeof(*copy), _Alignof(const char *), error);
    if (!copy) return false;
    memcpy(copy, *names, count * sizeof(*copy));
    for (size_t i = 0; i < count; ++i)
        if (!retain_protocol_text(app, copy + i, error)) return false;
    *names = copy;
    return true;
}

static bool retain_nq_message(qa_application *app, qa_application_protocol_event *event, qa_error *error)
{
    if (!event->nq) return true;
    qa_nq_message *message = application_event_stream_alloc(app, sizeof(*message), _Alignof(qa_nq_message), error);
    if (!message) return false;
    *message = *event->nq;
    event->nq = message;
    switch (message->op) {
    case QA_NQ_PRINT: case QA_NQ_STUFFTEXT: case QA_NQ_CENTERPRINT: case QA_NQ_FINALE: case QA_NQ_CUTSCENE:
    case QA_NQ_SKYBOX: case QA_NQ_BOTCHAT: case QA_NQ_RAWPRINT: case QA_NQ_SERVERVARS: case QA_NQ_ACHIEVEMENT: case QA_NQ_CHAT:
        return retain_protocol_text(app, &message->data.text, error);
    case QA_NQ_LIGHTSTYLE: case QA_NQ_NAME: case QA_NQ_SOCIAL: case QA_NQ_PLAYERINFO:
        return retain_protocol_text(app, &message->data.indexed_text.text, error);
    case QA_NQ_PROMPT:
        return message->data.prompt.operation >= 2 || retain_protocol_text(app, &message->data.prompt.text, error);
    case QA_NQ_SERVERINFO:
        return retain_protocol_text(app, &message->data.serverinfo.level, error) &&
            retain_protocol_names(app, &message->data.serverinfo.models, message->data.serverinfo.model_count, error) &&
            retain_protocol_names(app, &message->data.serverinfo.sounds, message->data.serverinfo.sound_count, error);
    default: return true;
    }
}

static bool retain_qw_service(qa_application *app, qa_application_protocol_event *event, qa_error *error)
{
    if (!event->qw) return true;
    qa_qw_service *service = application_event_stream_alloc(app, sizeof(*service), _Alignof(qa_qw_service), error);
    if (!service) return false;
    *service = *event->qw;
    event->qw = service;
    switch (service->kind) {
    case QA_QW_PRINT: case QA_QW_STUFFTEXT: case QA_QW_CENTER_PRINT: case QA_QW_FINALE:
        return retain_protocol_text(app, &service->data.text.value, error);
    case QA_QW_LIGHT_STYLE:
        return retain_protocol_text(app, &service->data.light_style.value, error);
    case QA_QW_USERINFO:
        return retain_protocol_text(app, &service->data.userinfo.value, error);
    case QA_QW_SERVER_DATA:
        return retain_protocol_text(app, &service->data.server.game_directory, error) &&
            retain_protocol_text(app, &service->data.server.level, error);
    case QA_QW_SET_INFO: case QA_QW_SERVER_INFO:
        return retain_protocol_text(app, &service->data.info.key, error) &&
            retain_protocol_text(app, &service->data.info.value, error);
    case QA_QW_MODEL_LIST: case QA_QW_SOUND_LIST:
        return retain_protocol_names(app, &service->data.list.names, service->data.list.count, error);
    case QA_QW_PACKET_ENTITIES: {
        qa_qw_frame *frame = application_event_stream_alloc(app, sizeof(*frame), _Alignof(qa_qw_frame), error);
        if (!frame) return false;
        *frame = *service->data.packet.frame;
        service->data.packet.frame = frame;
        return true;
    }
    case QA_QW_NAILS: {
        qa_qw_nail *nails = service->data.nails.count ? application_event_stream_alloc(app,
            service->data.nails.count * sizeof(*nails), _Alignof(qa_qw_nail), error) : NULL;
        if (service->data.nails.count && !nails) return false;
        if (service->data.nails.count)
            memcpy(nails, service->data.nails.items, service->data.nails.count * sizeof(*nails));
        service->data.nails.items = nails;
        return true;
    }
    case QA_QW_DOWNLOAD: {
        qa_bytes bytes = service->data.download.bytes;
        uint8_t *copy = bytes.size ? application_event_stream_alloc(app, bytes.size, 1, error) : NULL;
        if (bytes.size && !copy) return false;
        if (bytes.size) memcpy(copy, bytes.data, bytes.size);
        service->data.download.bytes = (qa_bytes){copy, bytes.size};
        return true;
    }
    default: return true;
    }
}

static bool retain_q2_service(qa_application *app, qa_application_protocol_event *event, qa_error *error)
{
    if (!event->q2) return true;
    qa_q2_server_event *service = application_event_stream_alloc(app, sizeof(*service), _Alignof(qa_q2_server_event), error);
    if (!service) return false;
    *service = *event->q2;
    event->q2 = service;
    switch (service->kind) {
    case QA_Q2_SVC_PRINT: case QA_Q2_SVC_CENTERPRINT: case QA_Q2_SVC_COMMAND:
    case QA_Q2_SVC_LAYOUT: case QA_Q2_SVC_ACHIEVEMENT:
        return retain_protocol_text(app, &service->data.print.text, error);
    case QA_Q2_SVC_CONFIGSTRING:
        return retain_protocol_text(app, &service->data.config.value, error);
    default: return true;
    }
}

bool qa_application_protocol_event_encode(qa_application_protocol_event *event, qa_net_writer *writer)
{
    if (event->nq) {
        if (!qa_nq_write(writer, event->encoding_protocol, (qa_nq_options){.standard_quake = event->standard_quake},
                event->nq, NULL, 0)) return false;
    } else if (event->qw) {
        if (!qa_qw_service_write(writer, event->encoding_protocol, event->qw, NULL)) return false;
    } else if (event->q2) {
        qa_q2_codec codec;
        if (!qa_q2_codec_init(&codec, event->encoding_protocol, writer->error) ||
            !qa_q2_server_event_write(&codec, writer, event->q2)) return false;
    } else return true;
    event->payload = (qa_bytes){writer->data, qa_net_writer_size(writer)};
    return true;
}

static bool emit_protocol(application_provider *provider,
    const qa_application_protocol_event *event,
    const qa_application_q2_protocol_delivery *delivery,
    const qa_native_host_message *source, const qa_q2_server_record *typed, qa_error *error)
{
    qa_application *application = provider ? provider->application : NULL;
    if (!application || !application->session || application->destroy_requested || !event ||
        (event->payload.size && !event->payload.data) || !qa_vec_finite(event->origin) ||
        (event->reference_count && !event->references) ||
        event->reference_count > SIZE_MAX / sizeof(*event->references) ||
        (source && ((source->reference_count && !source->references) ||
            source->reference_count > SIZE_MAX / sizeof(qa_application_protocol_reference))) ||
        (event->resource_count && !event->resources) ||
        event->resource_count > SIZE_MAX / sizeof(*event->resources))
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid source protocol event");
    for (size_t i = 0; !event->nq && !event->qw && !event->q2 && i < event->reference_count; ++i)
        if (event->payload.size < 2 || event->references[i].offset > event->payload.size - 2)
            return application_fail(error, QA_ERROR_ARGUMENT, "source protocol reference exceeds payload");
    if (source) for (size_t i = 0; i < source->reference_count; ++i)
        if (event->payload.size < 2 || source->references[i].offset > event->payload.size - 2)
            return application_fail(error, QA_ERROR_ARGUMENT, "source protocol reference exceeds payload");
    for (size_t i = 0; i < event->resource_count; ++i) {
        const qa_application_protocol_resource_reference *resource = event->resources + i;
        if ((unsigned)resource->kind > QA_NATIVE_HOST_IMAGE || !resource->name ||
            resource->record_ordinal >= event->payload.size || resource->resource_key[QA_APPLICATION_RESOURCE_KEY_CAPACITY - 1] ||
            (!resource->resource_key[0] && resource->resource_custody))
            return application_fail(error, QA_ERROR_ARGUMENT, "Source protocol resource lost its immutable precache receipt");
        if (resource->resource_key[0]) {
            const qa_resource *held; const qa_vfs *view; const qa_vfs_acquisition *opening;
            if (!application_unified_event_resource_receipt_read(application, resource->resource_key,
                resource->resource_custody, &held, &view, &opening, error)) return false;
        }
    }
    application_event_write own_write;
    bool joined = application->event_write &&
        application->event_write->envelope->kind == QA_APPLICATION_EVENT_BUILTIN;
    application_event_write *write = joined ? application->event_write : &own_write;
    if (!joined && !application_event_stream_begin(application, QA_APPLICATION_EVENT_PROTOCOL, write, error))
        return protocol_capacity(provider, event, delivery, write, error);
    uint8_t *payload = event->payload.size ? application_event_stream_alloc(application,
        event->payload.size, 1, error) : NULL;
    if (event->payload.size && !payload) goto abort;
    if (event->payload.size) memcpy(payload, event->payload.data, event->payload.size);
    qa_application_protocol_event copied = *event;
    if (!retain_nq_message(application, &copied, error) || !retain_qw_service(application, &copied, error) ||
        !retain_q2_service(application, &copied, error)) goto abort;
    qa_q2_server_record typed_record;
    if (typed) {
        typed_record = *typed;
        typed_record.raw = (qa_bytes){payload, event->payload.size};
        if (copied.q2) typed_record.event = *copied.q2;
        typed = &typed_record;
    }
    application_native_q2_protocol_resources captured = {0};
    if (delivery && !application_native_q2_protocol_resources_capture(provider->state.native.q2_engine,
        (qa_bytes){payload, event->payload.size}, event->references, event->reference_count,
        source, typed, &captured, error)) goto abort;
    qa_application_protocol_reference *references = delivery ? captured.references : event->reference_count ?
        application_event_stream_alloc(application, event->reference_count * sizeof(*references),
                         _Alignof(qa_application_protocol_reference), error) : NULL;
    if (!delivery && event->reference_count && !references) goto abort;
    if (!delivery && event->reference_count)
        memcpy(references, event->references, event->reference_count * sizeof(*references));
    qa_application_protocol_resource_reference *resources = delivery ? captured.rows : event->resource_count ?
        application_event_stream_alloc(application, event->resource_count * sizeof(*resources),
            _Alignof(qa_application_protocol_resource_reference), error) : NULL;
    if (!delivery && event->resource_count && !resources) goto abort;
    for (size_t i = 0; !delivery && i < event->resource_count; ++i) {
        resources[i] = event->resources[i];
        size_t length = strlen(resources[i].name);
        char *name = application_event_stream_alloc(application, length + 1, 1, error);
        if (!name) goto abort;
        memcpy(name, resources[i].name, length + 1); resources[i].name = name;
    }
    copied.event_id = write->envelope->id;
    copied.provider = provider->owner;
    copied.dialect = provider->launch->selection.clock.kind;
    if (provider->kind != APPLICATION_PROVIDER_Q1) {
        qa_clock_state clock;
        if (qa_session_clock(application->session, provider->owner, &clock)) copied.time_ns = clock.frame.time_ns;
        else {
            const struct application_native_q2 *engine=provider->kind==APPLICATION_PROVIDER_NATIVE?
                provider->state.native.q2_engine:NULL;
            bool entering=provider->product && provider->product->family==QA_GAME_Q2 &&
                !provider->constructed && engine && engine->provider==provider &&
                (engine->profile==QA_NATIVE_Q2_GAME_API3 || engine->profile==QA_NATIVE_Q2_GAME_API2023) &&
                engine->prepared && engine->calls && engine->host_constructing &&
                (!delivery || !delivery->audience.captured);
            for (size_t i=0;entering && i<sizeof(engine->clients)/sizeof(*engine->clients);++i)
                if (engine->clients[i].connected) entering=false;
            if (!entering)
                { application_fail(error, QA_ERROR_ARGUMENT, "Source protocol lost its genuine emission clock"); goto abort; }
        }
    }
    copied.payload = (qa_bytes){payload, event->payload.size};
    copied.references = references;
    copied.resources = resources;
    if (delivery) {
        copied.reference_count = captured.reference_count;
        copied.resource_count = captured.count;
    }
    application_protocol_record record = {.event = copied};
    if (delivery) {
        record.q2 = *delivery;
        if (!application_native_q2_delivery_retain(application, &delivery->audience,
                &record.q2.audience, error)) goto abort;
        if (delivery->audience.captured) record.event.time_ns = delivery->audience.source_time_ns;
    }
    application_protocol_record *retained = joined ? application_event_stream_alloc(application,
        sizeof(*retained), _Alignof(application_protocol_record), error) : &write->envelope->raw.protocol;
    if (!retained) goto abort;
    *retained = record;
    if (write->envelope->last_protocol) write->envelope->last_protocol->next = retained;
    else write->envelope->protocols = retained;
    write->envelope->last_protocol = retained;
    if (!application_unified_q2_protocol_event(provider, &retained->event,
            delivery ? &retained->q2 : NULL, typed, error)) goto abort;
    if (joined) return true;
    return application_event_stream_commit(application, write, error) ||
        protocol_capacity(provider, event, delivery, write, error);
abort:
    if (joined) return false;
    application_event_stream_abort(application, write, error);
    return protocol_capacity(provider, event, delivery, write, error);
}

bool application_emit_protocol(application_provider *provider,
    const qa_application_protocol_event *event, qa_error *error)
{ return emit_protocol(provider, event, NULL, NULL, NULL, error); }

bool application_emit_q2_protocol(application_provider *provider,
    const qa_application_protocol_event *event,
    const qa_application_q2_protocol_delivery *delivery,
    const qa_native_host_message *source, const qa_q2_server_record *typed, qa_error *error)
{
    if (!provider || !delivery || !delivery->original ||
        (delivery->profile != QA_NATIVE_Q2_GAME_API3 && delivery->profile != QA_NATIVE_Q2_GAME_API2023) ||
        (delivery->audience.captured && delivery->audience.source != provider->owner))
        return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 protocol lost its actual source receipt");
    return emit_protocol(provider, event, delivery, source, typed, error);
}

uint64_t qa_application_events_first(const qa_application *app)
{ return app && app->event_ring ? qa_event_ring_first(app->event_ring) : 1; }
uint64_t qa_application_events_local_first(const qa_application *app)
{ return app ? app->event_local_cursor : 1; }
uint64_t qa_application_events_next(const qa_application *app)
{ return app && app->event_ring ? qa_event_ring_next(app->event_ring) : 1; }
uint64_t qa_application_events_output_failures(const qa_application *app)
{ return app ? app->event_reliable_declines : 0; }

uint64_t qa_application_protocol_events_generation(const qa_application *app)
{ return app ? app->protocol_events_generation : 0; }

bool qa_application_event_read(const qa_application *app, qa_application_event_cursor *cursor,
    qa_application_event_view *out)
{
    const application_event_envelope *record;
    const application_protocol_record *protocol;
    const application_equipment_event_record *equipment;
    const application_event_view *view;
    if (!cursor->started) {
        record = application_event_stream_at(app, cursor->id);
        if (!record) return false;
        protocol = record->protocols; equipment = record->equipment; view = record->views;
        size_t index = cursor->projection;
        while (protocol && index--) protocol = protocol->next;
        index = cursor->projection;
        while (equipment && index--) equipment = equipment->next;
        index = cursor->projection;
        while (view && index--) view = view->next;
        if (cursor->projection && !protocol && !equipment && !view) return false;
        cursor->record = record; cursor->started = true;
    } else {
        record = cursor->record;
        protocol = cursor->protocol; equipment = cursor->equipment; view = cursor->presentation;
        if (!protocol && !equipment && !view) return false;
    }
    *out = (qa_application_event_view){.kind = record->kind,
        .protocol = protocol ? &protocol->event : NULL,
        .q2_delivery = protocol ? &protocol->q2 : NULL,
        .equipment = equipment ? &equipment->event : NULL,
        .equipment_owner = equipment ? equipment->owner : 0,
        .presentation = view ? view->event.presentation : NULL,
        .presentation_recipient = view ? view->event.recipient : (qa_actor_id){0},
        .presentation_time_ns = view ? view->event.time_ns : 0};
    cursor->protocol = protocol ? protocol->next : NULL;
    cursor->equipment = equipment ? equipment->next : NULL;
    cursor->presentation = view ? view->next : NULL;
    ++cursor->projection;
    switch (record->kind) {
    case QA_APPLICATION_EVENT_BUILTIN:
        out->value.builtin = &record->raw.builtin.event;
        out->q2_audience = &record->raw.builtin.q2_audience;
        break;
    case QA_APPLICATION_EVENT_Q2_MAP:
        out->value.q2_map = &record->raw.q2_map.source;
        out->q2_audience = &record->raw.q2_map.audience;
        break;
    case QA_APPLICATION_EVENT_Q3_MAP: out->value.q3_map = &record->raw.q3_map; break;
    case QA_APPLICATION_EVENT_Q2_PLAYER: out->value.q2_player = &record->raw.q2_player; break;
    case QA_APPLICATION_EVENT_UNIFIED: case QA_APPLICATION_EVENT_PROTOCOL:
    case QA_APPLICATION_EVENT_EQUIPMENT: break;
    }
    return true;
}

static bool clear_events(qa_application *application, qa_error *error)
{
    if (application->protocol_events_generation == UINT64_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "protocol event generation is exhausted");
    if ((application->equipment && !qa_equipment_idle(application->equipment)) ||
        !application_equipment_runtime_idle(application->equipment_runtime))
        return application_fail(error, QA_ERROR_ARGUMENT, "gear event consumption retains a source operation");
    ++application->protocol_events_generation;
    application->event_local_cursor = qa_application_events_next(application);
    uint64_t next = application->event_local_cursor < application->event_peer_cursor ?
        application->event_local_cursor : application->event_peer_cursor;
    qa_event_ring_retire(application->event_ring, next);
    return true;
}

bool qa_application_clear_events(qa_application *application, qa_error *error)
{
    if (application == NULL || application->operation != APPLICATION_IDLE ||
        application->destroy_requested || application->finalizing ||
        application->q3_round_active || application->frame_preparing ||
        application->publication_started)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "event consumption requires an idle application");
    return clear_events(application, error);
}

bool qa_application_q3_round_clear_events(qa_application *application,
    qa_actor_owner source, qa_error *error)
{
    if (!application || !application->q3_round_active || !application->frame_preparing)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 round event consumption requires its retained driver cut");
    return qa_application_q3_round_callback_ready(application, source, error) &&
        clear_events(application, error);
}
