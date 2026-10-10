#include "native_q1_spectator.h"
#include "map_players_private.h"
#include "native_q1_console.h"
#include "native_q1_wire.h"
#include "native_q1_wire_qw.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q1_source_entities.h"
#include <limits.h>
#include <stdio.h>

typedef struct spectator_call {
    application_provider *source;
    qa_q1_game_operation operation;
    qa_actor_id actor;
    uint32_t slot;
    bool postthink;
    bool ordinary;
} spectator_call;

static bool current(spectator_call *call, qa_error *error)
{
    application_provider *source = call->source;
    qa_application *app = source->application;
    if ((app->operation != APPLICATION_CONFIGURING && app->operation != APPLICATION_ADVANCING &&
         app->operation != APPLICATION_PERSISTING && app->operation != APPLICATION_IDLE) ||
        (app->operation == APPLICATION_IDLE && application_native_q1_console_idle(source) &&
         application_native_q1_wire_idle(source)))
        return application_fail(error, QA_ERROR_ARGUMENT, "QW spectator lost its admitted source boundary");
    if (app->destroy_requested || app->finalizing || !app->players ||
        app->players->map_provider != source || !source->constructed ||
        !source->attached || source->close_pending || source->state.q1 != call->operation.game ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != source ||
        !qa_q1_game_operation_live(&call->operation) ||
        !qa_actors_get(qa_session_actors(app->session), call->actor) ||
        call->actor.slot >= app->control_capacity)
        return application_fail(error, QA_ERROR_ARGUMENT, "QW spectator lost its actual source operation");
    const application_control_record *control = app->controls + call->actor.slot;
    if (!control->active || control->retired || (control->moving && !call->postthink) ||
        !qa_actor_id_equal(control->player.actor, call->actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "QW spectator lost its reserved movement control");
    uint32_t slot;
    if (!qa_q1_native_client_slot(source->state.q1, call->actor, &slot, error)) return false;
    if (slot != call->slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "QW spectator changed physical client slots");
    for (size_t i = 0; i < app->players->count; ++i) {
        const application_player_record *row = app->players->records + i;
        if (!row->retiring && row->spectator != call->ordinary && row->client_slot == slot &&
            qa_actor_id_equal(row->actor, call->actor)) return true;
    }
    return application_fail(error, QA_ERROR_ARGUMENT, "QW spectator has no trusted reserved roster row");
}

static bool sync_body(spectator_call *call, const qa_body_state *body, qa_error *error)
{
    qa_application *app = call->source->application;
    qa_builtin_motion_change change = {.body = *body, .view_angles = body->angles,
        .reason = QA_BUILTIN_MOTION_RESET};
    return application_control_motion_changed(app, call->actor, &change, error) && current(call, error);
}

static bool print_to(spectator_call *call, qa_actor_id recipient, const char *text,
    int32_t level, qa_error *error)
{
    qa_application *app = call->source->application;
    qa_string_id message;
    uint64_t time;
    double seconds;
    if (!qa_strings_intern_cstr(qa_session_strings(app->session), text, &message, error)) return false;
    if (!qa_q1_game_clock_read(call->source->state.q1, &time, &seconds))
        return application_fail(error, QA_ERROR_ARGUMENT, "QW spectator notice lost its source clock");
    return application_emit(app, &(qa_builtin_event){.kind = QA_BUILTIN_MESSAGE,
        .family = QA_GAME_Q1, .provider = call->source->owner, .time_ns = time,
        .text = message, .actor = recipient, .code = level}, error) && current(call, error);
}

static bool print(spectator_call *call, const char *text, qa_error *error)
{
    return print_to(call, (qa_actor_id){0}, text, 1, error);
}

bool application_native_q1_spectator_begin(application_provider *source, qa_actor_id actor,
    qa_error *error)
{
    if (!source || source->kind != APPLICATION_PROVIDER_Q1 || !source->application ||
        !source->state.q1 || !source->constructed || !source->attached || source->close_pending ||
        source->component.clock.kind != QA_RULESET_QUAKEWORLD)
        return application_fail(error, QA_ERROR_ARGUMENT, "QW spectator Begin needs its native source owner");
    qa_application *app = source->application;
    if ((app->operation != APPLICATION_CONFIGURING && app->operation != APPLICATION_ADVANCING &&
         app->operation != APPLICATION_PERSISTING && app->operation != APPLICATION_IDLE) ||
        (app->operation == APPLICATION_IDLE && application_native_q1_console_idle(source) &&
         application_native_q1_wire_idle(source)))
        return application_fail(error, QA_ERROR_ARGUMENT, "QW spectator Begin has no admitted source boundary");
    spectator_call call = {.source = source, .actor = actor};
    bool okay = qa_q1_game_operation_begin(source->state.q1, &call.operation, error);
    if (okay) okay = qa_q1_native_client_slot(source->state.q1, actor, &call.slot, error) &&
        current(&call, error);
    qa_q1_options options;
    double seconds;
    if (okay) okay = qa_q1_source_respawn_options_read(source->state.q1, &options, &seconds, error);
    if (okay && (!options.quakeworld || options.max_clients != 32))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "QW spectator requires its genuine fixed client table");
    qa_body_state body;
    if (okay) okay = qa_world_body_read(app->world, actor, &body, error) && current(&call, error);
    if (okay) {
        body.origin = qa_v3(0, 0, 0);
        okay = qa_world_body_write(app->world, actor, &body, error) && current(&call, error);
    }
    if (okay) {
        application_control_record *control = app->controls + actor.slot;
        control->player.view_offset = qa_v3(0, 0, 22);
        control->player.view_height = 22;
        okay = sync_body(&call, &body, error);
    }
    qa_q1_source_entity point;
    bool found;
    if (okay) okay = qa_q1_source_entity_first(source->state.q1, "info_player_start", &point, &found, error) &&
        current(&call, error);
    if (okay && found) {
        qa_body_state start;
        if (point.ordinal < 31)
            okay = application_fail(error, QA_ERROR_ARGUMENT, "QW start point overlaps its reserved physical clients");
        if (okay) okay = qa_world_body_read(app->world, point.actor, &start, error) && current(&call, error);
        if (okay) {
            body.origin = start.origin;
            okay = qa_world_body_write(app->world, actor, &body, error) && current(&call, error) &&
                sync_body(&call, &body, error);
        }
    }
    if (okay) okay = qa_world_set_collision(app->world, actor, NULL, error) && current(&call, error) &&
        qa_q1_source_client_observer(source->state.q1, actor, true, error) && current(&call, error);
    if (okay) okay = print(&call, "Spectator ", error);
    qa_q1_source_client_view client;
    if (okay && !qa_q1_source_client_read(source->state.q1, actor, &client))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "QW spectator notice lost its actual client name");
    if (okay) okay = print(&call, client.name, error) && print(&call, " entered the game\n", error) &&
        qa_q1_source_spectator_goal_reset(source->state.q1, actor, error) && current(&call, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

static bool source_call(application_provider *source, qa_actor_id actor,
    spectator_call *call, qa_error *error)
{
    if (!source || source->kind != APPLICATION_PROVIDER_Q1 || !source->application ||
        !source->state.q1 || !source->constructed || !source->attached || source->close_pending ||
        source->component.clock.kind != QA_RULESET_QUAKEWORLD)
        return application_fail(error, QA_ERROR_ARGUMENT, "QW spectator callback has no actual source owner");
    *call = (spectator_call){.source = source, .actor = actor, .postthink = true};
    return qa_q1_game_operation_begin(source->state.q1, &call->operation, error) &&
        qa_q1_native_client_slot(source->state.q1, actor, &call->slot, error) && current(call, error);
}

bool application_native_q1_spectator_track(application_provider *source, qa_actor_id actor,
    bool target_supplied, int32_t slot, qa_error *error)
{
    spectator_call call = {0};
    bool okay = source_call(source, actor, &call, error);
    qa_actor_id target = {0};
    if (okay && target_supplied && slot >= 0 && slot < 32) {
        const struct application_player_roster *roster = source->application->players;
        for (size_t i = 0; i < roster->count; ++i) {
            const application_player_record *row = roster->records + i;
            if (row->client_slot == (uint32_t)slot && !row->retiring && !row->source_begin_pending &&
                !row->deferred && !row->spectator &&
                qa_actors_get(qa_session_actors(source->application->session), row->actor)) {
                target = row->actor;
                break;
            }
        }
    }
    if (okay && target_supplied && !target.registry)
        okay = print_to(&call, actor, "Invalid client to track\n", 2, error);
    if (okay) okay = qa_q1_source_spectator_track(source->state.q1, actor, target, error) && current(&call, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_spectator_disconnect(application_provider *source, qa_actor_id actor,
    qa_error *error)
{
    spectator_call call = {0};
    bool okay = source_call(source, actor, &call, error);
    if (okay) okay = print(&call, "Spectator ", error);
    qa_q1_source_client_view client;
    if (okay && !qa_q1_source_client_read(source->state.q1, actor, &client))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "QW spectator disconnect lost its source name");
    if (okay) okay = print(&call, client.name, error) && print(&call, " left the game\n", error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_client_disconnect(application_provider *source, qa_actor_id actor,
    qa_error *error)
{
    if (!source || source->kind != APPLICATION_PROVIDER_Q1 || !source->application ||
        !source->state.q1 || !source->constructed || !source->attached || source->close_pending ||
        source->component.clock.kind != QA_RULESET_QUAKEWORLD)
        return application_fail(error, QA_ERROR_ARGUMENT, "QW ClientDisconnect needs its actual source owner");
    spectator_call call = {.source = source, .actor = actor, .postthink = true, .ordinary = true};
    bool okay = qa_q1_game_operation_begin(source->state.q1, &call.operation, error);
    if (okay) okay = qa_q1_native_client_slot(source->state.q1, actor, &call.slot, error) &&
        current(&call, error);
    qa_q1_source_client_view client;
    if (okay && !qa_q1_source_client_read(source->state.q1, actor, &client))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "QW ClientDisconnect lost its actual source name");
    if (okay) okay = print_to(&call, (qa_actor_id){0}, client.name, 2, error) &&
        print_to(&call, (qa_actor_id){0}, " left the game with ", 2, error);
    if (okay && !qa_q1_source_client_read(source->state.q1, actor, &client))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "QW ClientDisconnect lost its actual source frags");
    if (okay) {
        char frags[64];
        double value = client.frags;
        if (value >= INT_MIN && value <= INT_MAX && value == trunc(value))
            snprintf(frags, sizeof(frags), "%d", (int)value);
        else snprintf(frags, sizeof(frags), "%5.1f", value);
        okay = print_to(&call, (qa_actor_id){0}, frags, 2, error) &&
            print_to(&call, (qa_actor_id){0}, " frags\n", 2, error) &&
            qa_q1_source_client_disconnect_sound(source->state.q1, actor, error) && current(&call, error);
    }
    qa_application *app = source->application;
    application_provider *character = okay ? application_provider_for(app, actor, QA_ROLE_CHARACTER, "") : NULL;
    bool applied = false;
    if (okay && character && character->kind == APPLICATION_PROVIDER_Q1) {
        if (!character->constructed || !character->attached || character->close_pending)
            okay = application_fail(error, QA_ERROR_ARGUMENT, "QW disconnect lost its selected Q1 character owner");
        else okay = qa_q1_character_disconnect_pose(character->state.q1, actor, &applied, error) &&
            current(&call, error);
        if (okay && application_provider_for(app, actor, QA_ROLE_CHARACTER, "") != character)
            okay = application_fail(error, QA_ERROR_ARGUMENT, "QW disconnect changed its selected character owner");
    }
    if (okay && applied)
        okay = qa_world_set_collision(app->world, actor, NULL, error) && current(&call, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_spectator_postthink(application_provider *source, qa_actor_id actor,
    const qa_q1_input *input, qa_error *error)
{
    if (!source || source->kind != APPLICATION_PROVIDER_Q1 || !source->application ||
        !source->state.q1 || source->component.clock.kind != QA_RULESET_QUAKEWORLD || !input)
        return application_fail(error, QA_ERROR_ARGUMENT, "QW SpectatorThink has no actual source input");
    spectator_call call = {.source = source, .actor = actor, .postthink = true};
    qa_application *app = source->application;
    bool okay = qa_q1_game_operation_begin(source->state.q1, &call.operation, error);
    if (okay) okay = qa_q1_native_client_slot(source->state.q1, actor, &call.slot, error) &&
        current(&call, error) && qa_q1_player_source_input(source->state.q1, actor, input, error) &&
        current(&call, error);
    qa_q1_source_client_view client;
    if (okay && (!qa_q1_source_client_read(source->state.q1, actor, &client) || !client.observer))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "QW SpectatorThink lost its genuine observer");
    if (okay && client.impulse == 1) {
        qa_actor_id goal;
        bool found;
        okay = qa_q1_source_spectator_goal_next(source->state.q1, actor, &goal, &found, error) &&
            current(&call, error);
        if (okay && found) {
            qa_body_state point, body;
            okay = qa_world_body_read(app->world, goal, &point, error) && current(&call, error) &&
                qa_world_body_read(app->world, actor, &body, error) && current(&call, error);
            if (okay) {
                body.origin = point.origin;
                body.angles = point.angles;
                okay = qa_world_body_write(app->world, actor, &body, error) && current(&call, error);
            }
            if (okay) {
                qa_builtin_motion_change change = {.body = body, .view_angles = body.angles,
                    .reason = QA_BUILTIN_MOTION_RESET, .force_view_angles = true};
                okay = application_control_motion_changed(app, actor, &change, error) && current(&call, error) &&
                    qa_world_link(app->world, actor, NULL, error) && current(&call, error) &&
                    application_native_q1_qw_setangle(source, actor, body.angles, error) && current(&call, error);
            }
        }
    }
    if (okay && client.impulse) okay = qa_q1_source_client_consume_impulse(source->state.q1, actor, error) &&
        current(&call, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}
