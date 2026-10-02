#include "native_q1_respawn.h"
#include "map_players_private.h"
#include "native_q1_console.h"
#include "native_q1_wire.h"
#include "native_q1_composition.h"
#include "native_maps.h"
#include "map_travel_private.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q1_maps.h"
#include "qa/horde.h"

#include <math.h>
#include <string.h>

typedef struct source_call {
    qa_application *application;
    application_provider *provider;
    qa_q1_game_operation operation;
    qa_actor_id actor;
} source_call;

static bool current(source_call *call, qa_error *error)
{
    qa_application *app = call->application;
    application_provider *provider = call->provider;
    if (!app || app->destroy_requested || app->finalizing || !app->players ||
        app->players->map_provider != provider ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != provider ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        provider->state.q1 != call->operation.game ||
        !qa_q1_game_operation_live(&call->operation) ||
        !qa_actors_get(qa_session_actors(app->session), call->actor))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 travel lost its actual published source client");
    uint32_t slot;
    if (!qa_q1_native_client_slot(provider->state.q1, call->actor, &slot, error))
        return false;
    for (size_t i = 0; i < app->players->count; ++i) {
        const application_player_record *record = app->players->records + i;
        if (!record->retiring && qa_actor_id_equal(record->actor, call->actor) &&
            record->client_slot == slot) return true;
    }
    return application_fail(error, QA_ERROR_ARGUMENT,
        "Q1 travel actor differs from its actual physical roster slot");
}

static bool begin(application_provider *provider, qa_actor_id actor,
    source_call *call, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q1 || !provider->state.q1 ||
        !provider->application || !provider->constructed || !provider->attached ||
        provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 travel has no native source owner");
    qa_application *app = provider->application;
    if (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING &&
        app->operation != APPLICATION_CONFIGURING && app->operation != APPLICATION_PERSISTING)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 travel has no admitted application phase");
    if (app->operation == APPLICATION_IDLE &&
        application_native_q1_console_idle(provider) && application_native_q1_wire_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Q1 travel requires an admitted application or source command operation");
    *call = (source_call){.application = app, .provider = provider, .actor = actor};
    return qa_q1_game_operation_begin(provider->state.q1, &call->operation, error) &&
        current(call, error);
}

static bool ctf_mode(source_call *call, qa_mode_id *out, qa_error *error)
{
    if (!current(call, error)) return false;
    qa_application *app = call->application;
    qa_mode_view view;
    if (!application_native_q1_composition_mode(app, call->provider, out, error) ||
        !qa_modes_read(app->modes, *out, &view, error) || !current(call, error)) return false;
    return view.rules.source == QA_MODE_THREEWAVE ||
        application_fail(error, QA_ERROR_ARGUMENT, "Q1 travel has no genuine ThreeWave source controller");
}

static bool ctf_read(void *opaque, qa_actor_id actor, qa_q1_travel_ctf *out, qa_error *error)
{
    source_call *call = opaque;
    qa_mode_id mode;
    qa_mode_ctf_view view;
    if (!qa_actor_id_equal(actor, call->actor) || !ctf_mode(call, &mode, error) ||
        !qa_modes_ctf_read(call->application->modes, mode, actor, &view, error) ||
        !current(call, error)) return false;
    *out = (qa_q1_travel_ctf){.last_team = view.last_team, .status = view.status,
        .access = view.access, .start_map = view.start_map, .pregame_over = view.pregame_over,
        .observer = view.observer, .grapple_enabled = false,
        .grapple_disabled = view.grapple_disabled};
    return true;
}

static bool ctf_restore(void *opaque, qa_actor_id actor, double last_team,
    double status, double access, qa_error *error)
{
    source_call *call = opaque;
    qa_mode_id mode;
    return qa_actor_id_equal(actor, call->actor) && ctf_mode(call, &mode, error) &&
        qa_modes_ctf_restore_player(call->application->modes, mode, actor,
            last_team, status, access, error) && current(call, error);
}

static qa_q1_travel_services services(source_call *call)
{
    return (qa_q1_travel_services){.context = call, .ctf_read = ctf_read,
        .ctf_restore = ctf_restore};
}

bool application_native_q1_travel_new(application_provider *provider, qa_actor_id actor,
    qa_q1_travel_state **out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Q1 travel needs its owned output");
    *out = NULL;
    source_call call = {0};
    bool ok = begin(provider, actor, &call, error);
    qa_q1_travel_services callbacks = services(&call);
    if (ok) ok = qa_q1_travel_new(call.operation.game, actor, &callbacks, out, error) &&
        current(&call, error);
    if (!ok) { qa_q1_travel_destroy(*out); *out = NULL; }
    qa_q1_game_operation_end(&call.operation);
    return ok;
}

bool application_native_q1_travel_capture(application_provider *provider, qa_actor_id actor,
    qa_q1_travel_state **out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Q1 travel needs its owned output");
    *out = NULL;
    source_call call = {0};
    bool ok = begin(provider, actor, &call, error);
    qa_item_id selected = 0;
    if (ok) ok = qa_application_weapon_read(call.application, actor, &selected, error) &&
        current(&call, error);
    qa_q1_travel_services callbacks = services(&call);
    if (ok) ok = qa_q1_travel_capture(call.operation.game, actor, selected, &callbacks, out, error) &&
        current(&call, error);
    if (!ok) { qa_q1_travel_destroy(*out); *out = NULL; }
    qa_q1_game_operation_end(&call.operation);
    return ok;
}

bool application_native_q1_travel_admit(application_provider *provider, qa_actor_id actor,
    qa_q1_travel_state *state, qa_error *error)
{
    source_call call = {0};
    bool ok = begin(provider, actor, &call, error);
    qa_q1_travel_services callbacks = services(&call);
    if (ok) ok = qa_q1_travel_admit(call.operation.game, actor, state, &callbacks, error) &&
        current(&call, error);
    qa_q1_game_operation_end(&call.operation);
    return ok;
}

bool application_native_q1_respawn_new(application_provider *provider, qa_actor_id actor,
    qa_error *error)
{
    source_call call = {0};
    qa_q1_travel_state *travel = NULL;
    bool force = false;
    bool okay = begin(provider, actor, &call, error) &&
        application_native_q1_travel_new(provider, actor, &travel, error) && current(&call, error) &&
        qa_q1_source_client_request_respawn(provider->state.q1, actor, &force, error) &&
        current(&call, error) &&
        application_players_native_q1_respawn(call.application, provider, actor, travel, force, error) &&
        current(&call, error);
    qa_q1_travel_destroy(travel);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_request_respawn(void *opaque, qa_actor_id actor, qa_error *error)
{
    application_provider *caller = opaque;
    qa_application *app = caller ? caller->application : NULL;
    application_provider *provider = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    if (!caller || !caller->constructed || !caller->attached || caller->close_pending ||
        (caller != provider && application_provider_for(app, actor, QA_ROLE_CHARACTER, "") != caller))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 respawn request has no actual source caller");
    if (caller != provider)
        return application_players_selected_character_respawn(caller, actor, error);
    source_call call = {0};
    bool okay = begin(provider, actor, &call, error);
    bool handled = false;
    for (size_t i = 0; okay && !handled && app->modes && i < app->mode_count; ++i) {
        qa_mode_id id = app->mode_ids[i];
        qa_mode_view mode;
        okay = qa_modes_read(app->modes, id, &mode, error) && current(&call, error);
        if (!okay || !mode.rules.enabled || mode.rules.kind != QA_MODE_HORDE ||
            application_mode_provider(app, id) != provider) continue;
        okay = qa_modes_horde_request_respawn(app->modes, id, &handled, error) && current(&call, error);
    }
    qa_q1_options options;
    double source_seconds;
    if (okay && !handled)
        okay = qa_q1_source_respawn_options_read(provider->state.q1, &options, &source_seconds, error);
    if (okay && !handled && !options.coop && !options.deathmatch) {
        const char *map = qa_strings_cstr(qa_session_strings(app->session), app->current_map);
        okay = map && *map;
        if (!okay) application_fail(error, QA_ERROR_ARGUMENT, "Q1 restart lost its actual source map");
        if (okay) okay = application_source_queue_map_travel(app,
            &(qa_application_travel_request){.provider = provider->owner, .cause = actor,
                .geometry = app->map_geometry, .expression = map, .carry_players = false}, error) &&
                current(&call, error);
        if (okay) provider->q1_server_flags = qa_q1_game_campaign_flags(provider->state.q1);
        handled = true;
    }
    qa_q1_travel_state *travel = NULL;
    if (okay && !handled && options.coop) {
        for (size_t i = 0; i < app->players->count; ++i) {
            const application_player_record *record = app->players->records + i;
            if (!qa_actor_id_equal(record->actor, actor)) continue;
            if (record->q1_entry) {
                okay = qa_q1_travel_retain(record->q1_entry, error);
                if (okay) travel = record->q1_entry;
            }
            break;
        }
    }
    if (okay && !handled && !travel)
        okay = application_native_q1_travel_new(provider, actor, &travel, error) && current(&call, error);
    bool force = false;
    if (okay && !handled)
        okay = qa_q1_source_client_request_respawn(provider->state.q1, actor, &force, error) &&
            current(&call, error) &&
            application_players_native_q1_respawn(app, provider, actor, travel, force, error) &&
            current(&call, error);
    qa_q1_travel_destroy(travel);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

static bool score_begin(qa_application *app, qa_mode_id mode, qa_actor_id actor,
    bool writing, bool *bound, source_call *call, qa_error *error)
{
    if (!app || !bound)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 score needs its actual owner output");
    *bound = false;
    application_provider *source = application_world_provider(app, QA_ROLE_ENTITIES, "");
    if (!source || source->kind != APPLICATION_PROVIDER_Q1) return true;
    application_provider *policy = application_mode_provider(app, mode);
    if (!policy)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 score lost its actual mode provider");
    bool primary = app->primary_mode_ready && app->primary_mode.slot == mode.slot &&
        app->primary_mode.generation == mode.generation;
    if (policy != source && !primary) return true;
    qa_mode_view view;
    if (!qa_modes_read(app->modes, mode, &view, error)) return false;
    if (!view.rules.enabled) return true;
    if (writing) {
        if (!begin(source, actor, call, error)) return false;
    } else {
        *call = (source_call){.application = app, .provider = source, .actor = actor};
        if (!source->state.q1 || !qa_q1_game_operation_begin(source->state.q1,
            &call->operation, error) || !current(call, error)) return false;
    }
    *bound = true;
    return true;
}

bool application_native_q1_mode_score(void *opaque, qa_mode_id mode, qa_actor_id actor,
    bool *bound, int32_t *out, qa_error *error)
{
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 score needs its value output");
    source_call call = {0};
    bool okay = score_begin(opaque, mode, actor, false, bound, &call, error);
    if (okay && *bound) {
        qa_q1_source_client_view client;
        if (!qa_q1_source_client_read(call.provider->state.q1, actor, &client))
            okay = application_fail(error, QA_ERROR_ARGUMENT, "Native Q1 score lost its actual client record");
        if (okay) {
            double number = isfinite(client.frags) ? fmod(trunc(client.frags), 4294967296.0) : 0;
            if (number < 0) number += 4294967296.0;
            uint32_t bits = (uint32_t)number;
            memcpy(out, &bits, sizeof(*out));
            okay = current(&call, error);
        }
    }
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_mode_set_score(void *opaque, qa_mode_id mode, qa_actor_id actor,
    int32_t score, bool *bound, qa_error *error)
{
    source_call call = {0};
    bool okay = score_begin(opaque, mode, actor, true, bound, &call, error);
    if (okay && *bound)
        okay = qa_q1_source_client_set_score(call.provider->state.q1, actor, (float)score, error) &&
            current(&call, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_mode_add_score(void *opaque, qa_mode_id mode, qa_actor_id actor,
    int32_t amount, bool *bound, qa_error *error)
{
    source_call call = {0};
    bool okay = score_begin(opaque, mode, actor, true, bound, &call, error);
    if (okay && *bound)
        okay = qa_q1_source_client_add_score(call.provider->state.q1, actor, amount, error) &&
            current(&call, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_ctf_suicide_notice(void *opaque, qa_mode_id mode,
    qa_actor_id actor, bool limited, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *provider = app ? application_mode_provider(app, mode) : NULL;
    source_call call = {0};
    bool okay = begin(provider, actor, &call, error);
    qa_mode_id actual;
    qa_string_id text;
    qa_q1_source_client_view client;
    qa_builtin_message_arg argument = {.kind = QA_BUILTIN_MESSAGE_STRING};
    uint64_t time_ns;
    double seconds;
    if (okay) okay = ctf_mode(&call, &actual, error);
    if (okay && (actual.slot != mode.slot || actual.generation != mode.generation))
        okay = application_fail(error, QA_ERROR_ARGUMENT,
            "CTF suicide notice differs from its actual source controller");
    if (okay && !qa_q1_source_client_read(provider->state.q1, actor, &client))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "CTF suicide notice lost its actual client name");
    if (okay) okay = qa_strings_intern_cstr(qa_session_strings(app->session),
        limited ? "$qc_ctf_too_many_suicide" : "$qc_suicides", &text, error) &&
        (limited || qa_strings_intern_cstr(qa_session_strings(app->session), client.name,
            &argument.value.text, error)) && current(&call, error);
    if (okay && !qa_q1_game_clock_read(provider->state.q1, &time_ns, &seconds))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "CTF suicide notice lost its actual source clock");
    if (okay) okay = application_emit(app,
        &(qa_builtin_event){.kind = QA_BUILTIN_MESSAGE, .family = QA_GAME_Q1,
            .provider = provider->owner, .actor = limited ? actor : (qa_actor_id){0},
            .time_ns = time_ns, .text = text, .arguments = limited ? NULL : &argument,
            .argument_count = limited ? 0 : 1, .flags = limited ? 0 : 2u}, error) &&
        current(&call, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_suicide(void *opaque, qa_actor_id actor, qa_error *error)
{
    application_provider *provider = opaque;
    source_call call = {0};
    bool okay = begin(provider, actor, &call, error);
    qa_q1_options options;
    double source_seconds;
    if (okay) okay = qa_q1_source_respawn_options_read(provider->state.q1,
        &options, &source_seconds, error);
    if (okay && options.program == QA_Q1_CTF) {
        qa_mode_id mode;
        bool handled;
        okay = ctf_mode(&call, &mode, error) &&
            qa_modes_suicide(call.application->modes, mode, actor, &handled, error) &&
            current(&call, error);
        if (okay && !handled)
            okay = application_fail(error, QA_ERROR_ARGUMENT,
                "Q1 CTF suicide lost its actual source policy");
    } else if (okay) {
        qa_q1_source_client_view client;
        qa_string_id name;
        uint64_t time_ns;
        double elapsed;
        qa_q1_obituary_result notice;
        okay = qa_q1_source_client_read(provider->state.q1, actor, &client) && client.name;
        if (!okay) application_fail(error, QA_ERROR_ARGUMENT, "Q1 suicide lost its actual source name");
        if (okay) okay = qa_strings_intern_cstr(qa_session_strings(call.application->session),
            client.name, &name, error) && current(&call, error) &&
            qa_q1_client_notice_result(provider->state.q1, actor, name,
                QA_Q1_CLIENT_SUICIDE, 0, &notice, error) && current(&call, error);
        if (okay && !qa_q1_game_clock_read(provider->state.q1, &time_ns, &elapsed))
            okay = application_fail(error, QA_ERROR_ARGUMENT, "Q1 suicide lost its source clock");
        if (okay) okay = application_emit(call.application,
            &(qa_builtin_event){.kind = QA_BUILTIN_MESSAGE, .family = QA_GAME_Q1,
                .provider = provider->owner, .time_ns = time_ns, .text = notice.text,
                .arguments = notice.arguments, .argument_count = notice.argument_count,
                .flags = 2u}, error) && current(&call, error) &&
            qa_q1_source_client_add_score(provider->state.q1, actor, notice.score_delta, error) &&
            current(&call, error) && application_native_q1_request_respawn(provider, actor, error) &&
            current(&call, error);
    }
    qa_q1_game_operation_end(&call.operation);
    return okay;
}
