/* Match control from id Software's code/game/g_main.c and its TypeScript port.
 * Copyright (C) 1999-2005 Id Software, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "native_q3_match.h"
#include "native_q3_clients.h"
#include "native_q3_console.h"
#include "native_q3_log.h"
#include "unified_q3_events.h"
#include "native_q3_postgame.h"
#include "native_q3_session.h"
#include "native_q3_settings.h"
#include "native_q3_wire_state.h"
#include "map_players_private.h"
#include "bots_private.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_configstrings.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"

#include <stdio.h>
#include <string.h>

static bool source_live(const application_provider *provider, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 ||
        !provider->application || !provider->application->session ||
        provider->application->destroy_requested || !provider->state.q3 ||
        !provider->constructed || !provider->attached || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 match state has no live GAME owner");
    return true;
}

bool application_native_q3_match_intermission(application_provider *provider,
    int32_t *out, qa_error *error)
{
    if (!out)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 intermission query requires output");
    qa_q3_source_match_state state;
    if (!source_live(provider, error) ||
        !qa_q3_source_match_state_read(provider->state.q3, &state, error)) return false;
    *out = state.intermission_time_ms;
    return true;
}

bool application_native_q3_match_warmup(application_provider *provider,
    int32_t *out, qa_error *error)
{
    if (!out)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Q3 warmup query requires output");
    qa_q3_source_team_state state;
    if (!source_live(provider, error) ||
        !qa_q3_source_team_state_read(provider->state.q3, &state, error)) return false;
    *out = state.warmup_time_ms;
    return true;
}

typedef struct match_scope {
    application_provider *provider;
    qa_application *application;
    qa_q3_game *game;
    qa_modes *modes;
    qa_mode_id mode;
    qa_actor_owner owner;
    uint64_t publication_generation, command_generation, map_revision, time_ns;
    uint32_t maximum;
    int32_t time, start_time;
    qa_q3_product product;
    bool selected_q3;
} match_scope;

static int32_t signed_bits(uint32_t bits)
{
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool live(const match_scope *scope, qa_error *error)
{
    application_provider *provider = scope->provider;
    qa_application *app = scope->application;
    if (!source_live(provider, error)) return false;
    if (provider->application != app || provider->state.q3 != scope->game ||
        provider->owner != scope->owner || app->modes != scope->modes ||
        !app->primary_mode_ready || app->primary_mode.slot != scope->mode.slot ||
        app->primary_mode.generation != scope->mode.generation ||
        app->publication_generation != scope->publication_generation ||
        app->command_generation != scope->command_generation ||
        app->map_revision != scope->map_revision ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != provider)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "native Q3 match source changed during its callback");
    int32_t time;
    return qa_q3_source_clock(scope->game, &time, error) &&
        (time == scope->time || application_fail(error, QA_ERROR_ARGUMENT,
            "native Q3 match callback advanced its source clock"));
}

static bool begin(application_provider *provider, match_scope *scope, qa_error *error)
{
    if (!source_live(provider, error)) return false;
    qa_application *app = provider->application;
    if (!app->modes || !app->primary_mode_ready)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "native Q3 match requires the selected rule owner");
    *scope = (match_scope){.provider = provider, .application = app,
        .game = provider->state.q3, .modes = app->modes,
        .mode = app->primary_mode, .owner = provider->owner,
        .publication_generation = app->publication_generation,
        .command_generation = app->command_generation, .map_revision = app->map_revision};
    qa_mode_view mode;
    if (!qa_q3_source_clock(scope->game, &scope->time, error) || !live(scope, error) ||
        !qa_modes_read(scope->modes, scope->mode, &mode, error) ||
        !qa_q3_source_max_clients(scope->game, &scope->maximum, error) ||
        !qa_q3_source_match_context_read(scope->game, &scope->product, &scope->start_time, error))
        return false;
    application_provider *rule_provider = application_mode_provider(app, scope->mode);
    scope->selected_q3 = rule_provider && rule_provider->kind == APPLICATION_PROVIDER_Q3 &&
        application_native_q3_mode_source_provider(app, scope->mode) == provider &&
        mode.rules.source >= QA_MODE_Q3 &&
        mode.rules.source <= QA_MODE_TEAM_ARENA && mode.rules.kind >= QA_MODE_FFA &&
        mode.rules.kind <= QA_MODE_HARVESTER;
    scope->time_ns = (uint64_t)(uint32_t)scope->time * UINT64_C(1000000);
    qa_source_frame frame;
    if (qa_session_active_frame(app->session, scope->owner, &frame) &&
        frame.kind == QA_CLOCK_Q3 &&
        (uint32_t)(frame.time_ns / UINT64_C(1000000)) == (uint32_t)scope->time)
        scope->time_ns = frame.time_ns;
    return application_native_q3_console_borrow(provider, error);
}

static bool finish(match_scope *scope, bool result, qa_error *error)
{
    if (result) result = live(scope, error);
    application_native_q3_console_release(scope->provider);
    return result;
}

static bool client(const match_scope *scope, uint32_t slot, qa_q3_native_client *out,
    qa_q3_source_binding *binding, qa_error *error)
{
    if (!live(scope, error) || slot >= scope->maximum ||
        !qa_q3_client_slot_read(scope->game, slot, out, error) ||
        !qa_q3_source_binding_read(scope->game, slot, binding, error)) return false;
    if (out->connected == QA_Q3_CLIENT_DISCONNECTED) return true;
    uint32_t actual;
    if (!binding->actor.registry ||
        !qa_actors_get(qa_session_actors(scope->application->session), binding->actor) ||
        !qa_q3_native_client_slot(scope->game, binding->actor, &actual, error) || actual != slot)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "Q3 match client lost its physical source binding");
    return true;
}

static bool client_live(const match_scope *scope, uint32_t slot, qa_actor_id actor,
    qa_error *error)
{
    qa_q3_native_client value;
    qa_q3_source_binding binding;
    return client(scope, slot, &value, &binding, error) &&
        (qa_actor_id_equal(binding.actor, actor) ||
         application_fail(error, QA_ERROR_NOT_FOUND, "Q3 match callback replaced its source client"));
}

static bool rules(const match_scope *scope, qa_mode_view *mode, qa_mode_q3_settings *settings,
    qa_error *error)
{
    const application_native_q3_cvar_snapshot *warmup;
    if (!live(scope, error) || !scope->selected_q3 ||
        !qa_modes_read(scope->modes, scope->mode, mode, error) ||
        !application_native_q3_settings_integer(scope->provider, "g_doWarmup", &settings->do_warmup, error) ||
        !application_native_q3_settings_integer(scope->provider, "g_warmup", &settings->warmup_seconds, error) ||
        !application_native_q3_settings_integer(scope->provider, "timelimit", &settings->time_limit_minutes, error) ||
        !application_native_q3_settings_integer(scope->provider, "fraglimit", &settings->frag_limit, error) ||
        !application_native_q3_settings_integer(scope->provider, "capturelimit", &settings->capture_limit, error) ||
        !application_native_q3_settings_snapshot(scope->provider, "g_warmup", &warmup, error)) return false;
    settings->warmup_modification_count = warmup->modification_count;
    return true;
}

static bool source_state(const match_scope *scope, qa_q3_source_match_state *match,
    qa_q3_source_team_state *team, qa_q3_source_client_counts *counts, qa_error *error)
{
    return live(scope, error) && qa_q3_source_match_state_read(scope->game, match, error) &&
        qa_q3_source_team_state_read(scope->game, team, error) &&
        qa_q3_source_client_counts_read(scope->game, counts, error);
}

static bool config(const match_scope *scope, uint32_t index, const char *text, qa_error *error)
{
    uint64_t before, after;
    const char *current;
    if (!live(scope, error) || !qa_q3_configstring_revision(scope->game, index, &before, error) ||
        !qa_q3_configstring_write(scope->game, index, text, error) || !live(scope, error) ||
        !qa_q3_configstring_revision(scope->game, index, &after, error) ||
        !qa_q3_configstring_read(scope->game, index, &current, error)) return false;
    return (after >= before && after - before <= 1 && !strcmp(current, text)) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Q3 match configstring was superseded");
}

/* bg_lib AddInt retains its signed INT32_MIN remainders. */
static void integer_text(int32_t value, char text[12])
{
    unsigned char reversed[11];
    size_t count = 0;
    int32_t remaining = value < 0 ? signed_bits(0u - (uint32_t)value) : value;
    do {
        reversed[count++] = (unsigned char)(48 + remaining % 10);
        remaining /= 10;
    } while (remaining);
    if (value < 0) reversed[count++] = '-';
    for (size_t i = 0; i < count; ++i) text[i] = (char)reversed[count - i - 1];
    text[count] = 0;
}

static bool config_integer(const match_scope *scope, uint32_t index, int32_t value, qa_error *error)
{
    char text[12];
    integer_text(value, text);
    return config(scope, index, text, error);
}

static bool send(const match_scope *scope, const char *text, qa_error *error)
{
    return live(scope, error) &&
        application_native_q3_send_command(scope->provider, -1, text, error) && live(scope, error);
}

static bool append(const match_scope *scope, const char *text, qa_error *error)
{
    qa_console *console;
    qa_command_context context = {.owner = scope->owner, .dialect = QA_CONSOLE_Q3,
                                  .origin = QA_COMMAND_SERVER};
    if (!live(scope, error) ||
        !application_native_q3_console_at(scope->provider, &console, NULL, NULL)) return false;
    return application_unified_q3_console(scope->provider, false, text, error) &&
        qa_console_append(console, &context, text, error) && live(scope, error);
}

static bool log_text(const match_scope *scope, const char *text, qa_error *error)
{
    return live(scope, error) && application_native_q3_log(scope->provider, text, error) &&
        live(scope, error);
}

static bool single_player(const match_scope *scope, bool *out, qa_error *error)
{
    *out = false;
    if (scope->product != QA_Q3_TEAM_ARENA) return true;
    int32_t active;
    if (!application_native_q3_settings_integer(scope->provider,
        "ui_singlePlayerActive", &active, error)) return false;
    *out = active != 0;
    return true;
}

static bool score(const match_scope *scope, uint32_t slot, int32_t *out, qa_error *error)
{
    qa_q3_native_client value;
    qa_q3_source_binding binding;
    return client(scope, slot, &value, &binding, error) &&
        qa_modes_score(scope->modes, scope->mode, binding.actor, out, error) &&
        client_live(scope, slot, binding.actor, error);
}

static bool phase(const match_scope *scope, qa_mode_phase value, uint64_t deadline, qa_error *error)
{
    return live(scope, error) && qa_modes_q3_source_phase(scope->modes, scope->mode,
        value, scope->time_ns, deadline, error) && live(scope, error);
}

bool application_native_q3_match_init(application_provider *provider, qa_error *error)
{
    if (!source_live(provider, error)) return false;
    const application_native_q3_cvar_snapshot *warmup;
    qa_q3_source_match_state match;
    if (!application_native_q3_settings_snapshot(provider, "g_warmup", &warmup, error) ||
        !qa_q3_source_match_state_read(provider->state.q3, &match, error)) return false;
    match.warmup_modification_count = warmup->modification_count;
    return qa_q3_source_match_state_write(provider->state.q3, &match, error);
}

static bool log_exit(match_scope *scope, const char *reason, qa_error *error)
{
    qa_q3_source_match_state match;
    qa_q3_source_team_state team;
    qa_q3_source_client_counts counts;
    qa_mode_view mode;
    qa_mode_q3_settings settings;
    bool single;
    if (!source_state(scope, &match, &team, &counts, error) ||
        !rules(scope, &mode, &settings, error) || !single_player(scope, &single, error)) return false;
    char text[1024];
    snprintf(text, sizeof(text), "Exit: %s\n", reason);
    if (!log_text(scope, text, error)) return false;
    match.intermission_queued_ms = scope->time;
    if (!qa_q3_source_match_state_write(scope->game, &match, error) ||
        !config(scope, 22, "1", error)) return false;
    if (mode.rules.kind >= QA_MODE_TEAM_DEATHMATCH) {
        char red[12], blue[12];
        integer_text(mode.team_scores[0], red);
        integer_text(mode.team_scores[1], blue);
        snprintf(text, sizeof(text), "red:%s  blue:%s\n", red, blue);
        if (!log_text(scope, text, error)) return false;
    }
    bool won = true;
    int32_t count = counts.num_connected < 32 ? counts.num_connected : 32;
    for (int32_t ordinal = 0; ordinal < count; ++ordinal) {
        uint32_t slot = counts.sorted_clients[ordinal];
        qa_q3_native_client value;
        qa_q3_source_binding binding;
        if (!client(scope, slot, &value, &binding, error)) return false;
        if (value.session.team == 3 || value.connected == QA_Q3_CLIENT_CONNECTING) continue;
        int32_t points;
        if (!score(scope, slot, &points, error)) return false;
        char score_text[12], ping[12], number[12];
        integer_text(points, score_text);
        integer_text(value.ping < 999 ? value.ping : 999, ping);
        integer_text((int32_t)slot, number);
        snprintf(text, sizeof(text), "score: %s  ping: %s  client: %s %s\n",
                 score_text, ping, number, value.netname);
        if (!log_text(scope, text, error) || !client_live(scope, slot, binding.actor, error)) return false;
        if (single && mode.rules.kind == QA_MODE_DUEL && (binding.server_flags & 8u)) {
            qa_q3_player_state player;
            if (!qa_q3_player_read(scope->game, binding.actor, &player))
                return application_fail(error, QA_ERROR_NOT_FOUND,
                                        "Q3 tournament result lost its actual player rank");
            if (player.rank == 0) won = false;
        }
    }
    if (single) {
        if (mode.rules.kind >= QA_MODE_CTF) won = mode.team_scores[0] > mode.team_scores[1];
        if (!append(scope, won ? "spWin\n" : "spLose\n", error)) return false;
    }
    return phase(scope, QA_MODE_EXIT_PENDING,
        scope->time_ns + (single ? UINT64_C(5000000000) : UINT64_C(1000000000)), error);
}

bool application_native_q3_match_log_exit(application_provider *provider, qa_mode_id mode,
    qa_string_id reason, qa_error *error)
{
    match_scope scope;
    if (!begin(provider, &scope, error)) return false;
    bool result = scope.selected_q3 && scope.mode.slot == mode.slot &&
        scope.mode.generation == mode.generation;
    if (!result) application_fail(error, QA_ERROR_ARGUMENT,
                                 "Q3 exit effect requires the actual selected Q3 rule mode");
    const char *text = reason ? qa_strings_cstr(
        qa_session_strings(scope.application->session), reason) : "";
    if (result && !text) result = application_fail(error, QA_ERROR_ARGUMENT,
                                                 "Q3 exit reason has no owned source text");
    qa_q3_source_match_state match;
    if (result) result = qa_q3_source_match_state_read(scope.game, &match, error);
    if (result && !match.intermission_queued_ms && !match.intermission_time_ms)
        result = log_exit(&scope, text, error);
    return finish(&scope, result, error);
}

static bool wait_for_players(match_scope *scope, qa_error *error)
{
    qa_q3_source_team_state team;
    if (!qa_q3_source_team_state_read(scope->game, &team, error)) return false;
    if (team.warmup_time_ms == -1) return true;
    team.warmup_time_ms = -1;
    return qa_q3_source_team_state_write(scope->game, &team, error) &&
        config(scope, 5, "-1", error) && log_text(scope, "Warmup:\n", error) &&
        phase(scope, QA_MODE_WAITING, 0, error);
}

static bool add_tournament_player(match_scope *scope, qa_error *error)
{
    qa_q3_source_match_state match;
    qa_q3_source_team_state team;
    qa_q3_source_client_counts counts;
    if (!source_state(scope, &match, &team, &counts, error)) return false;
    if (counts.num_playing >= 2 || match.intermission_time_ms) return true;
    uint32_t selected = UINT32_MAX;
    int32_t oldest = 0;
    qa_actor_id actor = {0};
    for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
        qa_q3_native_client value;
        qa_q3_source_binding binding;
        if (!client(scope, slot, &value, &binding, error)) return false;
        if (value.connected != QA_Q3_CLIENT_CONNECTED || value.session.team != 3 ||
            value.session.spectator_state == QA_Q3_SPECTATOR_SCOREBOARD ||
            value.session.spectator_client < 0) continue;
        if (selected == UINT32_MAX || value.session.spectator_time_ms < oldest) {
            selected = slot;
            oldest = value.session.spectator_time_ms;
            actor = binding.actor;
        }
    }
    if (selected == UINT32_MAX) return true;
    team.warmup_time_ms = -1;
    return qa_q3_source_team_state_write(scope->game, &team, error) &&
        application_native_q3_client_set_team(scope->provider, actor, "f", error) &&
        client_live(scope, selected, actor, error);
}

static bool tournament(match_scope *scope, qa_error *error)
{
    qa_q3_source_match_state match;
    qa_q3_source_team_state team;
    qa_q3_source_client_counts counts;
    qa_mode_view mode;
    qa_mode_q3_settings settings;
    if (!source_state(scope, &match, &team, &counts, error) ||
        !rules(scope, &mode, &settings, error)) return false;
    if (!mode.rules.enabled || mode.rules.paused || !counts.num_playing) return true;
    if (mode.rules.kind == QA_MODE_DUEL) {
        if (counts.num_playing < 2 && !add_tournament_player(scope, error)) return false;
        if (!source_state(scope, &match, &team, &counts, error)) return false;
        if (counts.num_playing != 2) return wait_for_players(scope, error);
        if (!team.warmup_time_ms) return true;
    } else {
        if (mode.rules.kind == QA_MODE_SINGLE_PLAYER || !team.warmup_time_ms) return true;
        bool enough = counts.num_playing >= 2;
        if (mode.rules.kind > QA_MODE_TEAM_DEATHMATCH) {
            bool red = false, blue = false;
            for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
                qa_q3_native_client value;
                qa_q3_source_binding binding;
                if (!client(scope, slot, &value, &binding, error)) return false;
                if (value.connected == QA_Q3_CLIENT_DISCONNECTED) continue;
                red |= value.session.team == 1;
                blue |= value.session.team == 2;
            }
            enough = red && blue;
        }
        if (!enough) return wait_for_players(scope, error);
    }
    if (!rules(scope, &mode, &settings, error)) return false;
    if (settings.warmup_modification_count != match.warmup_modification_count) {
        match.warmup_modification_count = settings.warmup_modification_count;
        team.warmup_time_ms = -1;
        if (!qa_q3_source_match_state_write(scope->game, &match, error)) return false;
    }
    if (team.warmup_time_ms < 0) {
        uint32_t delay = ((uint32_t)settings.warmup_seconds - 1u) * 1000u;
        team.warmup_time_ms = signed_bits((uint32_t)scope->time + delay);
        return qa_q3_source_team_state_write(scope->game, &team, error) &&
            config_integer(scope, 5, team.warmup_time_ms, error) &&
            phase(scope, QA_MODE_COUNTDOWN,
                  (uint64_t)(uint32_t)team.warmup_time_ms * UINT64_C(1000000), error);
    }
    if (scope->time <= team.warmup_time_ms) return true;
    team.warmup_time_ms = signed_bits((uint32_t)team.warmup_time_ms + 10000u);
    if (!qa_q3_source_team_state_write(scope->game, &team, error) ||
        !application_native_q3_settings_force_set(scope->provider, "g_restarted", "1", error) ||
        !live(scope, error) || !append(scope, "map_restart 0\n", error)) return false;
    if (!qa_q3_source_match_state_read(scope->game, &match, error)) return false;
    match.restarted = true;
    return qa_q3_source_match_state_write(scope->game, &match, error);
}

static bool tournament_scores(match_scope *scope, qa_error *error)
{
    for (uint32_t ordinal = 0; ordinal < 2; ++ordinal) {
        qa_q3_source_client_counts counts;
        if (!qa_q3_source_client_counts_read(scope->game, &counts, error)) return false;
        uint32_t slot = counts.sorted_clients[ordinal];
        qa_q3_native_client value;
        qa_q3_source_binding binding;
        if (!client(scope, slot, &value, &binding, error)) return false;
        if (value.connected != QA_Q3_CLIENT_CONNECTED) continue;
        uint32_t field;
        if (!ordinal) {
            value.session.wins = signed_bits((uint32_t)value.session.wins + 1u);
            field = QA_Q3_CLIENT_SESSION_WINS;
        } else {
            value.session.losses = signed_bits((uint32_t)value.session.losses + 1u);
            field = QA_Q3_CLIENT_SESSION_LOSSES;
        }
        if (!qa_q3_client_session_slot_write(scope->game, slot, field, &value.session, error) ||
            !application_native_q3_client_userinfo_changed_slot(scope->provider, slot, error) ||
            !client_live(scope, slot, binding.actor, error)) return false;
    }
    return true;
}

static bool scoreboard(match_scope *scope, qa_error *error)
{
    for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
        qa_q3_native_client value;
        qa_q3_source_binding binding;
        if (!client(scope, slot, &value, &binding, error)) return false;
        if (value.connected != QA_Q3_CLIENT_CONNECTED) continue;
        if (!application_native_q3_client_scoreboard(scope->provider, binding.actor, error) ||
            !client_live(scope, slot, binding.actor, error)) return false;
    }
    return true;
}

static bool move_to_intermission(match_scope *scope, uint32_t slot, qa_error *error)
{
    qa_q3_native_client value;
    qa_q3_source_binding binding;
    if (!client(scope, slot, &value, &binding, error)) return false;
    qa_actor_id actor = binding.actor;
    if (value.session.spectator_state == QA_Q3_SPECTATOR_FOLLOW &&
        (!application_native_q3_client_stop_following_slot(scope->provider, slot, error) ||
         !client_live(scope, slot, actor, error))) return false;
    qa_q3_source_match_state match;
    if (!qa_q3_source_match_state_read(scope->game, &match, error)) return false;
    return qa_q3_client_move_intermission(scope->game, actor,
        match.intermission_origin, match.intermission_angles, error) &&
        client_live(scope, slot, actor, error);
}

static bool begin_intermission(match_scope *scope, qa_error *error)
{
    qa_q3_source_match_state match;
    if (!qa_q3_source_match_state_read(scope->game, &match, error)) return false;
    if (match.intermission_time_ms) return true;
    qa_mode_view mode;
    if (!qa_modes_read(scope->modes, scope->mode, &mode, error)) return false;
    if (scope->selected_q3 && mode.rules.kind == QA_MODE_DUEL &&
        !tournament_scores(scope, error)) return false;
    if (!qa_q3_source_match_state_read(scope->game, &match, error)) return false;
    match.intermission_time_ms = scope->time;
    if (!qa_q3_source_match_state_write(scope->game, &match, error)) return false;
    qa_vec3 origin, angles;
    if (!application_q3_find_intermission_pose(scope->provider, &origin, &angles, error) ||
        !live(scope, error) || !qa_q3_source_match_state_read(scope->game, &match, error)) return false;
    match.intermission_origin = origin;
    match.intermission_angles = angles;
    if (!qa_q3_source_match_state_write(scope->game, &match, error) ||
        !application_native_q3_match_begin_product(scope->provider, error) || !live(scope, error))
        return false;
    for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
        qa_q3_native_client value;
        qa_q3_source_binding binding;
        if (!client(scope, slot, &value, &binding, error)) return false;
        if (!binding.in_use) continue;
        qa_combat_state combat;
        if (!qa_combat_read(scope->application->combat, binding.actor, &combat, error) ||
            !client_live(scope, slot, binding.actor, error)) return false;
        if (combat.health <= 0 &&
            (!application_native_q3_client_respawn(scope->provider, binding.actor, error) ||
             !client_live(scope, slot, binding.actor, error))) return false;
        if (!move_to_intermission(scope, slot, error)) return false;
    }
    return scoreboard(scope, error) && (!scope->selected_q3 ||
        phase(scope, QA_MODE_INTERMISSION, scope->time_ns + UINT64_C(5000000000), error));
}

bool application_native_q3_match_begin_intermission(application_provider *provider,
    qa_error *error)
{
    match_scope scope;
    if (!begin(provider, &scope, error)) return false;
    return finish(&scope, begin_intermission(&scope, error), error);
}

static bool remove_tournament_loser(match_scope *scope, qa_error *error)
{
    qa_q3_source_client_counts counts;
    if (!qa_q3_source_client_counts_read(scope->game, &counts, error)) return false;
    if (counts.num_playing != 2) return true;
    uint32_t slot = counts.sorted_clients[1];
    qa_q3_native_client value;
    qa_q3_source_binding binding;
    if (!client(scope, slot, &value, &binding, error)) return false;
    if (value.connected != QA_Q3_CLIENT_CONNECTED) return true;
    return application_native_q3_client_set_team(scope->provider, binding.actor, "s", error) &&
        client_live(scope, slot, binding.actor, error);
}

static bool exit_level(match_scope *scope, qa_error *error)
{
    if (!application_native_q3_match_bots_end(scope->provider, error) || !live(scope, error)) return false;
    qa_mode_view mode;
    qa_mode_q3_settings settings;
    qa_q3_source_match_state match;
    if (!rules(scope, &mode, &settings, error) ||
        !qa_q3_source_match_state_read(scope->game, &match, error)) return false;
    if (mode.rules.kind == QA_MODE_DUEL) {
        if (match.restarted) return true;
        if (!remove_tournament_loser(scope, error) || !append(scope, "map_restart 0\n", error) ||
            !qa_q3_source_match_state_read(scope->game, &match, error)) return false;
        match.restarted = true;
        match.changemap = QA_STRING_NONE;
        match.intermission_time_ms = 0;
        return qa_q3_source_match_state_write(scope->game, &match, error) &&
            phase(scope, QA_MODE_FINISHED, 0, error);
    }
    if (!append(scope, "vstr nextmap\n", error) ||
        !qa_q3_source_match_state_read(scope->game, &match, error)) return false;
    match.changemap = QA_STRING_NONE;
    match.intermission_time_ms = 0;
    if (!qa_q3_source_match_state_write(scope->game, &match, error)) return false;
    qa_q3_source_team_state team;
    if (!qa_q3_source_team_state_read(scope->game, &team, error)) return false;
    team.team_scores[1] = team.team_scores[2] = 0;
    if (!qa_q3_source_team_state_write(scope->game, &team, error) ||
        !qa_modes_q3_source_reset_teams(scope->modes, scope->mode, error)) return false;
    for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
        qa_q3_native_client value;
        qa_q3_source_binding binding;
        if (!client(scope, slot, &value, &binding, error)) return false;
        if (value.connected != QA_Q3_CLIENT_CONNECTED) continue;
        if (!qa_modes_set_score(scope->modes, scope->mode, binding.actor, 0, error) ||
            !client_live(scope, slot, binding.actor, error) ||
            !qa_q3_client_score_reset(scope->game, slot, error)) return false;
    }
    if (!application_native_q3_session_write_world(scope->provider, error) || !live(scope, error))
        return false;
    for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
        qa_q3_native_client value;
        qa_q3_source_binding binding;
        if (!client(scope, slot, &value, &binding, error)) return false;
        if (value.connected == QA_Q3_CLIENT_CONNECTED &&
            !qa_q3_client_connecting(scope->game, slot, error)) return false;
    }
    return phase(scope, QA_MODE_FINISHED, 0, error);
}

bool application_native_q3_match_exit_level(application_provider *provider, qa_error *error)
{
    match_scope scope;
    if (!begin(provider, &scope, error)) return false;
    bool result;
    if (!scope.selected_q3)
        result = application_fail(error, QA_ERROR_ARGUMENT,
            "native Q3 level exit requires its actual selected native Q3 rule owner");
    else result = exit_level(&scope, error);
    return finish(&scope, result, error);
}

static bool intermission_exit(match_scope *scope, qa_error *error)
{
    qa_mode_view mode;
    qa_mode_q3_settings settings;
    if (!rules(scope, &mode, &settings, error)) return false;
    if (mode.rules.kind == QA_MODE_SINGLE_PLAYER) return true;
    uint32_t ready = 0, not_ready = 0;
    int32_t mask = 0;
    for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
        qa_q3_native_client value;
        qa_q3_source_binding binding;
        if (!client(scope, slot, &value, &binding, error)) return false;
        if (value.connected != QA_Q3_CLIENT_CONNECTED) continue;
        qa_q3_player_state player;
        if (!qa_q3_player_read(scope->game, binding.actor, &player) || player.client_number < 0 ||
            !qa_q3_source_binding_read(scope->game, (uint32_t)player.client_number, &binding, error))
            return application_fail(error, QA_ERROR_NOT_FOUND,
                                    "Q3 ready state lost its source PS clientNum");
        if (binding.server_flags & 8u) continue;
        if (value.ready_to_exit) {
            ++ready;
            if (slot < 16) mask |= (int32_t)(1u << slot);
        } else ++not_ready;
    }
    for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
        qa_q3_native_client value;
        qa_q3_source_binding binding;
        if (!client(scope, slot, &value, &binding, error)) return false;
        if (value.connected == QA_Q3_CLIENT_CONNECTED &&
            !qa_q3_wire_client_ready(scope->game, slot, mask, error)) return false;
    }
    qa_q3_source_match_state match;
    if (!qa_q3_source_match_state_read(scope->game, &match, error)) return false;
    if (scope->time < signed_bits((uint32_t)match.intermission_time_ms + 5000u)) return true;
    if (!ready) {
        match.ready_to_exit = false;
        return qa_q3_source_match_state_write(scope->game, &match, error);
    }
    if (!not_ready) return exit_level(scope, error);
    if (!match.ready_to_exit) {
        match.ready_to_exit = true;
        match.exit_time_ms = scope->time;
        if (!qa_q3_source_match_state_write(scope->game, &match, error)) return false;
    }
    return scope->time < signed_bits((uint32_t)match.exit_time_ms + 10000u) ||
        exit_level(scope, error);
}

static bool check_exit(match_scope *scope, qa_error *error)
{
    /* Foreign selected rules retain their own real match-control pass. This
     * source tail never substitutes native Q3 limits or readiness policy. */
    if (!scope->selected_q3) return true;
    qa_q3_source_match_state match;
    qa_q3_source_team_state team;
    qa_q3_source_client_counts counts;
    qa_mode_view mode;
    qa_mode_q3_settings settings;
    if (!source_state(scope, &match, &team, &counts, error) ||
        !rules(scope, &mode, &settings, error)) return false;
    if (!mode.rules.enabled || mode.rules.paused) return true;
    if (match.intermission_time_ms) return intermission_exit(scope, error);
    if (match.intermission_queued_ms) {
        bool single;
        if (!single_player(scope, &single, error)) return false;
        int32_t delay = single ? 5000 : 1000;
        if (signed_bits((uint32_t)scope->time - (uint32_t)match.intermission_queued_ms) >= delay) {
            match.intermission_queued_ms = 0;
            if (!qa_q3_source_match_state_write(scope->game, &match, error)) return false;
            return begin_intermission(scope, error);
        }
        return true;
    }
    if (counts.num_playing >= 2) {
        if (mode.rules.kind >= QA_MODE_TEAM_DEATHMATCH) {
            if (mode.team_scores[0] == mode.team_scores[1]) return true;
        } else {
            int32_t first, second;
            if (!score(scope, counts.sorted_clients[0], &first, error) ||
                !score(scope, counts.sorted_clients[1], &second, error)) return false;
            if (first == second) return true;
        }
    }
    if (settings.time_limit_minutes && !team.warmup_time_ms &&
        signed_bits((uint32_t)scope->time - (uint32_t)scope->start_time) >=
        signed_bits((uint32_t)settings.time_limit_minutes * 60000u))
        return send(scope, "print \"Timelimit hit.\n\"", error) &&
            log_exit(scope, "Timelimit hit.", error);
    if (counts.num_playing < 2) return true;
    static const char *frag_text[] = {"print \"Red hit the fraglimit.\n\"",
                                     "print \"Blue hit the fraglimit.\n\""};
    static const char *capture_text[] = {"print \"Red hit the capturelimit.\n\"",
                                        "print \"Blue hit the capturelimit.\n\""};
    if (mode.rules.kind < QA_MODE_CTF && settings.frag_limit) {
        for (uint32_t i = 0; i < 2; ++i)
            if (mode.team_scores[i] >= settings.frag_limit)
                return send(scope, frag_text[i], error) && log_exit(scope, "Fraglimit hit.", error);
        for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
            qa_q3_native_client value;
            qa_q3_source_binding binding;
            if (!client(scope, slot, &value, &binding, error)) return false;
            if (value.connected != QA_Q3_CLIENT_CONNECTED || value.session.team != 0) continue;
            int32_t points;
            if (!score(scope, slot, &points, error)) return false;
            if (points >= settings.frag_limit) {
                if (!log_exit(scope, "Fraglimit hit.", error)) return false;
                char command[1024];
                snprintf(command, sizeof(command), "print \"%s^7 hit the fraglimit.\n\"", value.netname);
                return send(scope, command, error);
            }
        }
    }
    if (mode.rules.kind >= QA_MODE_CTF && settings.capture_limit)
        for (uint32_t i = 0; i < 2; ++i)
            if (mode.team_scores[i] >= settings.capture_limit)
                return send(scope, capture_text[i], error) && log_exit(scope, "Capturelimit hit.", error);
    return true;
}

bool application_native_q3_match_check_exit(application_provider *provider, qa_error *error)
{
    match_scope scope;
    if (!begin(provider, &scope, error)) return false;
    return finish(&scope, check_exit(&scope, error), error);
}

bool application_native_q3_match_frame(application_provider *provider,
    const qa_source_frame *frame, qa_error *error)
{
    match_scope scope;
    if (!begin(provider, &scope, error)) return false;
    qa_source_frame active;
    bool current = frame && frame->provider == scope.owner && frame->kind == QA_CLOCK_Q3 &&
        frame->phase == QA_CLIENT_END_FRAME &&
        scope.application->operation == APPLICATION_ADVANCING &&
        qa_session_active_frame(scope.application->session, scope.owner, &active) &&
        active.phase == frame->phase && active.number == frame->number &&
        active.start_ns == frame->start_ns && active.time_ns == frame->time_ns &&
        active.elapsed_ns == frame->elapsed_ns && scope.time_ns == frame->time_ns;
    if (!current) application_fail(error, QA_ERROR_ARGUMENT,
                                  "native Q3 match controls require their genuine END frame");
    bool result = current && (!scope.selected_q3 ||
        (tournament(&scope, error) && check_exit(&scope, error)));
    return finish(&scope, result, error);
}
