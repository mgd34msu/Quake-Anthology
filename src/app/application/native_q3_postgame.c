/* Tournament results from id Software's code/game/g_arenas.c and its TypeScript port.
 * Copyright (C) 1999-2005 Id Software, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "native_q3_postgame.h"
#include "native_q3_console.h"
#include "native_q3_rank.h"
#include "native_q3_settings.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"
#include "../../gameplay/q3/source_postgame.h"

#include <string.h>

typedef struct postgame_scope {
    application_provider *provider;
    qa_application *application;
    qa_q3_game *game;
    qa_modes *modes;
    qa_mode_id mode;
    qa_actor_owner owner;
    uint64_t publication_generation, command_generation, map_revision;
    uint32_t maximum;
    int32_t time, game_type;
    qa_q3_product product;
} postgame_scope;

static bool live(const postgame_scope *scope, qa_error *error)
{
    application_provider *provider = scope->provider;
    qa_application *app = scope->application;
    if (provider->application != app || provider->kind != APPLICATION_PROVIDER_Q3 ||
        provider->state.q3 != scope->game || provider->owner != scope->owner ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        app->destroy_requested || app->modes != scope->modes || !app->primary_mode_ready ||
        app->primary_mode.slot != scope->mode.slot ||
        app->primary_mode.generation != scope->mode.generation ||
        app->publication_generation != scope->publication_generation ||
        app->command_generation != scope->command_generation ||
        app->map_revision != scope->map_revision ||
        application_world_provider(app, QA_ROLE_ENTITIES, "") != provider)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "native Q3 postgame source changed during its callback");
    int32_t time;
    return qa_q3_source_clock(scope->game, &time, error) &&
        (time == scope->time || application_fail(error, QA_ERROR_ARGUMENT,
            "native Q3 postgame callback advanced the actual source clock"));
}

static bool begin(application_provider *provider, postgame_scope *scope, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    if (!app || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !app->modes || !app->primary_mode_ready)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "postgame requires its native GAME and actual selected score owner");
    *scope = (postgame_scope){.provider = provider, .application = app,
        .game = provider->state.q3, .modes = app->modes, .mode = app->primary_mode,
        .owner = provider->owner, .publication_generation = app->publication_generation,
        .command_generation = app->command_generation, .map_revision = app->map_revision};
    int32_t start;
    if (!qa_q3_source_clock(scope->game, &scope->time, error) || !live(scope, error) ||
        !qa_q3_source_max_clients(scope->game, &scope->maximum, error) ||
        !qa_q3_source_match_context_read(scope->game, &scope->product, &start, error) ||
        !application_native_q3_settings_integer(provider, "g_gametype", &scope->game_type, error))
        return false;
    return application_native_q3_console_borrow(provider, error);
}

static bool finish(postgame_scope *scope, bool okay, qa_error *error)
{
    if (okay) okay = live(scope, error);
    application_native_q3_console_release(scope->provider);
    return okay;
}

static int32_t signed_bits(uint32_t bits)
{
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* bg_lib AddInt preserves the source's wrapped INT32_MIN byte sequence. */
static void decimal(int32_t value, char text[32])
{
    unsigned char reversed[16];
    size_t count = 0;
    int32_t remaining = value < 0 ? signed_bits(0u - (uint32_t)value) : value;
    do {
        reversed[count++] = (unsigned char)(48 + remaining % 10);
        remaining /= 10;
    } while (remaining);
    if (value < 0) reversed[count++] = '-';
    for (size_t index = 0; index < count; ++index) text[index] = (char)reversed[count - index - 1];
    text[count] = 0;
}

static void integer_fields(char *out, size_t capacity, const char *prefix,
    const int32_t *values, size_t count)
{
    size_t length = strlen(prefix);
    if (length >= capacity) length = capacity - 1;
    memcpy(out, prefix, length);
    for (size_t index = 0; index < count && length < capacity - 1; ++index) {
        out[length++] = ' ';
        char text[32];
        decimal(values[index], text);
        size_t bytes = strlen(text);
        if (bytes > capacity - 1 - length) bytes = capacity - 1 - length;
        memcpy(out + length, text, bytes);
        length += bytes;
    }
    out[length] = 0;
}

static bool source_player(const postgame_scope *scope, uint32_t slot,
    qa_q3_native_client *client, qa_q3_player_state *state, int32_t *score, qa_error *error)
{
    return live(scope, error) && qa_q3_client_slot_read(scope->game, slot, client, error) &&
        qa_q3_source_postgame_client_state(scope->game, slot, state, error) &&
        qa_q3_wire_client_source_score_read(scope->game, slot, score, error) && live(scope, error);
}

static bool tournament_info(postgame_scope *scope, qa_error *error)
{
    uint32_t player;
    for (player = 0; player < scope->maximum; ++player) {
        qa_q3_source_binding row;
        if (!qa_q3_source_binding_read(scope->game, player, &row, error)) return false;
        if (row.in_use && !(row.server_flags & 8u)) break;
    }
    if (player == scope->maximum) return true;
    if (!application_native_q3_rank(scope->provider, error) || !live(scope, error)) return false;
    qa_q3_source_client_counts counts;
    qa_q3_native_client client;
    qa_q3_player_state state;
    int32_t score;
    if (!qa_q3_source_client_counts_read(scope->game, &counts, error) ||
        !source_player(scope, player, &client, &state, &score, error)) return false;
    int32_t values[14] = {counts.num_non_spectator, (int32_t)player};
    size_t field_count;
    if (client.session.team == 3) field_count = scope->product == QA_Q3_TEAM_ARENA ? 13 : 8;
    else {
        int32_t numerator = signed_bits((uint32_t)state.accuracy_hits * 100u);
        int32_t accuracy = state.accuracy_shots
            ? signed_bits((uint32_t)((int64_t)numerator / state.accuracy_shots)) : 0;
        values[2] = accuracy;
        values[3] = state.impressive_count;
        values[4] = state.excellent_count;
        if (scope->product == QA_Q3_TEAM_ARENA) {
            bool won = false;
            int32_t score1, score2;
            if (scope->game_type >= 4) {
                qa_q3_source_team_state team;
                if (!qa_q3_source_team_state_read(scope->game, &team, error) || !live(scope, error)) return false;
                score1 = team.team_scores[1]; score2 = team.team_scores[2];
                won = client.session.team == 1 ? score1 > score2 : score2 > score1;
            } else {
                int32_t first, second;
                if (!qa_q3_wire_client_source_score_read(scope->game, counts.sorted_clients[0], &first, error) ||
                    !live(scope, error) ||
                    !qa_q3_wire_client_source_score_read(scope->game, counts.sorted_clients[1], &second, error) ||
                    !live(scope, error)) return false;
                won = player == counts.sorted_clients[0];
                score1 = won ? first : second; score2 = won ? second : first;
            }
            values[5] = state.defend_count; values[6] = state.assist_count;
            values[7] = state.gauntlet_frag_count; values[8] = score;
            values[9] = won && !state.deaths ? 1 : 0;
            values[10] = score1; values[11] = score2;
            values[12] = scope->time; values[13] = state.captures;
            field_count = 14;
        } else {
            values[5] = state.gauntlet_frag_count; values[6] = score;
            values[7] = !state.rank && !state.deaths ? 1 : 0;
            field_count = 8;
        }
    }
    char message[1024];
    integer_fields(message, sizeof(message), "postgame", values, field_count);
    const size_t message_length = strlen(message);
    size_t length = message_length;
    for (int32_t index = 0; index < counts.num_non_spectator; ++index) {
        uint32_t number = counts.sorted_clients[index];
        qa_q3_native_client ranked;
        qa_q3_player_state ps;
        int32_t row_score;
        if (!source_player(scope, number, &ranked, &ps, &row_score, error)) return false;
        int32_t row_values[3] = {(int32_t)number, ps.rank, row_score};
        char row[32];
        integer_fields(row, sizeof(row), "", row_values, 3);
        size_t bytes = strlen(row);
        if (message_length + bytes + 1 >= sizeof(message)) break;
        if (length + bytes >= sizeof(message))
            return application_fail(error, QA_ERROR_ARGUMENT,
                                    "postgame strcat exceeds the source 1024-byte message buffer");
        memcpy(message + length, row, bytes + 1);
        length += bytes;
    }
    qa_console *console;
    if (!application_native_q3_console_at(scope->provider, &console, NULL, NULL))
        return application_fail(error, QA_ERROR_NOT_FOUND, "postgame has no real source console");
    qa_command_context command = {.owner = scope->owner, .dialect = QA_CONSOLE_Q3,
                                  .origin = QA_COMMAND_SERVER};
    return qa_console_append(console, &command, message, error) && live(scope, error);
}

bool application_native_q3_match_begin_product(application_provider *provider, qa_error *error)
{
    postgame_scope scope;
    if (!begin(provider, &scope, error)) return false;
    bool okay = true;
    if (scope.product == QA_Q3_TEAM_ARENA) {
        int32_t active;
        okay = application_native_q3_settings_integer(provider, "ui_singlePlayerActive", &active, error);
        if (okay && active) okay = application_native_q3_settings_force_set(provider,
            "ui_singlePlayerActive", "0", error) && live(&scope, error) && tournament_info(&scope, error);
    } else if (scope.game_type == 2) {
        okay = tournament_info(&scope, error) && live(&scope, error) &&
            qa_q3_source_spawn_victory_pads(scope.game, error);
    }
    return finish(&scope, okay, error);
}

bool application_native_q3_postgame_console(application_provider *provider,
    const qa_command_invocation *command, bool *handled, qa_error *error)
{
    if (!command || !handled) return application_fail(error, QA_ERROR_ARGUMENT,
                                                     "podium command requires a real invocation");
    *handled = false;
    const char *name = command->argc ? command->argv[0] : "";
    const char *expected = "abort_podium";
    for (; *name && *expected; ++name, ++expected) {
        unsigned char byte = (unsigned char)*name;
        if (byte >= 'A' && byte <= 'Z') byte = (unsigned char)(byte + 'a' - 'A');
        if (byte != (unsigned char)*expected) return true;
    }
    if (*name || *expected) return true;
    *handled = true;
    postgame_scope scope;
    if (!begin(provider, &scope, error)) return false;
    return finish(&scope, scope.game_type != 2 || qa_q3_source_abort_podium(scope.game, error), error);
}

bool application_native_q3_postgame_cvar_integer(void *opaque, const char *name,
    int32_t *out, qa_error *error)
{
    application_provider *provider = opaque;
    if (!out || !name || (strcmp(name, "g_podiumDist") && strcmp(name, "g_podiumDrop")))
        return application_fail(error, QA_ERROR_ARGUMENT, "podium trap requires its registered source cvar");
    postgame_scope scope;
    if (!begin(provider, &scope, error)) return false;
    qa_cvars *registry = application_native_q3_cvar_owner(provider, name);
    const qa_cvar_view *value = registry ? qa_cvars_find(registry, name) : NULL;
    bool okay = value != NULL;
    if (okay) *out = value->integer;
    else application_fail(error, QA_ERROR_NOT_FOUND, "podium trap has no actual registered engine cvar");
    return finish(&scope, okay, error);
}
