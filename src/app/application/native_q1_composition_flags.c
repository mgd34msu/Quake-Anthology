#include "native_q1_composition_flags.h"
#include "native_q1_composition.h"
#include "native_q1_composition_birth.h"
#include "native_q1_composition_player.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q1_composition.h"
#include "qa/game_q1_source_entities.h"
#include "qa/game_q1_source_flags.h"
#include "qa/game_q1_source_runes.h"
#include "native_q1_console.h"
#include "qa/modes_q1_source.h"
#include "qa/application_qc_presentation.h"
#include "qa/text.h"

#include <stdlib.h>

typedef struct flag_call {
    application_provider *source;
    qa_application *app;
    qa_q1_game_operation operation;
    qa_mode_id mode;
    qa_mode_view view;
} flag_call;

static bool source_current(void *context, const qa_q1_game *game, qa_error *error) {
    application_provider *source = context;
    if (!source || source->kind != APPLICATION_PROVIDER_Q1 || source->state.q1 != game)
        return application_fail(error, QA_ERROR_ARGUMENT, "CTF flag service lost its actual native GAME");
    return application_native_q1_composition_current(source->application, source->owner,
        QA_MODE_THREEWAVE, error);
}
static bool current(flag_call *call, qa_error *error) {
    qa_mode_view view;
    if (!qa_q1_game_operation_live(&call->operation))
        return application_fail(error, QA_ERROR_ARGUMENT, "CTF flag operation lost its retained source");
    return source_current(call->source, call->operation.game, error) &&
        qa_modes_read(call->app->modes, call->mode, &view, error) &&
        ((view.origin == QA_MODE_NATIVE_Q1_COMPOSITION &&
          view.source_owner == call->source->owner && view.rules.source == QA_MODE_THREEWAVE) ||
         application_fail(error, QA_ERROR_ARGUMENT, "CTF flags lost their actual source controller"));
}
static bool player_current(flag_call *call, qa_actor_id actor, bool *observer, qa_error *error) {
    return current(call, error) && application_native_q1_composition_player_current(
        call->app, call->source->owner, QA_MODE_THREEWAVE, actor, observer, error);
}
static bool begin(application_provider *source, flag_call *call, qa_error *error) {
    *call = (flag_call){.source = source, .app = source ? source->application : NULL};
    if (!source || !call->app || source->kind != APPLICATION_PROVIDER_Q1 || !source->state.q1)
        return application_fail(error, QA_ERROR_ARGUMENT, "CTF flags have no actual native GAME");
    return qa_q1_game_operation_begin(source->state.q1, &call->operation, error) &&
        application_native_q1_composition_mode(call->app, source, &call->mode, error) &&
        qa_modes_read(call->app->modes, call->mode, &call->view, error) && current(call, error);
}
static void end(flag_call *call) {
    qa_q1_game_operation_end(&call->operation);
}
static bool snapshot_players(flag_call *call, qa_actor_id **out, size_t *count,
    qa_error *error) {
    qa_q1_options options;
    double seconds;
    if (!qa_q1_source_respawn_options_read(call->operation.game, &options, &seconds, error) ||
        !current(call, error)) return false;
    qa_actor_id *players = calloc(options.max_clients ? options.max_clients : 1, sizeof(*players));
    if (!players)
        return application_fail(error, QA_ERROR_MEMORY, "Capturing genuine CTF source recipients");
    size_t total = 0;
    for (uint32_t slot = 0; slot < options.max_clients; ++slot) {
        qa_actor_id actor;
        if (qa_q1_source_client_actor(call->operation.game, slot, &actor))
            players[total++] = actor;
    }
    if (!current(call, error)) { free(players); return false; }
    *out = players;
    *count = total;
    return true;
}
static bool number(flag_call *call, qa_actor_id actor, qa_mode_q1_number field,
    double *out, qa_error *error) {
    bool observer;
    return player_current(call, actor, &observer, error) &&
        qa_modes_q1_source_read(call->app->modes, call->mode, actor, field, out, error) &&
        player_current(call, actor, &observer, error);
}
static bool set_number(flag_call *call, qa_actor_id actor, qa_mode_q1_number field,
    double value, qa_error *error) {
    bool observer;
    return player_current(call, actor, &observer, error) &&
        qa_modes_q1_source_write(call->app->modes, call->mode, actor, field, value, error) &&
        player_current(call, actor, &observer, error);
}
static bool team(flag_call *call, qa_actor_id actor, int *out, qa_error *error) {
    bool observer;
    qa_combat_state combat;
    if (!player_current(call, actor, &observer, error) ||
        !qa_combat_read_traits(call->app->combat, actor, &combat, error) ||
        !player_current(call, actor, &observer, error)) return false;
    *out = combat.team == call->view.rules.teams[0] ? 0 :
        combat.team == call->view.rules.teams[1] ? 1 : -1;
    return true;
}
static bool seconds(flag_call *call, double *out, qa_error *error) {
    qa_q1_options options;
    return current(call, error) &&
        qa_q1_source_respawn_options_read(call->operation.game, &options, out, error);
}
static bool emit(flag_call *call, qa_builtin_event *event, qa_error *error) {
    uint64_t time_ns;
    double elapsed;
    if (!current(call, error)) return false;
    if (!qa_q1_game_clock_read(call->operation.game, &time_ns, &elapsed))
        return application_fail(error, QA_ERROR_ARGUMENT, "CTF event lost its actual source clock");
    event->family = QA_GAME_Q1;
    event->provider = call->source->owner;
    event->time_ns = time_ns;
    return application_emit(call->app, event, error) && current(call, error);
}
static bool message(flag_call *call, qa_actor_id recipient, const char *key,
    const qa_builtin_message_arg *arguments, size_t count, qa_error *error) {
    bool observer;
    qa_builtin_event event = {.kind = QA_BUILTIN_MESSAGE, .actor = recipient,
        .arguments = arguments, .argument_count = count};
    return player_current(call, recipient, &observer, error) &&
        qa_strings_intern_cstr(qa_session_strings(call->app->session), key, &event.text, error) &&
        emit(call, &event, error) && player_current(call, recipient, &observer, error);
}
static bool announce_extra(flag_call *call, const char *key, qa_actor_id actor,
    const char *extra, qa_error *error) {
    bool observer;
    qa_q1_source_client_view client;
    qa_builtin_message_arg arguments[2] = {{.kind = QA_BUILTIN_MESSAGE_STRING},
        {.kind = QA_BUILTIN_MESSAGE_STRING}};
    qa_actor_id *players = NULL;
    size_t count;
    if (!player_current(call, actor, &observer, error) ||
        !qa_q1_source_client_read(call->operation.game, actor, &client) ||
        !qa_strings_intern_cstr(qa_session_strings(call->app->session), client.name,
            &arguments[0].value.text, error)) return false;
    size_t arguments_count = 1;
    if (extra && *extra) {
        if (!qa_strings_intern_cstr(qa_session_strings(call->app->session), extra,
            &arguments[1].value.text, error)) return false;
        arguments_count = 2;
    }
    if (!snapshot_players(call, &players, &count, error)) return false;
    bool okay = true;
    for (size_t i = 0; okay && i < count; ++i)
        okay = message(call, players[i], key, arguments, arguments_count, error);
    free(players);
    return okay && player_current(call, actor, &observer, error);
}
static bool announce(flag_call *call, const char *key, qa_actor_id actor, qa_error *error) {
    return announce_extra(call, key, actor, NULL, error);
}
bool application_native_q1_ctf_announce(application_provider *source, qa_actor_id actor,
    const char *key, qa_error *error) {
    flag_call call = {0};
    bool okay = begin(source, &call, error) && announce(&call, key, actor, error);
    end(&call);
    return okay;
}
static bool sound(flag_call *call, qa_actor_id actor, const char *path,
    int channel, float attenuation, qa_error *error) {
    bool observer;
    qa_body_state body;
    qa_builtin_event event = {.kind = QA_BUILTIN_SOUND, .actor = actor,
        .channel = channel, .attenuation = attenuation, .volume = 1};
    if (!player_current(call, actor, &observer, error) ||
        !qa_strings_intern_cstr(qa_session_strings(call->app->session), path, &event.resource, error) ||
        !qa_world_body_read(call->app->world, actor, &body, error) ||
        !player_current(call, actor, &observer, error)) return false;
    event.origin = qa_vec_add(body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), .5f));
    return emit(call, &event, error) && player_current(call, actor, &observer, error);
}
static bool grant(flag_call *call, qa_actor_id actor, const char *name, double count,
    qa_error *error) {
    bool observer;
    qa_inventory_entry entry = {.count = count, .capacity = 1, .policy = QA_COUNT_SOURCE_FLOAT};
    return player_current(call, actor, &observer, error) &&
        qa_strings_intern_cstr(qa_session_strings(call->app->session), name, &entry.item, error) &&
        qa_inventory_configure(call->app->inventory, actor, &entry, NULL, NULL, error) &&
        player_current(call, actor, &observer, error);
}
static bool score(flag_call *call, qa_actor_id actor, double delta, qa_error *error) {
    bool observer;
    return player_current(call, actor, &observer, error) &&
        qa_q1_source_client_add_score(call->operation.game, actor, delta, error) &&
        player_current(call, actor, &observer, error);
}
static bool update_call(flag_call *call, qa_error *error) {
    qa_actor_id *players;
    size_t count;
    if (!snapshot_players(call, &players, &count, error)) return false;
    bool okay = true;
    for (size_t i = 0; okay && i < count; ++i)
        okay = application_native_q1_ctf_status(call->source, players[i], error) && current(call, error);
    free(players);
    return okay;
}
static bool home(flag_call *call, bool blue, qa_actor_id *out, bool *found, qa_error *error) {
    qa_q1_source_entity entity;
    if (!qa_q1_source_entity_first(call->operation.game,
        blue ? "item_flag_team2" : "item_flag_team1", &entity, found, error) ||
        !current(call, error)) return false;
    *out = entity.actor;
    return true;
}
static bool return_call(flag_call *call, qa_actor_id flag, bool announce_return, qa_error *error) {
    qa_q1_source_flag_view view;
    if (!qa_q1_source_flag_read(call->operation.game, flag, &view, error) ||
        !qa_q1_source_flag_return(call->operation.game, flag, error) || !current(call, error)) return false;
    if (!announce_return) return true;
    qa_actor_id *players;
    size_t count;
    if (!snapshot_players(call, &players, &count, error)) return false;
    bool okay = true;
    for (size_t i = 0; okay && i < count; ++i) {
        int color;
        okay = team(call, players[i], &color, error) &&
            message(call, players[i], color == (int)view.blue ?
                "$qc_ctf_your_returned" : "$qc_ctf_enemy_returned", NULL, 0, error);
    }
    free(players);
    return okay;
}

static bool player_frame(void *context, qa_actor_id actor, double *out, qa_error *error) {
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Carried flag animation requires its source output");
    flag_call call = {0};
    bool observer;
    bool okay = begin(context, &call, error) && player_current(&call, actor, &observer, error);
    application_provider *character = okay ? application_provider_for(call.app, actor, QA_ROLE_CHARACTER, "") : NULL;
    double frame = 0;
    if (okay && (!character || !character->constructed || character->close_pending))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "Carried flag has no admitted selected CHARACTER");
    if (okay) switch (character->kind) {
    case APPLICATION_PROVIDER_Q1: {
        qa_q1_character_view view;
        okay = qa_q1_character_read(character->state.q1, actor, &view);
        if (okay) frame = view.animation_frame;
        else application_fail(error, QA_ERROR_NOT_FOUND, "Carried flag lost its selected Q1 animation");
        break;
    }
    case APPLICATION_PROVIDER_Q2: {
        qa_q2_visual view;
        okay = qa_q2_presentation_read(character->state.q2, actor, &view);
        if (okay) frame = view.frame;
        else application_fail(error, QA_ERROR_NOT_FOUND, "Carried flag lost its selected Q2 animation");
        break;
    }
    case APPLICATION_PROVIDER_Q3: {
        qa_q3_player_state state;
        okay = qa_q3_player_read(character->state.q3, actor, &state);
        if (!okay) application_fail(error, QA_ERROR_NOT_FOUND, "Carried flag lost its selected Q3 player");
        /* The actual selected-player donor has no legacy Q1/Q2 frame for Q3. */
        break;
    }
    case APPLICATION_PROVIDER_QC: {
        qa_application_qc_animation view;
        okay = qa_application_qc_selected_character_frame_read(call.app, actor, &view, error);
        if (okay) {
            frame = view.frame;
            okay = qa_application_qc_selected_character_frame_current(call.app, &view) ||
                application_fail(error, QA_ERROR_ARGUMENT, "Carried flag selected QC animation changed");
        }
        break;
    }
    case APPLICATION_PROVIDER_QVM: {
        uint32_t slot;
        okay = character->component.clock.kind == QA_RULESET_Q3 &&
            application_q3_guest_actor_client(character, actor, &slot);
        if (!okay) application_fail(error, QA_ERROR_UNSUPPORTED,
            "Carried flag has no genuine selected original Q3 player animation");
        break;
    }
    case APPLICATION_PROVIDER_NATIVE:
        okay = application_native_q1_selected_q2_frame(call.app, actor, &frame, error);
        break;
    }
    if (okay) okay = player_current(&call, actor, &observer, error);
    if (okay && (application_provider_for(call.app, actor, QA_ROLE_CHARACTER, "") != character ||
        !character->constructed || character->close_pending))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "Carried flag selected animation owner changed");
    if (okay) *out = frame;
    end(&call);
    return okay;
}

static bool return_flag(void *context, qa_actor_id flag, qa_error *error) {
    flag_call call = {0};
    bool okay = begin(context, &call, error) && return_call(&call, flag, true, error);
    end(&call);
    return okay;
}
static bool update(void *context, qa_error *error) {
    flag_call call = {0};
    bool okay = begin(context, &call, error) && update_call(&call, error);
    end(&call);
    return okay;
}

static bool log_action(flag_call *call, qa_actor_id actor, const char *action, qa_error *error) {
    bool observer;
    qa_builtin_event event = {.kind = QA_BUILTIN_SOURCE_LOG, .actor = actor};
    return player_current(call, actor, &observer, error) &&
        qa_strings_intern_cstr(qa_session_strings(call->app->session), action, &event.text, error) &&
        emit(call, &event, error) && player_current(call, actor, &observer, error);
}
static bool capture_total(flag_call *call, bool blue, qa_error *error) {
    qa_builtin_event event = {.kind = QA_BUILTIN_CTF_CAPTURE, .ctf_capture.blue = blue};
    return qa_q1_source_capture_add(call->operation.game, blue, &event.ctf_capture.total, error) &&
        current(call, error) && emit(call, &event, error);
}

bool application_native_q1_ctf_drop_flag(application_provider *source, qa_actor_id actor,
    qa_error *error) {
    flag_call call = {0};
    qa_actor_id flag;
    bool found;
    bool okay = begin(source, &call, error) &&
        qa_q1_source_flag_carried(call.operation.game, actor, &flag, &found, error) && current(&call, error);
    if (okay && found) {
        double last;
        okay = number(&call, actor, QA_Q1_CTF_LAST_TEAM, &last, error) &&
            announce(&call, last == 5 ? "$qc_ks_blue_dropped" : "$qc_ks_red_dropped", actor, error) &&
            log_action(&call, actor, "FLAG-DROP", error) &&
            qa_q1_source_flag_drop(call.operation.game, flag, actor, error) &&
            current(&call, error) && update_call(&call, error);
    }
    end(&call);
    return okay;
}
static bool drop_flag(void *context, qa_actor_id actor, qa_error *error) {
    return application_native_q1_ctf_drop_flag(context, actor, error);
}

static bool captured(flag_call *call, qa_actor_id actor, bool blue, qa_error *error) {
    double time;
    if (!announce(call, blue ? "$qc_ks_red_captured" : "$qc_ks_blue_captured", actor, error) ||
        !log_action(call, actor, "FLAG-CAPTURE", error) ||
        !grant(call, actor, "q1:key/silver", 0, error) ||
        !grant(call, actor, "q1:key/gold", 0, error) || !seconds(call, &time, error) ||
        !qa_q1_source_capture_words_write(call->operation.game, time, blue ? 14 : 5, error) ||
        !current(call, error) || !sound(call, actor, "misc/flagcap.wav", 2, 0, error) ||
        !capture_total(call, blue, error) || !score(call, actor, 15, error)) return false;
    qa_actor_id *players;
    size_t count;
    if (!snapshot_players(call, &players, &count, error)) return false;
    bool okay = true;
    double color = blue ? 14 : 5;
    for (size_t i = 0; okay && i < count; ++i) {
        qa_actor_id recipient = players[i];
        double last;
        okay = set_number(call, recipient, QA_Q1_CTF_KILLED, 0, error) &&
            number(call, recipient, QA_Q1_CTF_LAST_TEAM, &last, error);
        if (!okay) break;
        if (last == color) {
            if (!qa_actor_id_equal(recipient, actor)) okay = score(call, recipient, 10, error);
            double returned;
            if (okay) okay = number(call, recipient, QA_Q1_CTF_LAST_RETURNED, &returned, error) &&
                seconds(call, &time, error);
            if (okay && returned + 4 > time)
                okay = announce(call, "$qc_ks_assist", recipient, error) && score(call, recipient, 1, error);
            double fragged;
            if (okay) okay = number(call, recipient, QA_Q1_CTF_LAST_FRAGGED_CARRIER, &fragged, error) &&
                seconds(call, &time, error);
            if (okay && fragged + 6 > time)
                okay = announce(call, "$qc_ks_assist_carrier", recipient, error) && score(call, recipient, 2, error);
        } else okay = set_number(call, recipient, QA_Q1_CTF_LAST_HURT_CARRIER, -5, error);
        if (okay) okay = number(call, recipient, QA_Q1_CTF_LAST_TEAM, &last, error) &&
            message(call, recipient, last == color ? "$qc_ctf_team_captured" :
                "$qc_ctf_your_captured", NULL, 0, error);
    }
    free(players);
    for (unsigned i = 0; okay && i < 2; ++i) {
        qa_actor_id flag;
        bool found;
        okay = home(call, i == 0 ? blue : !blue, &flag, &found, error);
        if (okay && found) okay = return_call(call, flag, false, error);
    }
    return okay;
}

static bool touch(void *context, qa_actor_id flag, qa_actor_id actor, qa_error *error) {
    flag_call call = {0};
    bool okay = begin(context, &call, error);
    qa_q1_source_flag_view view;
    if (okay) okay = qa_q1_source_flag_read(call.operation.game, flag, &view, error) && current(&call, error);
    qa_q1_source_client_view client;
    if (okay && (!view.trigger || !qa_q1_source_client_read(call.operation.game, actor, &client))) {
        end(&call);
        return true;
    }
    bool observer;
    qa_combat_state combat;
    int color;
    double last;
    if (okay) okay = player_current(&call, actor, &observer, error) &&
        qa_combat_read(call.app->combat, actor, &combat, error) &&
        player_current(&call, actor, &observer, error) && team(&call, actor, &color, error) &&
        number(&call, actor, QA_Q1_CTF_LAST_TEAM, &last, error);
    if (okay && (combat.health <= 0 || observer || color < 0 || last != (color == 0 ? 5 : 14))) {
        end(&call);
        return true;
    }
    if (okay && color == (int)view.blue) {
        if (view.count == 0) {
            qa_actor_id carried;
            bool found;
            okay = qa_q1_source_flag_carried(call.operation.game, actor, &carried, &found, error) &&
                current(&call, error);
            if (okay && !found) { end(&call); return true; }
            if (okay) okay = captured(&call, actor, color == 1, error);
        } else {
            double time;
            okay = announce(&call, color == 0 ? "$qc_ks_red_returned" : "$qc_ks_blue_returned", actor, error) &&
                log_action(&call, actor, "FLAG-RECOVERY", error) && score(&call, actor, 1, error) &&
                seconds(&call, &time, error) && set_number(&call, actor, QA_Q1_CTF_LAST_RETURNED, time, error) &&
                sound(&call, actor, "doors/runetry.wav", 3, 1, error) && return_call(&call, flag, true, error);
        }
    } else if (okay) {
        double time;
        okay = announce(&call, "$qc_ks_blue_picked_up", actor, error) &&
            log_action(&call, actor, "FLAG-PICKUP", error) &&
            message(&call, actor, "$qc_ctf_have_flag", NULL, 0, error) &&
            sound(&call, actor, "misc/flagtk.wav", 3, 1, error) &&
            grant(&call, actor, view.blue ? "q1:key/silver" : "q1:key/gold", 1, error) &&
            seconds(&call, &time, error) && set_number(&call, actor, QA_Q1_CTF_FLAG_SINCE, time, error) &&
            qa_q1_source_flag_carry(call.operation.game, flag, actor, error) && current(&call, error);
        qa_actor_id *players = NULL;
        size_t count = 0;
        if (okay) okay = snapshot_players(&call, &players, &count, error);
        for (size_t i = 0; okay && i < count; ++i) {
            if (qa_actor_id_equal(players[i], actor)) continue;
            int recipient_color;
            okay = team(&call, players[i], &recipient_color, error) &&
                message(&call, players[i], recipient_color == color ? "$qc_ctf_your_has" :
                    "$qc_ctf_your_taken", NULL, 0, error);
        }
        free(players);
    }
    if (okay) okay = update_call(&call, error);
    end(&call);
    return okay;
}

static const char *const rune_items[QA_Q1_RUNE_COUNT] = {
    "q1:ctf/rune/resistance", "q1:ctf/rune/strength",
    "q1:ctf/rune/haste", "q1:ctf/rune/regeneration"};
static bool held_rune(flag_call *call, qa_actor_id actor, qa_q1_source_rune *out,
    bool *found, qa_error *error) {
    bool observer;
    if (!player_current(call, actor, &observer, error)) return false;
    for (unsigned i = 0; i < QA_Q1_RUNE_COUNT; ++i) {
        qa_item_id item;
        double count;
        if (!qa_strings_intern_cstr(qa_session_strings(call->app->session), rune_items[i],
            &item, error) ||
            !qa_inventory_count_read(call->app->inventory, actor, item, &count, error) ||
            !player_current(call, actor, &observer, error)) return false;
        if (count > 0) {
            *out = (qa_q1_source_rune)i;
            *found = true;
            return true;
        }
    }
    *found = false;
    return true;
}
static bool source_cvar(flag_call *call, const char *name, double *out, qa_error *error) {
    if (!current(call, error)) return false;
    qa_cvars *cvars = application_native_q1_console_registry(call->source);
    const qa_cvar_view *value = cvars ? qa_cvars_find(cvars, name) : NULL;
    if (!value || value->owner != call->source->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "CTF rune lost its genuine GAME policy");
    *out = (float)(value->number);
    return true;
}

bool application_native_q1_ctf_drop_rune(application_provider *source, qa_actor_id actor,
    qa_error *error) {
    flag_call call = {0};
    qa_q1_source_rune rune;
    bool found, observer;
    bool okay = begin(source, &call, error) && held_rune(&call, actor, &rune, &found, error);
    if (okay && found) {
        qa_body_state body;
        qa_actor_id item;
        okay = qa_world_body_read(call.app->world, actor, &body, error) &&
            player_current(&call, actor, &observer, error) &&
            qa_q1_source_rune_drop(call.operation.game, rune, body.origin, &item, error) &&
            player_current(&call, actor, &observer, error) &&
            grant(&call, actor, rune_items[rune], 0, error) &&
            application_native_q1_ctf_status(source, actor, error) && current(&call, error);
    }
    end(&call);
    return okay;
}

static bool health(flag_call *call, qa_actor_id actor, float *out, qa_error *error) {
    qa_combat_state state;
    bool observer;
    if (!player_current(call, actor, &observer, error) ||
        !qa_combat_read(call->app->combat, actor, &state, error) ||
        !player_current(call, actor, &observer, error)) return false;
    *out = state.health;
    return true;
}
bool application_native_q1_ctf_regenerate(application_provider *source, qa_actor_id actor,
    qa_error *error) {
    flag_call call = {0};
    qa_q1_source_rune rune;
    bool found, observer;
    double due, time;
    float value;
    bool okay = begin(source, &call, error) && held_rune(&call, actor, &rune, &found, error);
    if (okay && (!found || rune != QA_Q1_RUNE_REGENERATION)) { end(&call); return true; }
    if (okay) okay = number(&call, actor, QA_Q1_CTF_REGEN_TIME, &due, error) &&
        seconds(&call, &time, error);
    if (okay && due >= time) { end(&call); return true; }
    if (okay) okay = health(&call, actor, &value, error);
    if (okay && value <= 0) { end(&call); return true; }
    double delay = 0;
    if (okay) okay = health(&call, actor, &value, error);
    if (okay && value < 150) {
        okay = health(&call, actor, &value, error) &&
            qa_combat_set_health(call.app->combat, actor, fminf(150, value + 5), error) &&
            player_current(&call, actor, &observer, error);
        if (okay) delay += .5;
    }
    qa_combat_state combat;
    if (okay) okay = qa_combat_read(call.app->combat, actor, &combat, error) &&
        player_current(&call, actor, &observer, error);
    if (okay && combat.armor.regular.kind != QA_ARMOR_NONE && combat.armor.regular.points < 150 &&
        (combat.armor.regular.kind != QA_ARMOR_Q1 || combat.armor.regular.protection.q1_absorption > 0)) {
        okay = qa_combat_set_regular_points(call.app->combat, actor,
            fminf(150, (float)combat.armor.regular.points + 5), NULL, error) &&
            player_current(&call, actor, &observer, error);
        if (okay) delay += .5;
    }
    if (okay) okay = seconds(&call, &time, error) &&
        set_number(&call, actor, QA_Q1_CTF_REGEN_TIME, time + delay, error);
    if (okay && delay > 0) {
        okay = number(&call, actor, QA_Q1_CTF_REGEN_SOUND, &due, error) && seconds(&call, &time, error);
        if (okay && due < time) okay = seconds(&call, &time, error) &&
            set_number(&call, actor, QA_Q1_CTF_REGEN_SOUND, time + 1, error) &&
            sound(&call, actor, "rune/rune4.wav", 4, 1, error);
    }
    end(&call);
    return okay;
}

static bool haste_sound(flag_call *call, qa_actor_id actor, qa_error *error) {
    double due, time;
    if (!number(call, actor, QA_Q1_CTF_HASTE_SOUND, &due, error) ||
        !seconds(call, &time, error)) return false;
    if (!(due < time)) return true;
    return seconds(call, &time, error) &&
        set_number(call, actor, QA_Q1_CTF_HASTE_SOUND, time + 1, error) &&
        sound(call, actor, "rune/rune3.wav", 4, 1, error);
}
static bool haste_interval(qa_q1_weapon weapon, float *out) {
    switch (weapon) {
    case QA_Q1_AXE: case QA_Q1_SHOTGUN: case QA_Q1_GRENADE: *out = .3f; return true;
    case QA_Q1_SUPER_SHOTGUN: case QA_Q1_ROCKET: *out = .4f; return true;
    default: return false;
    }
}
bool application_native_q1_ctf_weapon_parameters(application_provider *source, qa_actor_id actor,
    qa_q1_weapon weapon, qa_q1_weapon_parameters *parameters, qa_error *error) {
    if (!parameters) return application_fail(error, QA_ERROR_ARGUMENT, "CTF weapon parameters are absent");
    flag_call call = {0};
    qa_q1_source_rune rune;
    bool found;
    bool okay = begin(source, &call, error) && held_rune(&call, actor, &rune, &found, error);
    if (okay && found && rune == QA_Q1_RUNE_HASTE) {
        (void)haste_interval(weapon, &parameters->interval);
        parameters->nail_speed = 2000;
    }
    end(&call);
    return okay;
}
bool application_native_q1_ctf_before_fire(application_provider *source, qa_actor_id actor,
    qa_error *error) {
    flag_call call = {0};
    qa_q1_source_rune rune;
    bool found;
    bool okay = begin(source, &call, error) && held_rune(&call, actor, &rune, &found, error);
    if (okay && found && rune == QA_Q1_RUNE_STRENGTH) {
        double due, time;
        okay = number(&call, actor, QA_Q1_CTF_STRENGTH_SOUND, &due, error) && seconds(&call, &time, error);
        if (okay && due < time) {
            bool observer;
            okay = seconds(&call, &time, error) &&
                set_number(&call, actor, QA_Q1_CTF_STRENGTH_SOUND, time + 1, error);
            double quad = okay ? qa_q1_game_power_expires(call.operation.game, actor, QA_Q1_QUAD) : 0;
            if (okay) okay = player_current(&call, actor, &observer, error) && seconds(&call, &time, error) &&
                sound(&call, actor, quad > time ? "rune/rune22.wav" : "rune/rune2.wav", 4, 1, error);
        }
    }
    end(&call);
    return okay;
}
bool application_native_q1_ctf_attack_delay(application_provider *source, qa_actor_id actor,
    qa_q1_weapon weapon, float *delay, qa_error *error) {
    if (!delay) return application_fail(error, QA_ERROR_ARGUMENT, "CTF attack delay is absent");
    flag_call call = {0};
    qa_q1_source_rune rune;
    bool found;
    float interval;
    bool okay = begin(source, &call, error) && held_rune(&call, actor, &rune, &found, error);
    if (okay && found && rune == QA_Q1_RUNE_HASTE && haste_interval(weapon, &interval)) {
        okay = haste_sound(&call, actor, error);
        if (okay) *delay = interval;
    }
    end(&call);
    return okay;
}
bool application_native_q1_ctf_nail_fire(application_provider *source, qa_actor_id actor,
    qa_error *error) {
    flag_call call = {0};
    qa_q1_source_rune rune;
    bool found;
    bool okay = begin(source, &call, error) && held_rune(&call, actor, &rune, &found, error);
    if (okay && found && rune == QA_Q1_RUNE_HASTE) okay = haste_sound(&call, actor, error);
    end(&call);
    return okay;
}
static bool optional_rune(flag_call *call, qa_actor_id actor, qa_q1_source_rune *rune,
    bool *found, qa_error *error) {
    qa_q1_source_client_view client;
    *found = false;
    if (!current(call, error)) return false;
    if (!qa_q1_source_client_read(call->operation.game, actor, &client)) return true;
    return held_rune(call, actor, rune, found, error);
}
static bool last_team(flag_call *call, qa_actor_id actor, double *out, qa_error *error) {
    qa_q1_source_client_view client;
    *out = 0;
    if (!current(call, error)) return false;
    if (!qa_q1_source_client_read(call->operation.game, actor, &client)) return true;
    return number(call, actor, QA_Q1_CTF_LAST_TEAM, out, error);
}
static bool friendly(flag_call *call, const qa_damage_request *request, bool *out,
    qa_error *error) {
    *out = false;
    if (!request->attack.attacker.registry ||
        qa_actor_id_equal(request->attack.attacker, request->target)) return true;
    double target, attacker;
    if (!last_team(call, request->target, &target, error)) return false;
    if (target != 5 && target != 14) return true;
    if (!last_team(call, request->attack.attacker, &attacker, error)) return false;
    *out = target == attacker;
    return true;
}
static bool direct_damage(flag_call *call, qa_actor_id target, qa_actor_id inflictor,
    qa_actor_id attacker, float amount, const char *cause, qa_error *error) {
    if (!inflictor.registry) {
        qa_q1_source_entity world;
        bool found;
        if (!qa_q1_source_entity_first(call->operation.game, "worldspawn", &world, &found, error) ||
            !current(call, error)) return false;
        if (!found) return application_fail(error, QA_ERROR_ARGUMENT,
            "CTF source damage lost its real world actor");
        inflictor = world.actor;
    }
    return qa_q1_source_damage(call->operation.game, target, inflictor, attacker, amount, cause, error) &&
        current(call, error);
}
bool application_native_q1_ctf_damage_effect(application_provider *source,
    qa_damage_effect_stage stage, const qa_damage_request *request,
    qa_damage_effect *effect, qa_error *error) {
    if (!request || !effect) return application_fail(error, QA_ERROR_ARGUMENT,
        "CTF source damage needs its canonical request and effect");
    if (stage != QA_DAMAGE_AFTER_QUAD && stage != QA_DAMAGE_ARMOR_ALLOWED &&
        stage != QA_DAMAGE_PROTECTION_APPLIES && stage != QA_DAMAGE_BEFORE_HEALTH) return true;
    flag_call call = {0};
    bool okay = begin(source, &call, error);
    if (okay && stage == QA_DAMAGE_AFTER_QUAD) {
        qa_q1_source_rune rune;
        bool found;
        if (request->attack.attacker.registry) {
            okay = optional_rune(&call, request->attack.attacker, &rune, &found, error);
            if (okay && found && rune == QA_Q1_RUNE_STRENGTH)
                effect->amount = (float)(float)((double)effect->amount * 2);
        }
        if (okay) okay = optional_rune(&call, request->target, &rune, &found, error);
        if (okay && found && rune == QA_Q1_RUNE_RESISTANCE) {
            effect->amount = (float)(float)((double)effect->amount / 2);
            double due, time;
            okay = number(&call, request->target, QA_Q1_CTF_RESISTANCE_SOUND, &due, error) &&
                seconds(&call, &time, error);
            if (okay && due < time) okay = seconds(&call, &time, error) &&
                set_number(&call, request->target, QA_Q1_CTF_RESISTANCE_SOUND, time + 1, error) &&
                sound(&call, request->target, "rune/rune1.wav", 4, 1, error);
        }
        qa_q1_source_client_view attacker;
        if (okay && request->attack.attacker.registry &&
            qa_q1_source_client_read(call.operation.game, request->attack.attacker, &attacker)) {
            qa_actor_id flag;
            okay = qa_q1_source_flag_carried(call.operation.game, request->target, &flag, &found, error) &&
                current(&call, error);
            if (okay && found) {
                double a, t, time;
                okay = last_team(&call, request->attack.attacker, &a, error) &&
                    last_team(&call, request->target, &t, error);
                if (okay && a != t && (t == 5 || t == 14))
                    okay = seconds(&call, &time, error) && set_number(&call,
                        request->attack.attacker, QA_Q1_CTF_LAST_HURT_CARRIER, time, error);
            }
        }
    } else if (okay && stage == QA_DAMAGE_PROTECTION_APPLIES) {
        qa_combat_state combat;
        double last;
        okay = qa_combat_read_traits(call.app->combat, request->target, &combat, error) &&
            current(&call, error) && last_team(&call, request->target, &last, error);
        if (okay) {
            int team_index = combat.team == call.view.rules.teams[0] ? 0 :
                combat.team == call.view.rules.teams[1] ? 1 : -1;
            int last_index = last == 5 ? 0 : last == 14 ? 1 : -1;
            effect->allowed = effect->allowed && team_index == last_index;
        }
    } else if (okay) {
        double policy;
        bool teammates;
        okay = source_cvar(&call, "teamplay", &policy, error);
        if (okay && !(policy < 0) && !call.view.rules.start_map) {
            okay = friendly(&call, request, &teammates, error);
            uint32_t flags = (uint32_t)qa_source_float_to_i32((float)policy);
            if (okay && teammates) {
                if (stage == QA_DAMAGE_ARMOR_ALLOWED && (flags & 2u)) effect->allowed = false;
                if (stage == QA_DAMAGE_BEFORE_HEALTH) {
                    if (flags & 4u) okay = direct_damage(&call, request->attack.attacker,
                        request->attack.inflictor, request->attack.attacker,
                        effect->amount, "ctf:reflection", error);
                    if (okay && (flags & 1u)) effect->allowed = false;
                }
            }
        }
    }
    if (okay) okay = current(&call, error);
    end(&call);
    return okay;
}

static bool player_body(flag_call *call, qa_actor_id actor, qa_body_state *out, qa_error *error) {
    bool observer;
    return player_current(call, actor, &observer, error) &&
        qa_world_body_read(call->app->world, actor, out, error) &&
        player_current(call, actor, &observer, error);
}
static qa_vec3 body_center(qa_body_state body) {
    return qa_vec_add(body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), .5f));
}
bool application_native_q1_ctf_score_death(application_provider *source, qa_actor_id victim,
    qa_actor_id attacker, qa_error *error) {
    flag_call call = {0};
    bool okay = begin(source, &call, error);
    qa_q1_source_client_view client;
    if (okay && !qa_q1_source_client_read(call.operation.game, victim, &client)) {
        end(&call);
        return true;
    }
    if (okay && (!attacker.registry || !qa_q1_source_client_read(call.operation.game, attacker, &client) ||
        qa_actor_id_equal(victim, attacker))) {
        okay = score(&call, victim, -1, error);
        end(&call);
        return okay;
    }
    int color = -1, victim_color = -1;
    bool teammates = false;
    double policy = 0;
    qa_damage_request death = {.target = victim, .attack.attacker = attacker};
    if (okay) okay = team(&call, attacker, &color, error) && friendly(&call, &death, &teammates, error) &&
        source_cvar(&call, "teamplay", &policy, error);
    if (okay && policy == 2 && color >= 0) {
        okay = team(&call, victim, &victim_color, error);
        if (okay && victim_color == color) {
            okay = score(&call, attacker, -1, error);
            end(&call);
            return okay;
        }
    }
    if (okay) okay = source_cvar(&call, "teamplay", &policy, error);
    double penalty = policy < 0 ? -policy : teammates && ((uint32_t)qa_source_float_to_i32((float)policy) & 8u) ? 1 : 0;
    if (okay && penalty > 0) okay = score(&call, attacker, -penalty, error);
    else if (okay) {
        okay = score(&call, attacker, 1, error);
        qa_actor_id carried;
        bool found;
        if (okay) okay = qa_q1_source_flag_carried(call.operation.game, victim, &carried, &found, error) &&
            current(&call, error);
        if (okay && found) {
            okay = team(&call, victim, &victim_color, error);
            if (okay && victim_color != color) {
                double time, since;
                okay = seconds(&call, &time, error) &&
                    set_number(&call, attacker, QA_Q1_CTF_LAST_FRAGGED_CARRIER, time, error) &&
                    number(&call, victim, QA_Q1_CTF_FLAG_SINCE, &since, error) && seconds(&call, &time, error);
                if (okay && since + 2 > time)
                    okay = message(&call, attacker, "$qc_ctf_carrier_no_bonus", NULL, 0, error);
                else if (okay) {
                    qa_builtin_message_arg points = {.kind = QA_BUILTIN_MESSAGE_NUMBER, .value.number = 2};
                    okay = score(&call, attacker, 2, error) &&
                        message(&call, attacker, "$qc_ctf_kill_carrier", &points, 1, error);
                }
            }
        }
        const char *team_name = color == 0 ? "$qc_ctf_redteam" : color == 1 ? "$qc_ctf_blueteam" : "";
        bool carrier_bonus = false, flag_bonus = false;
        double hurt, time;
        if (okay) okay = number(&call, victim, QA_Q1_CTF_LAST_HURT_CARRIER, &hurt, error) &&
            seconds(&call, &time, error);
        if (okay && hurt + 4 > time) {
            okay = qa_q1_source_flag_carried(call.operation.game, attacker, &carried, &found, error) &&
                current(&call, error);
            if (okay && !found) {
                okay = score(&call, attacker, 2, error) && announce_extra(&call,
                    "$qc_ks_defends_carrier_aggressive", attacker, team_name, error);
                if (okay) carrier_bonus = true;
            }
        }
        qa_body_state attacker_body = {0}, victim_body = {0};
        if (okay) okay = player_body(&call, attacker, &attacker_body, error) &&
            player_body(&call, victim, &victim_body, error);
        qa_vec3 centers[2] = {attacker_body.origin, victim_body.origin};
        for (size_t pass = 0; okay && pass < 2; ++pass) {
            qa_actor_id *players = NULL;
            size_t count;
            okay = snapshot_players(&call, &players, &count, error);
            for (size_t i = 0; okay && i < count; ++i) {
                qa_actor_id player = players[i];
                qa_body_state body;
                bool observer;
                int player_color;
                okay = player_body(&call, player, &body, error);
                if (okay && !carrier_bonus) okay = player_current(&call, player, &observer, error);
                if (okay && !carrier_bonus && !observer && !qa_actor_id_equal(player, attacker)) {
                    okay = team(&call, player, &player_color, error);
                    if (okay && player_color == color) {
                        okay = qa_q1_source_flag_carried(call.operation.game, player, &carried, &found, error) &&
                            current(&call, error);
                        if (okay && found && qa_vec_length(qa_vec_sub(body_center(body), centers[pass])) <= 550) {
                            okay = score(&call, attacker, 1, error) && announce_extra(&call,
                                "$qc_ks_defends_carrier", attacker, team_name, error);
                            if (okay) carrier_bonus = true;
                        }
                    }
                }
            }
            free(players);
            qa_actor_id flag;
            bool found_flag = false;
            if (okay && color >= 0) okay = home(&call, color == 1, &flag, &found_flag, error);
            if (okay && (!flag_bonus || (color == 0 && pass == 1)) && found_flag) {
                qa_q1_source_flag_view flag_view;
                okay = qa_q1_source_flag_read(call.operation.game, flag, &flag_view, error) && current(&call, error);
                if (okay && flag_view.trigger) {
                    qa_body_state flag_body;
                    okay = qa_world_body_read(call.app->world, flag, &flag_body, error) && current(&call, error);
                    if (okay && qa_vec_length(qa_vec_sub(body_center(flag_body), centers[pass])) <= 550) {
                        okay = score(&call, attacker, 1, error) && announce_extra(&call,
                            "$qc_ks_defends_flag", attacker, team_name, error);
                        if (okay) flag_bonus = true;
                    }
                }
            }
        }
    }
    if (okay) okay = source_cvar(&call, "teamplay", &policy, error);
    if (okay && policy >= 0 && teammates && ((uint32_t)qa_source_float_to_i32((float)policy) & 16u))
        okay = direct_damage(&call, attacker, attacker, attacker, 1000, "ctf:teamkill", error) &&
            score(&call, attacker, 1, error);
    if (okay) okay = current(&call, error);
    end(&call);
    return okay;
}

static bool rune_touch(void *context, qa_actor_id item, qa_actor_id actor, qa_error *error) {
    flag_call call = {0};
    bool okay = begin(context, &call, error);
    qa_q1_source_client_view client;
    if (okay && !qa_q1_source_client_read(call.operation.game, actor, &client)) {
        end(&call);
        return true;
    }
    bool observer;
    qa_combat_state combat;
    if (okay) okay = player_current(&call, actor, &observer, error) &&
        qa_combat_read(call.app->combat, actor, &combat, error) &&
        player_current(&call, actor, &observer, error);
    if (okay && (observer || combat.health <= 0)) { end(&call); return true; }
    qa_q1_source_rune held;
    bool found;
    if (okay) okay = held_rune(&call, actor, &held, &found, error);
    if (okay && found) {
        double notice, time;
        okay = number(&call, actor, QA_Q1_CTF_RUNE_NOTICE, &notice, error) && seconds(&call, &time, error);
        if (okay && notice < time)
            okay = message(&call, actor, "$qc_already_have_rune", NULL, 0, error) &&
                seconds(&call, &time, error) &&
                set_number(&call, actor, QA_Q1_CTF_RUNE_NOTICE, time + 5, error);
        end(&call);
        return okay;
    }
    qa_q1_source_rune rune;
    static const char *const hud[] = {"$qc_rune1_hud", "$qc_rune2_hud", "$qc_rune3_hud", "$qc_rune4_hud"};
    static const char *const got[] = {"$qc_got_rune1", "$qc_got_rune2", "$qc_got_rune3", "$qc_got_rune4"};
    if (okay) okay = qa_q1_source_rune_read(call.operation.game, item, &rune, error) &&
        current(&call, error) && grant(&call, actor, rune_items[rune], 1, error) &&
        message(&call, actor, hud[rune], NULL, 0, error) &&
        sound(&call, actor, "weapons/lock4.wav", 3, 1, error);
    if (okay) {
        qa_body_state body;
        qa_builtin_event event = {.kind = QA_BUILTIN_ITEM, .actor = item, .other = actor};
        okay = qa_world_body_read(call.app->world, actor, &body, error) &&
            player_current(&call, actor, &observer, error) &&
            qa_strings_intern_cstr(qa_session_strings(call.app->session), rune_items[rune],
                &event.resource, error);
        if (okay) { event.origin = body.origin; okay = emit(&call, &event, error); }
    }
    double teamplay;
    if (okay) okay = source_cvar(&call, "teamplay", &teamplay, error);
    if (okay && ((teamplay == 0 && rune != QA_Q1_RUNE_REGENERATION) ||
        (teamplay == 2147483648.0 && rune == QA_Q1_RUNE_REGENERATION)))
        okay = announce(&call, got[rune], actor, error);
    if (okay) okay = qa_q1_source_rune_collect(call.operation.game, item, error) &&
        player_current(&call, actor, &observer, error) &&
        application_native_q1_ctf_status(call.source, actor, error) && current(&call, error);
    end(&call);
    return okay;
}

bool application_native_q1_ctf_flags_configure(application_provider *source, qa_error *error) {
    qa_q1_options options;
    double time;
    if (!source || source->kind != APPLICATION_PROVIDER_Q1 || !source->state.q1 ||
        !qa_q1_source_respawn_options_read(source->state.q1, &options, &time, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "CTF flag binding requires its genuine native constructor");
    if (options.program != QA_Q1_CTF) return true;
    return qa_q1_source_flags_configure(source->state.q1, &(qa_q1_source_flags_services){
        .context = source, .current = source_current, .touch = touch, .return_flag = return_flag,
        .drop_flag = drop_flag, .update = update, .player_frame = player_frame}, error) &&
        qa_q1_source_runes_configure(source->state.q1, &(qa_q1_source_runes_services){
            .context = source, .current = source_current, .touch = rune_touch}, error);
}
