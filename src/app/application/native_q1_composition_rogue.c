#include "native_q1_composition_rogue.h"
#include "native_q1_composition.h"
#include "native_q1_console.h"
#include "native_q1_composition_birth.h"
#include "native_q1_composition_player.h"
#include "qa/game_q1_bots.h"
#include "qa/game_q1_composition.h"
#include "qa/game_q1_rogue.h"
#include "qa/text.h"
#include "qa/modes_q1_source.h"
#include "qa/game_q1_source_rogue_tag.h"
#include "qa/game_q1_source_rogue_flags.h"
#include "qa/game_q1_source_entities.h"
#include "qa/game_q1_source_obituary.h"
#include "qa/game_q1_source_rogue_runes.h"
#include "qa/source_number.h"
#include "map_players_private.h"
#include "qa/application_qc_presentation.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct rogue_call {
    application_provider *source;
    qa_application *app;
    qa_q1_game_operation operation;
    qa_mode_id mode;
} rogue_call;

static bool rogue_flags_configure(application_provider *, qa_error *);

bool application_native_q1_base_team_health(void *context, bool *enabled, qa_error *error)
{
    application_provider *policy = context;
    qa_application *app = policy ? policy->application : NULL;
    application_provider *source = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    if (!enabled || !policy || !policy->constructed || !policy->attached || policy->close_pending ||
        !source || !source->constructed || !source->attached || source->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q1 team-health rule lost its actual source");
    *enabled = true;
    if (source->kind != APPLICATION_PROVIDER_Q1) return true;
    qa_q1_options options;
    double seconds;
    if (!qa_q1_source_respawn_options_read(source->state.q1, &options, &seconds, error)) return false;
    if (options.program == QA_Q1_ROGUE) {
        if (!application_native_q1_composition_current(app, source->owner, QA_MODE_ROGUE, error)) return false;
        *enabled = false;
    }
    return true;
}

static bool current(rogue_call *call, qa_error *error)
{
    qa_mode_view view;
    if (!qa_q1_game_operation_live(&call->operation) ||
        call->source->state.q1 != call->operation.game)
        return application_fail(error, QA_ERROR_ARGUMENT, "Rogue callback lost its retained GAME");
    return application_native_q1_composition_current(call->app, call->source->owner,
            QA_MODE_ROGUE, error) && qa_modes_read(call->app->modes, call->mode, &view, error) &&
        ((view.origin == QA_MODE_NATIVE_Q1_COMPOSITION &&
          view.source_owner == call->source->owner && view.rules.source == QA_MODE_ROGUE) ||
         application_fail(error, QA_ERROR_ARGUMENT, "Rogue callback lost its genuine source controller"));
}

static bool begin(application_provider *source, rogue_call *call, qa_error *error)
{
    *call = (rogue_call){.source = source, .app = source ? source->application : NULL};
    if (!source || !call->app || source->kind != APPLICATION_PROVIDER_Q1 || !source->state.q1)
        return application_fail(error, QA_ERROR_ARGUMENT, "Rogue callback has no actual native GAME");
    return qa_q1_game_operation_begin(source->state.q1, &call->operation, error) &&
        application_native_q1_composition_mode(call->app, source, &call->mode, error) &&
        current(call, error);
}

static bool player(rogue_call *call, qa_actor_id actor, bool *found, qa_error *error)
{
    qa_q1_source_client_view client;
    *found = false;
    if (!current(call, error)) return false;
    if (!actor.registry) return true;
    if (!qa_actors_get(qa_session_actors(call->app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Rogue callback actor retired");
    if (!qa_q1_source_client_read(call->operation.game, actor, &client)) return true;
    bool observer;
    if (!application_native_q1_composition_player_current(call->app, call->source->owner,
        QA_MODE_ROGUE, actor, &observer, error)) return false;
    *found = true;
    return true;
}

static bool number(rogue_call *call, qa_actor_id actor, qa_mode_q1_number field,
    double *out, qa_error *error)
{
    bool found;
    if (!player(call, actor, &found, error)) return false;
    if (!found) return application_fail(error, QA_ERROR_ARGUMENT, "Rogue word has no source client");
    if (!qa_modes_q1_rogue_initialize(call->app->modes, call->mode, actor, error) ||
        !current(call, error) || !qa_modes_q1_source_read(call->app->modes, call->mode,
            actor, field, out, error) || !player(call, actor, &found, error)) return false;
    return found || application_fail(error, QA_ERROR_ARGUMENT, "Rogue word lost its source client");
}

static bool team(rogue_call *call, qa_actor_id actor, double *out, qa_error *error)
{
    bool found;
    if (!player(call, actor, &found, error)) return false;
    *out = 0;
    return !found || number(call, actor, QA_Q1_ROGUE_STEAM, out, error);
}

static bool policy(rogue_call *call, const char *name, double *out, qa_error *error)
{
    if (!current(call, error)) return false;
    qa_cvars *cvars = application_native_q1_console_registry(call->source);
    const qa_cvar_view *value = cvars ? qa_cvars_find(cvars, name) : NULL;
    if (!value || value->owner != call->source->owner)
        return application_fail(error, QA_ERROR_ARGUMENT, "Rogue callback lost its actual GAME policy");
    *out = qa_source_fround(value->number);
    return true;
}

static bool ctf(rogue_call *call, bool *out, qa_error *error)
{
    double mode;
    if (!policy(call, "teamplay", &mode, error)) return false;
    *out = mode == 4 || mode == 5 || mode == 6;
    return true;
}

static uint32_t bits(double value)
{
    double integer = isfinite(value) ? fmod(trunc(value), 4294967296.0) : 0;
    if (integer < 0) integer += 4294967296.0;
    return (uint32_t)integer;
}

static bool tag_current(void *context, const qa_q1_game *game, qa_error *error)
{
    application_provider *source = context;
    return source && source->kind == APPLICATION_PROVIDER_Q1 && source->state.q1 == game &&
        application_native_q1_composition_current(source->application, source->owner,
            QA_MODE_ROGUE, error);
}

static bool players_snapshot(rogue_call *call, qa_actor_id **out, size_t *count, qa_error *error)
{
    qa_q1_options options;
    double seconds;
    if (!current(call, error) || !qa_q1_source_respawn_options_read(call->operation.game,
        &options, &seconds, error)) return false;
    qa_actor_id *rows = calloc(options.max_clients ? options.max_clients : 1, sizeof(*rows));
    if (!rows) return application_fail(error, QA_ERROR_MEMORY, "Capturing Rogue source players");
    size_t used = 0;
    for (uint32_t i = 0; i < options.max_clients; ++i) {
        qa_actor_id actor;
        if (qa_q1_source_client_actor(call->operation.game, i, &actor)) rows[used++] = actor;
    }
    *out = rows;
    *count = used;
    return true;
}

static bool player_required(rogue_call *call, qa_actor_id actor, qa_error *error)
{
    bool found;
    return player(call, actor, &found, error) &&
        (found || application_fail(error, QA_ERROR_ARGUMENT, "Rogue action lost its source client"));
}

static bool message(rogue_call *call, qa_actor_id actor, const char *text, bool center,
    const qa_builtin_message_arg *args, size_t count, qa_error *error)
{
    qa_builtin_event event = {.kind = QA_BUILTIN_MESSAGE, .family = QA_GAME_Q1,
        .provider = call->source->owner, .actor = actor, .flags = center ? 0 : 2u,
        .arguments = args, .argument_count = count};
    double seconds;
    return player_required(call, actor, error) &&
        qa_strings_intern_cstr(qa_session_strings(call->app->session), text, &event.text, error) &&
        (qa_q1_game_clock_read(call->operation.game, &event.time_ns, &seconds) ||
         application_fail(error, QA_ERROR_ARGUMENT, "Rogue message lost its source clock")) &&
        application_emit(call->app, &event, error) && player_required(call, actor, error);
}

static bool broadcast(rogue_call *call, const char *text, bool center, qa_error *error)
{
    qa_actor_id *rows;
    size_t count;
    if (!players_snapshot(call, &rows, &count, error)) return false;
    bool okay = true;
    for (size_t i = 0; okay && i < count; ++i)
        okay = message(call, rows[i], text, center, NULL, 0, error);
    free(rows);
    return okay;
}

static const char *team_name(double value)
{
    return value == 5 ? "Red" : value == 14 ? "Blue" : value == 1 ? "Grey" : "UNKNOWN";
}

static bool named_broadcast(rogue_call *call, qa_actor_id actor, const char *action,
    double flag_team, bool neutral, qa_error *error)
{
    qa_q1_source_client_view client;
    if (!player_required(call, actor, error) ||
        !qa_q1_source_client_read(call->operation.game, actor, &client)) return false;
    size_t length = strlen(client.name) + strlen(action) + strlen(team_name(flag_team)) + 32;
    char *text = malloc(length);
    if (!text) return application_fail(error, QA_ERROR_MEMORY, "Formatting Rogue source announcement");
    snprintf(text, length, "%s %s the %s%sflag!\n", client.name, action,
        neutral ? "" : team_name(flag_team), neutral ? "" : " ");
    bool okay = broadcast(call, text, false, error);
    free(text);
    return okay && player_required(call, actor, error);
}

static bool source_sound(rogue_call *call, qa_actor_id actor, const char *path,
    bool global, qa_error *error)
{
    qa_body_state body;
    qa_builtin_event event = {.kind = QA_BUILTIN_SOUND, .family = QA_GAME_Q1,
        .provider = call->source->owner, .actor = actor, .channel = global ? 2 : 0,
        .volume = 1, .attenuation = global ? 0 : 1};
    double seconds;
    if (!current(call, error) || !qa_actors_get(qa_session_actors(call->app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Rogue sound actor retired");
    return qa_strings_intern_cstr(qa_session_strings(call->app->session), path, &event.resource, error) &&
        qa_world_body_read(call->app->world, actor, &body, error) && current(call, error) &&
        (qa_q1_game_clock_read(call->operation.game, &event.time_ns, &seconds) ||
         application_fail(error, QA_ERROR_ARGUMENT, "Rogue sound lost its source clock")) &&
        (event.origin = qa_vec_add(body.origin,
            qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), .5f)), true) &&
        application_emit(call->app, &event, error) && current(call, error);
}

static bool write_number(rogue_call *call, qa_actor_id actor, qa_mode_q1_number field,
    double value, qa_error *error)
{
    return player_required(call, actor, error) &&
        qa_modes_q1_source_write(call->app->modes, call->mode, actor, field, value, error) &&
        player_required(call, actor, error);
}

static bool score(rogue_call *call, qa_actor_id actor, double amount, qa_error *error)
{
    return player_required(call, actor, error) &&
        qa_q1_source_client_add_score(call->operation.game, actor, amount, error) &&
        player_required(call, actor, error);
}

static bool color(rogue_call *call, qa_actor_id actor, double *out, qa_error *error)
{
    qa_q1_source_client_view view;
    if (!player_required(call, actor, error) ||
        !qa_q1_source_client_read(call->operation.game, actor, &view)) return false;
    *out = view.team;
    return true;
}

static bool seconds(rogue_call *call, double *out, qa_error *error)
{
    uint64_t time;
    double elapsed;
    if (!current(call, error) || !qa_q1_game_clock_read(call->operation.game, &time, &elapsed))
        return application_fail(error, QA_ERROR_ARGUMENT, "Rogue action lost its actual source time");
    *out = (double)time / 1000000000.0;
    return true;
}

static bool flag_return_call(rogue_call *call, qa_actor_id actor, qa_error *error)
{
    qa_q1_source_rogue_flag_view flag;
    double mode;
    if (!qa_q1_source_rogue_flag_read(call->operation.game, actor, &flag, error) ||
        !qa_q1_source_rogue_flag_return(call->operation.game, actor, error) ||
        !current(call, error) || !policy(call, "teamplay", &mode, error)) return false;
    qa_actor_id *rows;
    size_t count;
    if (!players_snapshot(call, &rows, &count, error)) return false;
    bool okay = true;
    char text[80];
    if (mode == 6) snprintf(text, sizeof(text), "%s flag has been returned to base!\n", team_name(flag.team));
    for (size_t i = 0; okay && i < count; ++i) {
        double player_team;
        okay = team(call, rows[i], &player_team, error) &&
            message(call, rows[i], mode == 5 ? "$qc_flag_returned" : mode == 6 ? text :
                player_team == flag.team ? "$qc_your_flag_returned_base" :
                "$qc_enemy_flag_returned_base", true, NULL, 0, error);
    }
    free(rows);
    return okay;
}

static bool flag_drop_call(rogue_call *call, qa_actor_id actor, qa_error *error)
{
    qa_q1_source_rogue_flag_view flag;
    double mode;
    if (!qa_q1_source_rogue_flag_read(call->operation.game, actor, &flag, error) ||
        !policy(call, "teamplay", &mode, error)) return false;
    if (!flag.owner.registry || !qa_world_body_storage_serial(call->app->world, flag.owner))
        return flag_return_call(call, actor, error);
    return named_broadcast(call, flag.owner, "lost", flag.team, mode == 5, error) &&
        qa_q1_source_rogue_flag_drop(call->operation.game, actor, error) && current(call, error);
}

static bool flag_return(void *context, qa_actor_id actor, qa_error *error)
{
    rogue_call call = {0};
    bool okay = begin(context, &call, error) && flag_return_call(&call, actor, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

static bool flag_drop(void *context, qa_actor_id actor, qa_error *error)
{
    rogue_call call = {0};
    bool okay = begin(context, &call, error) && flag_drop_call(&call, actor, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

static bool flag_carrier(void *context, qa_actor_id actor, qa_actor_id carrier,
    bool *out, qa_error *error)
{
    rogue_call call = {0};
    bool found;
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Rogue carrier requires its source output");
    bool okay = begin(context, &call, error);
    *out = false;
    if (okay && carrier.registry && qa_actors_get(qa_session_actors(call.app->session), carrier)) {
        okay = player(&call, carrier, &found, error);
        if (okay && found) {
            qa_combat_state combat;
            qa_q1_source_rogue_flag_view flag;
            double mode, flags;
            okay = qa_combat_read(call.app->combat, carrier, &combat, error) &&
                player_required(&call, carrier, error) &&
                qa_q1_source_rogue_flag_read(call.operation.game, actor, &flag, error) &&
                number(&call, carrier, QA_Q1_ROGUE_FLAGS, &flags, error) &&
                policy(&call, "teamplay", &mode, error);
            if (okay) *out = combat.health > 0 && !(mode == 5 && !(bits(flags) & 1)) &&
                !(flag.team == 5 && !(bits(flags) & 1)) && !(flag.team == 14 && !(bits(flags) & 2));
        }
    }
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

static bool flag_frame(void *context, qa_actor_id actor, double *out, qa_error *error)
{
    rogue_call call = {0};
    bool okay = out && begin(context, &call, error) && player_required(&call, actor, error);
    application_provider *character = okay ? application_provider_for(call.app, actor, QA_ROLE_CHARACTER, "") : NULL;
    double frame = 0;
    if (okay && (!character || !character->constructed || character->close_pending))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "Rogue flag lost its selected character");
    if (okay) switch (character->kind) {
    case APPLICATION_PROVIDER_Q1: {
        qa_q1_character_view view;
        okay = qa_q1_character_read(character->state.q1, actor, &view);
        if (okay) frame = view.animation_frame;
        break;
    }
    case APPLICATION_PROVIDER_Q2: {
        qa_q2_visual view;
        okay = qa_q2_presentation_read(character->state.q2, actor, &view);
        if (okay) frame = view.frame;
        break;
    }
    case APPLICATION_PROVIDER_QC: {
        qa_application_qc_animation view;
        okay = qa_application_qc_selected_character_frame_read(call.app, actor, &view, error);
        if (okay) {
            frame = view.frame;
            okay = qa_application_qc_selected_character_frame_current(call.app, &view) ||
                application_fail(error, QA_ERROR_ARGUMENT, "Rogue flag selected QC animation changed");
        }
        break;
    }
    case APPLICATION_PROVIDER_Q3: {
        qa_q3_player_state state;
        okay = qa_q3_player_read(character->state.q3, actor, &state);
        break;
    }
    case APPLICATION_PROVIDER_QVM: {
        uint32_t slot;
        okay = character->component.clock.kind == QA_CLOCK_Q3 &&
            application_q3_guest_actor_client(character, actor, &slot);
        break;
    }
    case APPLICATION_PROVIDER_NATIVE:
        okay = application_native_q1_selected_q2_frame(call.app, actor, &frame, error);
        break;
    }
    if (!okay && (!error || error->code == QA_OK))
        application_fail(error, QA_ERROR_NOT_FOUND, "Rogue flag lost its admitted character frame");
    if (okay) okay = player_required(&call, actor, error) &&
        application_provider_for(call.app, actor, QA_ROLE_CHARACTER, "") == character &&
        character->constructed && !character->close_pending;
    if (okay) *out = frame;
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

static bool keys(rogue_call *call, qa_actor_id actor, bool clear, double flag_team, qa_error *error)
{
    const char *const names[] = {"q1:key/silver", "q1:key/gold"};
    for (size_t i = 0; i < 2; ++i) {
        if (!clear && !(flag_team == 0 || flag_team == (i ? 5 : 14))) continue;
        qa_item_id item;
        double count;
        if (!player_required(call, actor, error) ||
            !qa_strings_intern_cstr(qa_session_strings(call->app->session), names[i], &item, error)) return false;
        if (clear) {
            if (!qa_inventory_count_read(call->app->inventory, actor, item, &count, error) ||
                !player_required(call, actor, error) ||
                !qa_inventory_consume(call->app->inventory, actor, item, count, NULL, error)) return false;
        } else if (!qa_inventory_give(call->app->inventory, actor, item, 1, NULL, error)) return false;
        if (!player_required(call, actor, error)) return false;
    }
    return true;
}

static bool capture(rogue_call *call, qa_actor_id actor, bool alternate, qa_error *error)
{
    double actor_team, flags, mode;
    if (!team(call, actor, &actor_team, error) ||
        !number(call, actor, QA_Q1_ROGUE_FLAGS, &flags, error) ||
        !policy(call, "teamplay", &mode, error) ||
        !named_broadcast(call, actor, "captured", 0, true, error) ||
        !keys(call, actor, true, 0, error) || !source_sound(call, actor, "misc/flagcap.wav", true, error) ||
        !score(call, actor, alternate ? 8 : 15, error)) return false;
    qa_actor_id *rows = NULL;
    size_t count;
    bool okay = players_snapshot(call, &rows, &count, error);
    for (size_t i = 0; okay && i < count; ++i) {
        double actual_color, stamp, now, other_flags;
        okay = color(call, rows[i], &actual_color, error);
        if (!okay) break;
        if (actual_color == actor_team) {
            if (!qa_actor_id_equal(actor, rows[i])) okay = score(call, rows[i], alternate ? 4 : 10, error);
            if (okay && !alternate && mode != 5) {
                okay = number(call, rows[i], QA_Q1_ROGUE_LAST_RETURNED_FLAG, &stamp, error) &&
                    seconds(call, &now, error);
                if (okay && stamp + 4 > now) okay = score(call, rows[i], 1, error);
            }
            if (okay && !alternate) {
                okay = number(call, rows[i], QA_Q1_ROGUE_LAST_FRAGGED_CARRIER, &stamp, error) &&
                    seconds(call, &now, error);
                if (okay && stamp + 6 > now) okay = score(call, rows[i], 2, error);
            }
            if (okay) okay = message(call, rows[i], "$qc_your_team_captured", true, NULL, 0, error);
        } else okay = write_number(call, rows[i], QA_Q1_ROGUE_LAST_HURT_CARRIER, -5, error) &&
            message(call, rows[i], "$qc_your_flag_captured", true, NULL, 0, error);
        if (okay && !alternate) okay = number(call, rows[i], QA_Q1_ROGUE_FLAGS, &other_flags, error) &&
            write_number(call, rows[i], QA_Q1_ROGUE_FLAGS, bits(other_flags) & ~UINT32_C(3), error);
    }
    free(rows);
    rows = NULL;
    if (okay) okay = qa_q1_source_rogue_flags_snapshot(call->operation.game, &rows, &count, error);
    for (size_t i = 0; okay && i < count; ++i) {
        qa_q1_source_rogue_flag_view flag;
        okay = qa_q1_source_rogue_flag_read(call->operation.game, rows[i], &flag, error);
        if (okay && (alternate ? flag.team == ((bits(flags) & 1) ? 5 : 14) :
            mode == 5 ? flag.team == 0 : flag.team != 0))
            okay = qa_q1_source_rogue_flag_return(call->operation.game, rows[i], error) && current(call, error);
    }
    free(rows);
    if (okay && alternate) okay = write_number(call, actor, QA_Q1_ROGUE_FLAGS, bits(flags) & ~UINT32_C(3), error);
    return okay;
}

static bool flag_touch(void *context, qa_actor_id actor, qa_actor_id other, bool base, qa_error *error)
{
    rogue_call call = {0};
    bool found;
    bool okay = begin(context, &call, error) && player(&call, other, &found, error);
    if (!okay || !found) goto finish;
    qa_combat_state combat;
    qa_q1_source_rogue_flag_view flag;
    double own_team, actual_color, flags, mode;
    okay = qa_combat_read(call.app->combat, other, &combat, error) && player_required(&call, other, error);
    if (!okay || combat.health <= 0) goto finish;
    okay = team(&call, other, &own_team, error) && color(&call, other, &actual_color, error);
    if (!okay || actual_color != own_team) goto finish;
    okay = qa_q1_source_rogue_flag_read(call.operation.game, actor, &flag, error) &&
        number(&call, other, QA_Q1_ROGUE_FLAGS, &flags, error) && policy(&call, "teamplay", &mode, error);
    if (!okay) goto finish;
    if (base) {
        if (mode == 5 && ((flag.team == 5 && own_team == 14) || (flag.team == 14 && own_team == 5)) &&
            (bits(flags) & 1)) okay = capture(&call, other, false, error);
        else if (mode == 6 && own_team == 1 && (((bits(flags) & 1) && flag.team == 14) ||
            ((bits(flags) & 2) && flag.team == 5))) okay = capture(&call, other, true, error);
        goto finish;
    }
    if (flag.count == 1) goto finish;
    if (mode != 5) {
        if (mode != 4 && mode != 6) goto finish;
        if (flag.team == own_team) {
            if (flag.count == 0) {
                if ((flag.team == 5 && (bits(flags) & 2)) || (flag.team == 14 && (bits(flags) & 1)))
                    okay = capture(&call, other, false, error);
            } else {
                double now;
                okay = score(&call, other, 1, error) && seconds(&call, &now, error) &&
                    write_number(&call, other, QA_Q1_ROGUE_LAST_RETURNED_FLAG, now, error) &&
                    source_sound(&call, other, "misc/flagret.wav", false, error) &&
                    flag_return_call(&call, actor, error);
            }
            goto finish;
        }
        if (bits(flags) & 3) goto finish;
    }
    double now;
    okay = named_broadcast(&call, other, "got", flag.team, mode == 5, error) &&
        source_sound(&call, other, "misc/flagtk.wav", false, error) &&
        write_number(&call, other, QA_Q1_ROGUE_FLAGS, bits(flags) | (flag.team == 14 ? 2 : 1), error) &&
        seconds(&call, &now, error) && write_number(&call, other, QA_Q1_ROGUE_FLAG_SINCE, now, error) &&
        keys(&call, other, false, flag.team, error) &&
        qa_q1_source_rogue_flag_carry(call.operation.game, actor, other, error) && current(&call, error) &&
        message(&call, other, mode == 5 ? "YOU GOT THE FLAG\n\nTAKE IT TO THEIR BASE\n" :
            "YOU GOT THE ENEMY FLAG\n\nRETURN TO BASE\n", true, NULL, 0, error);
    qa_actor_id *rows = NULL;
    size_t count;
    if (okay) okay = players_snapshot(&call, &rows, &count, error);
    char text[100];
    snprintf(text, sizeof(text), "%s team has the %s flag!\n", team_name(own_team), team_name(flag.team));
    for (size_t i = 0; okay && i < count; ++i) {
        if (qa_actor_id_equal(other, rows[i])) continue;
        double recipient_team;
        okay = team(&call, rows[i], &recipient_team, error) &&
            message(&call, rows[i], mode == 5 ? "$qc_flag_taken" : recipient_team == flag.team ?
                "$qc_your_flag_taken" : text, true, NULL, 0, error);
    }
    free(rows);
finish:
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

static bool rogue_flags_configure(application_provider *source, qa_error *error)
{
    return qa_q1_source_rogue_flags_configure(source->state.q1,
        &(qa_q1_source_rogue_flags_services){.context = source, .current = tag_current,
            .touch = flag_touch, .return_flag = flag_return, .drop_flag = flag_drop,
            .player_frame = flag_frame, .carrier = flag_carrier}, error);
}

static bool assists(rogue_call *call, qa_actor_id victim, qa_actor_id attacker, qa_error *error)
{
    double victim_flags, victim_team, killer_team, now, stamp, killer_flags;
    if (!number(call, victim, QA_Q1_ROGUE_FLAGS, &victim_flags, error) ||
        !team(call, victim, &victim_team, error) || !team(call, attacker, &killer_team, error)) return false;
    if ((bits(victim_flags) & 3) && victim_team != killer_team) {
        if (!seconds(call, &now, error) ||
            !write_number(call, attacker, QA_Q1_ROGUE_LAST_FRAGGED_CARRIER, now, error) ||
            !number(call, victim, QA_Q1_ROGUE_FLAG_SINCE, &stamp, error) ||
            !seconds(call, &now, error)) return false;
        if (stamp + 2 <= now) {
            qa_builtin_message_arg arg = {.kind = QA_BUILTIN_MESSAGE_NUMBER, .value.number = 2};
            if (!score(call, attacker, 2, error) ||
                !message(call, attacker, "$qc_enemy_killed_bonus", false, &arg, 1, error)) return false;
        } else if (!message(call, attacker, "$qc_enemy_killed_no_bonus", false, NULL, 0, error)) return false;
    }
    bool carrier_bonus = false, flag_bonus = false;
    if (!number(call, victim, QA_Q1_ROGUE_LAST_HURT_CARRIER, &stamp, error) ||
        !seconds(call, &now, error) || !number(call, attacker, QA_Q1_ROGUE_FLAGS, &killer_flags, error)) return false;
    if (stamp + 4 > now && !(bits(killer_flags) & 3)) {
        if (!score(call, attacker, 2, error)) return false;
        carrier_bonus = true;
    }
    qa_actor_id origins[] = {attacker, victim};
    for (size_t side = 0; side < 2; ++side) {
        if (!qa_world_body_storage_serial(call->app->world, origins[side])) continue;
        qa_body_state origin;
        if (!qa_world_body_read(call->app->world, origins[side], &origin, error) || !current(call, error)) return false;
        const qa_actor_registry *registry = qa_session_actors(call->app->session);
        size_t count = qa_actors_count(registry), used = 0;
        qa_actor_id *rows = count ? malloc(count * sizeof(*rows)) : NULL;
        if (count && !rows) return application_fail(error, QA_ERROR_MEMORY, "Capturing Rogue assist observations");
        uint32_t cursor = 0;
        const qa_actor_record *record;
        while (qa_actors_next(registry, &cursor, &record)) rows[used++] = record->id;
        bool okay = true;
        for (size_t i = used; okay && i; --i) {
            qa_actor_id actor = rows[i - 1];
            if (!qa_actors_get(registry, actor) || !qa_world_body_storage_serial(call->app->world, actor)) continue;
            qa_body_state body;
            okay = qa_world_body_read(call->app->world, actor, &body, error) && current(call, error);
            if (!okay) break;
            qa_vec3 center = qa_vec_add(body.origin, qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), .5f));
            if (qa_vec_length(qa_vec_sub(center, origin.origin)) > 400) continue;
            bool found;
            okay = player(call, actor, &found, error);
            if (okay && found) {
                double actor_team, actor_flags;
                okay = team(call, actor, &actor_team, error) && number(call, actor, QA_Q1_ROGUE_FLAGS, &actor_flags, error);
                if (okay && actor_team == killer_team && (bits(actor_flags) & 3) &&
                    !qa_actor_id_equal(actor, attacker) && !carrier_bonus) {
                    okay = score(call, attacker, 1, error);
                    carrier_bonus = true;
                }
            }
            qa_q1_bot_entity entity;
            if (okay) okay = qa_q1_bot_entity_read(call->operation.game, actor, &entity, error) && current(call, error);
            if (okay && entity.present) {
                const char *name = qa_strings_cstr(qa_session_strings(call->app->session), entity.classname);
                if ((killer_team == 5 && name && !strcmp(name, "item_flag_team1")) ||
                    (killer_team == 14 && name && !strcmp(name, "item_flag_team2")) ||
                    (name && !strcmp(name, "item_flag") && (side == 0 || !flag_bonus))) {
                    okay = score(call, attacker, 1, error);
                    flag_bonus = true;
                }
            }
        }
        free(rows);
        if (!okay) return false;
    }
    return true;
}

bool application_native_q1_rogue_player_died(application_provider *source,
    qa_actor_id actor, qa_actor_id attacker, qa_error *error)
{
    rogue_call call = {0};
    bool okay = begin(source, &call, error) && player_required(&call, actor, error);
    double killed, mode, flags;
    if (okay) okay = number(&call, actor, QA_Q1_ROGUE_KILLED, &killed, error) &&
        write_number(&call, actor, QA_Q1_ROGUE_KILLED, killed == 2 ? 0 : 1, error) &&
        policy(&call, "teamplay", &mode, error);
    if (!okay || (mode != 4 && mode != 5 && mode != 6)) goto finish;
    qa_q1_source_obituary_actor projectile;
    if (attacker.registry) {
        okay = qa_q1_source_obituary_read(call.operation.game, attacker, &projectile, error) && current(&call, error);
        const char *name = okay ? qa_strings_cstr(qa_session_strings(call.app->session), projectile.classname) : NULL;
        if (name && !strcmp(name, "power_shield")) attacker = projectile.owner;
    }
    bool found = false;
    if (okay && attacker.registry) okay = player(&call, attacker, &found, error);
    if (okay && found && !qa_actor_id_equal(actor, attacker)) okay = assists(&call, actor, attacker, error);
    if (okay) okay = number(&call, actor, QA_Q1_ROGUE_FLAGS, &flags, error);
    if (okay && (bits(flags) & 3)) {
        qa_actor_id *rows;
        size_t count;
        okay = players_snapshot(&call, &rows, &count, error);
        for (size_t i = 0; okay && i < count; ++i) {
            double value;
            okay = team(&call, rows[i], &value, error);
            if (okay && (mode == 5 || ((bits(flags) & 1) && value == 5) || ((bits(flags) & 2) && value == 14)))
                okay = write_number(&call, rows[i], QA_Q1_ROGUE_LAST_HURT_CARRIER, -10, error);
        }
        free(rows);
    }
    qa_actor_id *rows = NULL;
    size_t count;
    if (okay) okay = qa_q1_source_rogue_flags_snapshot(call.operation.game, &rows, &count, error);
    for (size_t i = 0; okay && i < count; ++i) {
        qa_q1_source_rogue_flag_view flag;
        okay = qa_q1_source_rogue_flag_read(call.operation.game, rows[i], &flag, error);
        if (okay && flag.team == (mode == 5 && (bits(flags) & 1) ? 0 :
            (bits(flags) & 1) ? 5 : (bits(flags) & 2) ? 14 : -1)) {
            okay = write_number(&call, actor, QA_Q1_ROGUE_FLAGS, bits(flags) & ~UINT32_C(3), error) &&
                flag_drop_call(&call, rows[i], error);
            break;
        }
    }
    free(rows);
finish:
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

static bool tag_player(void *context, qa_actor_id actor, bool *found, qa_error *error)
{
    rogue_call call = {0};
    bool okay = begin(context, &call, error) && player(&call, actor, found, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

static bool tag_announce(void *context, const char *text, qa_actor_id actor, qa_error *error)
{
    rogue_call call = {0};
    qa_q1_options options;
    qa_q1_source_client_view client;
    qa_builtin_message_arg argument = {.kind = QA_BUILTIN_MESSAGE_STRING};
    qa_string_id key;
    double seconds;
    bool found;
    qa_actor_id *players = NULL;
    bool okay = begin(context, &call, error) && player(&call, actor, &found, error);
    if (okay && !found) okay = application_fail(error, QA_ERROR_ARGUMENT,
        "Rogue token announcement lost its actual source player");
    if (okay) okay = qa_q1_source_client_read(call.operation.game, actor, &client) &&
        qa_strings_intern_cstr(qa_session_strings(call.app->session), client.name,
            &argument.value.text, error) &&
        qa_strings_intern_cstr(qa_session_strings(call.app->session), text, &key, error) &&
        qa_q1_source_respawn_options_read(call.operation.game, &options, &seconds, error) &&
        current(&call, error);
    size_t count = 0;
    if (okay) {
        players = calloc(options.max_clients ? options.max_clients : 1, sizeof(*players));
        if (!players) okay = application_fail(error, QA_ERROR_MEMORY, "Capturing Rogue token recipients");
    }
    for (uint32_t slot = 0; okay && slot < options.max_clients; ++slot) {
        qa_actor_id recipient;
        if (qa_q1_source_client_actor(call.operation.game, slot, &recipient)) players[count++] = recipient;
    }
    for (size_t i = 0; okay && i < count; ++i) {
        uint64_t time;
        okay = player(&call, players[i], &found, error);
        if (okay && !found) okay = application_fail(error, QA_ERROR_ARGUMENT,
            "Rogue token recipient retired during announcement");
        if (okay) okay = qa_q1_game_clock_read(call.operation.game, &time, &seconds);
        if (okay) okay = application_emit(call.app, &(qa_builtin_event){.kind = QA_BUILTIN_MESSAGE,
            .family = QA_GAME_Q1, .provider = call.source->owner, .actor = players[i],
            .time_ns = time, .text = key, .flags = 2u, .arguments = &argument,
            .argument_count = 1}, error) && current(&call, error);
    }
    free(players);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

static bool tag_spawn_point(void *context, qa_actor_id *out, qa_error *error)
{
    rogue_call call = {0};
    bool okay = out && begin(context, &call, error);
    struct application_player_roster *roster = okay ? call.app->players : NULL;
    if (okay && (!roster || roster->map_provider != call.source || !roster->q1_selector))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "Rogue token lost its actual base spawn selector");
    if (okay) okay = qa_q1_spawn_select(roster->q1_selector, roster->q1_points,
        roster->q1_point_count, true, out, error) && current(&call, error);
    if (okay && !out->registry)
        okay = application_fail(error, QA_ERROR_NOT_FOUND, "Rogue token has no genuine base spawn point");
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_rogue_world_configure(application_provider *source, qa_error *error)
{
    qa_q1_options options;
    double seconds;
    if (!source || source->kind != APPLICATION_PROVIDER_Q1 || !source->state.q1 ||
        !qa_q1_source_respawn_options_read(source->state.q1, &options, &seconds, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Rogue world binding lost its native constructor");
    if (options.program != QA_Q1_ROGUE) return true;
    return qa_q1_source_rogue_tag_configure(source->state.q1,
        &(qa_q1_source_rogue_tag_services){.context = source, .current = tag_current,
            .player = tag_player, .announce = tag_announce, .spawn_point = tag_spawn_point}, error) &&
        rogue_flags_configure(source, error);
}

bool application_native_q1_rogue_tag_score(void *context, qa_actor_id victim,
    qa_actor_id attacker, int32_t *points, qa_error *error)
{
    rogue_call call = {0};
    bool found;
    bool okay = begin(context, &call, error) && player(&call, victim, &found, error);
    if (okay && !found) okay = application_fail(error, QA_ERROR_ARGUMENT,
        "Rogue token score lost its source victim");
    if (okay) okay = qa_q1_source_rogue_tag_score(call.operation.game, victim, attacker, points, error) &&
        current(&call, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_rogue_confirmed_damage(application_provider *source,
    qa_actor_id target, qa_actor_id attacker, qa_error *error)
{
    rogue_call call = {0};
    bool enabled, target_player, attacker_player;
    bool okay = begin(source, &call, error) && ctf(&call, &enabled, error);
    if (!okay || !enabled || !attacker.registry) goto finish;
    okay = player(&call, target, &target_player, error);
    if (!okay || !target_player) goto finish;
    okay = player(&call, attacker, &attacker_player, error);
    if (!okay || !attacker_player) goto finish;
    double flags, target_team, attacker_team;
    okay = number(&call, target, QA_Q1_ROGUE_FLAGS, &flags, error);
    if (!okay || !(bits(flags) & 3)) goto finish;
    okay = team(&call, target, &target_team, error) && team(&call, attacker, &attacker_team, error);
    if (!okay || target_team == attacker_team) goto finish;
    qa_q1_options options;
    double seconds;
    okay = qa_q1_source_respawn_options_read(call.operation.game, &options, &seconds, error) &&
        current(&call, error) && qa_modes_q1_source_write(call.app->modes, call.mode,
            attacker, QA_Q1_ROGUE_LAST_HURT_CARRIER, seconds, error) && current(&call, error);
finish:
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_rogue_before_fire(application_provider *source,
    qa_actor_id actor, qa_error *error)
{
    rogue_call call = {0};
    bool okay = begin(source, &call, error) && player_required(&call, actor, error) &&
        qa_q1_source_rogue_runes_before_fire(call.operation.game, actor, error) &&
        player_required(&call, actor, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

bool application_native_q1_rogue_attack_delay(application_provider *source,
    qa_actor_id actor, qa_q1_weapon weapon, float *delay, bool observation, qa_error *error)
{
    if (!delay) return application_fail(error, QA_ERROR_ARGUMENT, "Rogue haste needs an actual delay");
    switch (weapon) {
    case QA_Q1_AXE: case QA_Q1_SHOTGUN: case QA_Q1_SUPER_SHOTGUN:
    case QA_Q1_GRENADE: case QA_Q1_ROCKET:
    case QA_Q1_MULTI_GRENADE: case QA_Q1_MULTI_ROCKET: case QA_Q1_PLASMA:
        break;
    default: return true;
    }
    rogue_call call = {0};
    bool okay = begin(source, &call, error) && player_required(&call, actor, error);
    if (okay && observation) {
        uint32_t rune;
        bool found;
        okay = qa_q1_source_rogue_runes_read(call.operation.game, actor, &rune, &found, error) &&
            player_required(&call, actor, error);
        if (okay && found && (rune & 4))
            *delay = (float)qa_source_fround(qa_source_fround((double)*delay * 2) / 3);
    } else if (okay) okay = qa_q1_source_rogue_runes_attack_delay(call.operation.game, actor, delay, error) &&
        player_required(&call, actor, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

static bool legal_team(double mode, double value)
{
    return mode < 4 ? value > 0 : mode == 4 || mode == 5 || mode == 6 ?
        value == 5 || value == 14 || (mode == 6 && value == 1) : true;
}

static bool flag_report(rogue_call *call, qa_actor_id actor,
    const qa_q1_source_rogue_flag_view *flag, const char *label, bool own,
    bool single, double mode, qa_error *error)
{
    char buffer[512], *text = buffer, *owned = NULL;
    size_t capacity = sizeof(buffer);
    const char *key = NULL;
    if (flag && flag->count == 1 && flag->owner.registry) {
        qa_q1_source_client_view owner;
        if (!player_required(call, flag->owner, error) ||
            !qa_q1_source_client_read(call->operation.game, flag->owner, &owner)) return false;
        size_t name_size = strlen(owner.name) + 1;
        if (name_size > (SIZE_MAX - strlen(label) - 128) / 2)
            return application_fail(error, QA_ERROR_MEMORY, "Rogue flag announcement exceeds its source extent");
        capacity = name_size + strlen(label) + 128;
        owned = malloc(capacity + name_size);
        if (!owned) return application_fail(error, QA_ERROR_MEMORY, "Retaining the Rogue flag owner name");
        text = owned;
        char *name = owned + capacity;
        memcpy(name, owner.name, name_size);
        bool self = qa_actor_id_equal(actor, flag->owner);
        if (single && self) key = "$qc_you_have_flag";
        else if (mode == 4 && self) key = "$qc_you_have_enemy_flag";
        else if (self) snprintf(text, capacity, "You have the %s flag!\n", label);
        else if (mode == 4) snprintf(text, capacity, "%s has %s flag.\n",
            name, own ? "your" : "the enemy");
        else {
            double owner_team;
            if (!team(call, flag->owner, &owner_team, error)) { free(owned); return false; }
            if (single) snprintf(text, capacity, "%s of the %s team has the flag!\n",
                name, team_name(owner_team));
            else snprintf(text, capacity, "%s of the %s team has the %s flag.\n",
                name, team_name(owner_team), label);
        }
    } else if (single) key = !flag ? "$qc_flag_missing" : flag->count == 0 ?
        "$qc_flag_at_base" : flag->count == 2 ? "$qc_flag_lying_about" : "$qc_flag_screwed_up";
    else {
        const char *place = !flag ? "missing!" : flag->count == 0 ?
            mode == 6 ? "at base." : own ? "in your base." : "in their base." :
            flag->count == 2 ? "lying about." : " corrupt.";
        snprintf(text, capacity, "%s is %s\n", label, place);
    }
    bool okay = message(call, actor, key ? key : text, false, NULL, 0, error);
    free(owned);
    return okay;
}

bool application_native_q1_rogue_impulse(application_provider *source,
    qa_actor_id actor, int32_t impulse, bool *handled, qa_error *error)
{
    if (!handled) return application_fail(error, QA_ERROR_ARGUMENT, "Rogue impulse needs its consumption result");
    *handled = false;
    if (impulse != 23) return true;
    rogue_call call = {0};
    bool okay = begin(source, &call, error) && player_required(&call, actor, error);
    double deathmatch, mode;
    if (okay) okay = policy(&call, "deathmatch", &deathmatch, error) &&
        policy(&call, "teamplay", &mode, error);
    if (okay && deathmatch != 0 && mode != 4 && mode != 5 && mode != 6)
        okay = message(&call, actor, "$qc_ctf_disabled", false, NULL, 0, error);
    if (okay && deathmatch != 0 && (mode == 4 || mode == 5 || mode == 6)) {
        qa_actor_id *flags = NULL;
        size_t count;
        okay = qa_q1_source_rogue_flags_snapshot(call.operation.game, &flags, &count, error) &&
            player_required(&call, actor, error);
        qa_q1_source_rogue_flag_view red = {0}, blue = {0}, neutral = {0};
        bool has_red = false, has_blue = false, has_neutral = false;
        for (size_t i = 0; okay && i < count; ++i) {
            qa_q1_source_rogue_flag_view view;
            okay = qa_q1_source_rogue_flag_read(call.operation.game, flags[i], &view, error);
            if (!okay) break;
            if (view.team == 5 && !has_red) { red = view; has_red = true; }
            else if (view.team == 14 && !has_blue) { blue = view; has_blue = true; }
            else if (view.team == 0 && !has_neutral) { neutral = view; has_neutral = true; }
        }
        free(flags);
        if (okay && mode == 5) okay = flag_report(&call, actor,
            has_neutral ? &neutral : NULL, "", false, true, mode, error);
        else if (okay) {
            double actual_color, actual_team;
            okay = color(&call, actor, &actual_color, error) && team(&call, actor, &actual_team, error);
            const qa_q1_source_rogue_flag_view *ordered[2] = {
                has_red ? &red : NULL, has_blue ? &blue : NULL};
            if (mode == 4 && actual_color != 5) {
                ordered[0] = has_blue ? &blue : NULL;
                ordered[1] = has_red ? &red : NULL;
            }
            for (unsigned i = 0; okay && i < 2; ++i) {
                bool own = mode == 4 ? i == 0 : has_red && actual_team == red.team;
                const char *label = own ? "Your flag" : mode == 4 ? "The enemy flag" :
                    i == 0 ? "Red flag" : "Blue flag";
                /* Ownership names use the source's red/blue list ordinal even
                 * when mode four reverses the queried flag order. */
                if (ordered[i] && ordered[i]->count == 1 && ordered[i]->owner.registry && mode != 4)
                    label = i == 0 ? "Red" : "Blue";
                okay = flag_report(&call, actor, ordered[i], label, own, false, mode, error);
            }
        }
    }
    if (okay) okay = qa_q1_source_client_consume_impulse(call.operation.game, actor, error) &&
        player_required(&call, actor, error);
    if (okay) *handled = true;
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

static bool set_color(rogue_call *call, qa_actor_id actor, double value, qa_error *error)
{
    double integer = trunc(value) - 1;
    if (!isfinite(integer) || integer < INT32_MIN || integer > INT32_MAX)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Rogue color exceeds the source client field");
    return player_required(call, actor, error) &&
        qa_q1_source_client_colors(call->operation.game, actor, (int32_t)integer,
            (int32_t)integer, error) && player_required(call, actor, error);
}

static bool team_update(rogue_call *call, qa_error *error)
{
    double next, now, mode, deathmatch;
    if (!qa_q1_rogue_world_update_read(call->operation.game, &next, error) ||
        !seconds(call, &now, error) || !policy(call, "teamplay", &mode, error) ||
        !policy(call, "deathmatch", &deathmatch, error)) return false;
    if (next > now || mode < 1 || deathmatch == 0) return true;
    if (!qa_q1_rogue_world_update_write(call->operation.game, now + 120, error) ||
        !current(call, error)) return false;
    bool enabled;
    if (!ctf(call, &enabled, error)) return false;
    if (!enabled) return true;
    qa_actor_id *rows;
    size_t count;
    if (!players_snapshot(call, &rows, &count, error)) return false;
    double totals[3] = {0};
    bool okay = true;
    for (size_t i = 0; okay && i < count; ++i) {
        double actual_team;
        qa_q1_source_client_view client;
        okay = team(call, rows[i], &actual_team, error) && player_required(call, rows[i], error);
        if (okay && !qa_q1_source_client_read(call->operation.game, rows[i], &client))
            okay = application_fail(error, QA_ERROR_ARGUMENT, "Rogue score update lost its source client");
        if (okay && actual_team == 5) totals[0] += client.frags;
        else if (okay && actual_team == 14) totals[1] += client.frags;
        else if (okay && actual_team == 1) totals[2] += client.frags;
    }
    free(rows);
    if (!okay) return false;
    unsigned order[3] = {0, 1, 2}, length = mode == 6 ? 3 : 2;
    for (unsigned i = 1; i < length; ++i) {
        unsigned item = order[i], j = i;
        while (j && totals[item] > totals[order[j - 1]]) { order[j] = order[j - 1]; --j; }
        order[j] = item;
    }
    static const double teams[3] = {5, 14, 1};
    char amount[32], text[160];
    if (totals[order[0]] > totals[order[1]]) {
        if (!qa_format_ecmascript_number(totals[order[0]] - totals[order[1]], amount, error)) return false;
        snprintf(text, sizeof(text), "%s team is leading by %s points!\n", team_name(teams[order[0]]), amount);
    } else {
        unsigned first = totals[0] == totals[1] ? 0 : totals[2] == totals[1] ? 1 : 0;
        unsigned second = totals[0] == totals[1] ? 1 : 2;
        if (!qa_format_ecmascript_number(totals[order[0]], amount, error)) return false;
        snprintf(text, sizeof(text), "%s and %s teams are tied with %s points!\n",
            team_name(teams[first]), team_name(teams[second]), amount);
    }
    return broadcast(call, text, false, error);
}

static bool mark_team_death(rogue_call *call, qa_actor_id actor, qa_error *error)
{
    double killed;
    if (!number(call, actor, QA_Q1_ROGUE_KILLED, &killed, error)) return false;
    if (killed != 1 && !write_number(call, actor, QA_Q1_ROGUE_KILLED, 2, error)) return false;
    return qa_q1_source_damage(call->operation.game, actor, actor, actor, 1000, "", error) &&
        player_required(call, actor, error);
}

static bool team_frame(rogue_call *call, qa_actor_id actor, qa_error *error)
{
    double actual_color, mode, deathmatch, flags, steam;
    if (!number(call, actor, QA_Q1_ROGUE_STEAM, &steam, error) ||
        !color(call, actor, &actual_color, error) || !team_update(call, error) ||
        !player_required(call, actor, error) || !policy(call, "deathmatch", &deathmatch, error) ||
        !policy(call, "teamplay", &mode, error)) return false;
    if (deathmatch == 0 || mode < 4)
        return write_number(call, actor, QA_Q1_ROGUE_STEAM, actual_color, error);
    if (!number(call, actor, QA_Q1_ROGUE_FLAGS, &flags, error)) return false;
    if (bits(flags) & 4) {
        if (!write_number(call, actor, QA_Q1_ROGUE_FLAGS, bits(flags) & ~UINT32_C(4), error) ||
            !number(call, actor, QA_Q1_ROGUE_STEAM, &steam, error)) return false;
        return set_color(call, actor, steam, error);
    }
    if (!number(call, actor, QA_Q1_ROGUE_STEAM, &steam, error)) return false;
    if (!legal_team(mode, actual_color) && actual_color == steam &&
        !write_number(call, actor, QA_Q1_ROGUE_STEAM, -1, error)) return false;
    if (!number(call, actor, QA_Q1_ROGUE_STEAM, &steam, error)) return false;
    if (actual_color == steam) return true;
    double gamecfg;
    if (!policy(call, "gamecfg", &gamecfg, error)) return false;
    if (steam >= 0 && legal_team(mode, steam) && !(bits(gamecfg) & 16)) {
        double suicides;
        if (!number(call, actor, QA_Q1_ROGUE_SUICIDE_COUNT, &suicides, error)) return false;
        if (suicides > 3) {
            if (!message(call, actor, "$qc_color_games", false, NULL, 0, error) ||
                !qa_session_release(call->app->session, actor, error)) return false;
            if (!player_required(call, actor, error)) return false;
        }
        if (!mark_team_death(call, actor, error) ||
            !number(call, actor, QA_Q1_ROGUE_SUICIDE_COUNT, &suicides, error) ||
            !write_number(call, actor, QA_Q1_ROGUE_SUICIDE_COUNT, suicides + 1, error) ||
            !message(call, actor, "$qc_cannot_change_teams", false, NULL, 0, error)) return false;
        return set_color(call, actor, steam, error);
    }
    if (steam >= 0 && !legal_team(mode, steam) &&
        !write_number(call, actor, QA_Q1_ROGUE_STEAM, -50, error)) return false;
    if (!number(call, actor, QA_Q1_ROGUE_STEAM, &steam, error)) return false;
    if (steam > 0 && !mark_team_death(call, actor, error)) return false;
    qa_q1_source_client_view client;
    if (!player_required(call, actor, error) ||
        !qa_q1_source_client_read(call->operation.game, actor, &client)) return false;
    return score(call, actor, -client.frags, error) &&
        application_native_q1_composition_birth(call->source, actor, false, error) &&
        player_required(call, actor, error);
}

bool application_native_q1_rogue_after_physics(application_provider *source,
    qa_actor_id actor, qa_error *error)
{
    qa_q1_options options;
    double source_seconds;
    if (!source || source->kind != APPLICATION_PROVIDER_Q1 || !source->state.q1 ||
        !qa_q1_source_respawn_options_read(source->state.q1, &options, &source_seconds, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Rogue frame lost its native source GAME");
    if (options.program != QA_Q1_ROGUE) return true;
    rogue_call call = {0};
    bool okay = begin(source, &call, error) && player_required(&call, actor, error) &&
        team_frame(&call, actor, error) &&
        qa_q1_source_rogue_runes_frame(call.operation.game, actor, error) &&
        player_required(&call, actor, error);
    qa_q1_game_operation_end(&call.operation);
    return okay;
}

static bool different_teams(rogue_call *call, const qa_damage_request *request,
    bool *out, qa_error *error)
{
    double attacker, target;
    if (!team(call, request->attack.attacker, &attacker, error) ||
        !team(call, request->target, &target, error)) return false;
    *out = attacker != target;
    return true;
}

bool application_native_q1_rogue_damage_effect(application_provider *source,
    qa_damage_effect_stage stage, const qa_damage_request *request,
    qa_damage_effect *effect, qa_error *error)
{
    if (!request || !effect)
        return application_fail(error, QA_ERROR_ARGUMENT, "Rogue damage needs its actual request and effect");
    if (stage != QA_DAMAGE_ARMOR_ALLOWED && stage != QA_DAMAGE_BEFORE_HEALTH &&
        stage != QA_DAMAGE_AFTER_QUAD && stage != QA_DAMAGE_AFTER_ARMOR) return true;
    rogue_call call = {0};
    bool allowed = true, enabled, different;
    bool self = request->attack.attacker.registry &&
        qa_actor_id_equal(request->attack.attacker, request->target);
    bool okay = begin(source, &call, error);
    double mode;
    if (okay && (stage == QA_DAMAGE_AFTER_QUAD || stage == QA_DAMAGE_AFTER_ARMOR)) {
        okay = policy(&call, "deathmatch", &mode, error);
        if (okay && mode != 0) {
            if (stage == QA_DAMAGE_AFTER_QUAD && request->attack.attacker.registry)
                okay = qa_q1_source_rogue_runes_damage(call.operation.game,
                    request->attack.attacker, &effect->amount, error) && current(&call, error);
            else if (stage == QA_DAMAGE_AFTER_ARMOR)
                okay = qa_q1_source_rogue_runes_resistance(call.operation.game,
                    request->target, &effect->amount, error) && current(&call, error);
        }
        goto finish;
    }
    if (okay && stage == QA_DAMAGE_BEFORE_HEALTH) {
        okay = policy(&call, "teamplay", &mode, error);
        if (!okay || mode <= 0) goto finish;
        if (mode == 1) {
            okay = different_teams(&call, request, &different, error);
            if (!okay) goto finish;
            if (!different) { allowed = false; goto finish; }
        }
    }
    if (okay) okay = ctf(&call, &enabled, error);
    if (!okay || !enabled || self) goto finish;
    okay = different_teams(&call, request, &different, error);
    if (!okay || different) goto finish;
    double gamecfg;
    okay = policy(&call, "gamecfg", &gamecfg, error);
    if (okay) allowed = (bits(gamecfg) & (stage == QA_DAMAGE_ARMOR_ALLOWED ? 2 : 4)) != 0;
finish:
    if (okay) effect->allowed = effect->allowed && allowed;
    qa_q1_game_operation_end(&call.operation);
    return okay;
}
