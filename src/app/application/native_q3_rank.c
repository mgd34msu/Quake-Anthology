/* CalculateRanks and SortRanks from id Software's code/game/g_main.c.
 * Copyright (C) 1999-2005 Id Software, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "native_q3_rank.h"
#include "native_q3_clients.h"
#include "native_q3_console.h"
#include "native_q3_match.h"
#include "native_q3_settings.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_configstrings.h"
#include "qa/game_q3_source.h"

#include <string.h>

typedef struct rank_scope {
    application_provider *provider;
    qa_application *application;
    qa_q3_game *game;
    qa_modes *modes;
    qa_mode_id mode;
    qa_actor_owner owner;
    uint32_t maximum;
    int32_t game_type;
} rank_scope;

typedef struct rank_client {
    qa_actor_id actor;
    qa_q3_client_connection connected;
    qa_q3_client_session session;
    uint32_t server_flags;
    int32_t score;
} rank_client;

static bool live(const rank_scope *scope, qa_error *error)
{
    application_provider *provider = scope->provider;
    qa_application *application = scope->application;
    if (application->destroy_requested || provider->application != application ||
        provider->kind != APPLICATION_PROVIDER_Q3 || provider->state.q3 != scope->game ||
        provider->owner != scope->owner || !provider->constructed || !provider->attached ||
        provider->close_pending || application->modes != scope->modes)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "native Q3 rank source retired during its callback");
    return application_native_q3_source_mode_current(provider, scope->mode, error);
}

static bool begin(application_provider *provider, rank_scope *scope, qa_error *error)
{
    qa_application *application = provider ? provider->application : NULL;
    if (!application || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !application->modes)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "CalculateRanks requires its native GAME and selected score owner");
    *scope = (rank_scope){.provider = provider, .application = application,
        .game = provider->state.q3, .modes = application->modes,
        .owner = provider->owner};
    bool associated;
    if (!application_native_q3_source_mode(provider, &scope->mode, &associated, error)) return false;
    if (!associated)
        return application_fail(error, QA_ERROR_NOT_FOUND,
            "CalculateRanks has no actual provider-associated score owner");
    if (!live(scope, error) ||
        !qa_q3_source_max_clients(scope->game, &scope->maximum, error) ||
        !application_native_q3_settings_integer(provider, "g_gametype", &scope->game_type, error))
        return false;
    return application_native_q3_console_borrow(provider, error);
}

static bool finish(rank_scope *scope, bool okay, qa_error *error)
{
    if (okay) okay = live(scope, error);
    application_native_q3_console_release(scope->provider);
    return okay;
}

static bool client_read(const rank_scope *scope, uint32_t slot, rank_client *out,
    qa_error *error)
{
    qa_q3_source_binding binding;
    qa_q3_native_client client;
    if (!live(scope, error) ||
        !qa_q3_source_binding_read(scope->game, slot, &binding, error) ||
        !qa_q3_client_slot_read(scope->game, slot, &client, error) ||
        !qa_q3_client_server_flags(scope->game, slot, &out->server_flags, error)) return false;
    out->actor = binding.actor;
    out->connected = client.connected;
    out->session = client.session;
    return true;
}

static bool client_current(const rank_scope *scope, uint32_t slot,
    const rank_client *expected, qa_error *error)
{
    rank_client actual = {0};
    uint32_t bound;
    if (!client_read(scope, slot, &actual, error)) return false;
    if (!qa_actor_id_equal(actual.actor, expected->actor) ||
        actual.connected != expected->connected ||
        actual.session.team != expected->session.team ||
        actual.session.spectator_state != expected->session.spectator_state ||
        actual.session.spectator_client != expected->session.spectator_client ||
        actual.session.spectator_time_ms != expected->session.spectator_time_ms ||
        actual.server_flags != expected->server_flags ||
        (actual.connected != QA_Q3_CLIENT_DISCONNECTED &&
         (!qa_actors_get(qa_session_actors(scope->application->session), actual.actor) ||
          !qa_q3_native_client_slot(scope->game, actual.actor, &bound, error) || bound != slot)))
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "rank client changed its actual physical source during score read");
    return true;
}

static bool score_read(const rank_scope *scope, uint32_t slot,
    rank_client *client, qa_error *error)
{
    return client_current(scope, slot, client, error) &&
        qa_modes_score(scope->modes, scope->mode, client->actor, &client->score, error) &&
        client_current(scope, slot, client, error);
}

static int compare(const rank_client clients[QA_Q3_NATIVE_CLIENTS], uint32_t first,
    uint32_t second)
{
    const rank_client *a = &clients[first], *b = &clients[second];
    if (a->session.spectator_state == QA_Q3_SPECTATOR_SCOREBOARD ||
        a->session.spectator_client < 0) return 1;
    if (b->session.spectator_state == QA_Q3_SPECTATOR_SCOREBOARD ||
        b->session.spectator_client < 0) return -1;
    if (a->connected == QA_Q3_CLIENT_CONNECTING) return 1;
    if (b->connected == QA_Q3_CLIENT_CONNECTING) return -1;
    if (a->session.team == 3 && b->session.team == 3)
        return a->session.spectator_time_ms < b->session.spectator_time_ms ? -1 :
            a->session.spectator_time_ms > b->session.spectator_time_ms ? 1 : 0;
    if (a->session.team == 3) return 1;
    if (b->session.team == 3) return -1;
    return a->score > b->score ? -1 : a->score < b->score ? 1 : 0;
}

/* Rank sorting follows the actual game/bg_lib.c qsort partition sequence.
 * Copyright (c) 1992, 1993 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement: This product includes software
 *    developed by the University of California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
static void swap(uint32_t values[QA_Q3_NATIVE_CLIENTS], int first, int second)
{
    uint32_t value = values[first];
    values[first] = values[second];
    values[second] = value;
}

static int median(const uint32_t values[QA_Q3_NATIVE_CLIENTS], const rank_client *clients,
    int a, int b, int c)
{
    return compare(clients, values[a], values[b]) < 0 ?
        (compare(clients, values[b], values[c]) < 0 ? b :
         (compare(clients, values[a], values[c]) < 0 ? c : a)) :
        (compare(clients, values[b], values[c]) > 0 ? b :
         (compare(clients, values[a], values[c]) < 0 ? a : c));
}

static void insertion(uint32_t values[QA_Q3_NATIVE_CLIENTS], const rank_client *clients,
    int start, int length)
{
    for (int mid = start + 1; mid < start + length; ++mid)
        for (int left = mid; left > start &&
             compare(clients, values[left - 1], values[left]) > 0; --left)
            swap(values, left, left - 1);
}

static void swap_range(uint32_t values[QA_Q3_NATIVE_CLIENTS], int first, int second,
    int length)
{
    for (int index = 0; index < length; ++index) swap(values, first + index, second + index);
}

static void sort(uint32_t values[QA_Q3_NATIVE_CLIENTS], const rank_client *clients,
    int start, int length)
{
    for (;;) {
        if (length < 7) { insertion(values, clients, start, length); return; }
        int mid = start + length / 2;
        if (length > 7) {
            int left = start, end = start + length - 1;
            if (length > 40) {
                int distance = length / 8;
                left = median(values, clients, left, left + distance, left + 2 * distance);
                mid = median(values, clients, mid - distance, mid, mid + distance);
                end = median(values, clients, end - 2 * distance, end - distance, end);
            }
            mid = median(values, clients, left, mid, end);
        }
        swap(values, start, mid);
        int a = start + 1, b = a, c = start + length - 1, d = c;
        bool swapped = false;
        for (;;) {
            while (b <= c) {
                int result = compare(clients, values[b], values[start]);
                if (result > 0) break;
                if (!result) { swapped = true; swap(values, a, b); ++a; }
                ++b;
            }
            while (b <= c) {
                int result = compare(clients, values[c], values[start]);
                if (result < 0) break;
                if (!result) { swapped = true; swap(values, c, d); --d; }
                --c;
            }
            if (b > c) break;
            swap(values, b, c);
            swapped = true;
            ++b;
            --c;
        }
        if (!swapped) { insertion(values, clients, start, length); return; }
        int end = start + length;
        int range = a - start < b - a ? a - start : b - a;
        swap_range(values, start, b - range, range);
        range = d - c < end - d - 1 ? d - c : end - d - 1;
        swap_range(values, b, end - range, range);
        if (b - a > 1) sort(values, clients, start, b - a);
        range = d - c;
        if (range <= 1) return;
        start = end - range;
        length = range;
    }
}

static void integer_text(int32_t value, char text[12])
{
    unsigned char reversed[11];
    size_t count = 0;
    uint32_t bits = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    int32_t remaining;
    memcpy(&remaining, &bits, sizeof(remaining));
    do {
        reversed[count++] = (unsigned char)(48 + remaining % 10);
        remaining /= 10;
    } while (remaining);
    if (value < 0) reversed[count++] = '-';
    for (size_t index = 0; index < count; ++index)
        text[index] = (char)reversed[count - index - 1];
    text[count] = 0;
}

static bool score_configstring(rank_scope *scope, uint32_t index, qa_error *error)
{
    qa_q3_source_team_state team;
    qa_q3_source_client_counts counts;
    int32_t score = -9999;
    if (!live(scope, error)) return false;
    if (scope->game_type >= 3) {
        if (!qa_q3_source_team_state_read(scope->game, &team, error)) return false;
        score = team.team_scores[index + 1u];
    } else {
        if (!qa_q3_source_client_counts_read(scope->game, &counts, error)) return false;
        if (counts.num_connected > (int32_t)index) {
            rank_client client = {0};
            uint32_t slot = counts.sorted_clients[index];
            if (!client_read(scope, slot, &client, error) || !score_read(scope, slot, &client, error))
                return false;
            score = client.score;
        }
    }
    char text[12];
    integer_text(score, text);
    return qa_q3_configstring_write(scope->game, 6u + index, text, error) && live(scope, error);
}

static bool calculate(rank_scope *scope, qa_error *error)
{
    qa_q3_source_client_counts counts;
    rank_client clients[QA_Q3_NATIVE_CLIENTS] = {0};
    if (!qa_q3_source_client_counts_read(scope->game, &counts, error)) return false;
    counts.follow1 = counts.follow2 = -1;
    counts.num_connected = counts.num_non_spectator = counts.num_playing = counts.num_voting = 0;
    counts.num_team_voting[0] = counts.num_team_voting[1] = 0;
    for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
        rank_client *client = &clients[slot];
        if (!client_read(scope, slot, client, error)) return false;
        if (client->connected == QA_Q3_CLIENT_DISCONNECTED) continue;
        counts.sorted_clients[counts.num_connected++] = slot;
        if (!score_read(scope, slot, client, error)) return false;
        if (client->session.team == 3) continue;
        ++counts.num_non_spectator;
        if (client->connected != QA_Q3_CLIENT_CONNECTED) continue;
        ++counts.num_playing;
        if (!(client->server_flags & 8u)) {
            ++counts.num_voting;
            if (client->session.team == 1 || client->session.team == 2)
                ++counts.num_team_voting[client->session.team - 1];
        }
        if (counts.follow1 == -1) counts.follow1 = (int32_t)slot;
        else if (counts.follow2 == -1) counts.follow2 = (int32_t)slot;
    }
    for (uint32_t slot = 0; slot < scope->maximum; ++slot)
        if (!client_current(scope, slot, &clients[slot], error)) return false;
    sort(counts.sorted_clients, clients, 0, counts.num_connected);
    if (!qa_q3_source_client_counts_write(scope->game, &counts, error)) return false;
    if (scope->game_type >= 3) {
        qa_q3_source_team_state team;
        if (!qa_q3_source_team_state_read(scope->game, &team, error)) return false;
        int32_t rank = team.team_scores[1] == team.team_scores[2] ? 2 :
            team.team_scores[1] > team.team_scores[2] ? 0 : 1;
        for (int32_t index = 0; index < counts.num_connected; ++index)
            if (!qa_q3_client_rank(scope->game, counts.sorted_clients[index], rank, error)) return false;
    } else {
        int32_t rank = -1, score = 0;
        for (int32_t index = 0; index < counts.num_playing; ++index) {
            uint32_t slot = counts.sorted_clients[index];
            int32_t new_score = clients[slot].score;
            if (!index || new_score != score) {
                rank = index;
                if (!qa_q3_client_rank(scope->game, slot, rank, error)) return false;
            } else if (!qa_q3_client_rank(scope->game, counts.sorted_clients[index - 1], rank | 0x4000, error) ||
                       !qa_q3_client_rank(scope->game, slot, rank | 0x4000, error)) return false;
            score = new_score;
            if (scope->game_type == 2 && counts.num_playing == 1 &&
                !qa_q3_client_rank(scope->game, slot, rank | 0x4000, error)) return false;
        }
    }
    if (!score_configstring(scope, 0, error) || !score_configstring(scope, 1, error) ||
        !application_native_q3_match_check_exit(scope->provider, error) || !live(scope, error)) return false;
    int32_t intermission;
    if (!application_native_q3_match_intermission(scope->provider, &intermission, error)) return false;
    if (intermission) {
        for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
            rank_client client = {0};
            if (!client_read(scope, slot, &client, error)) return false;
            if (client.connected != QA_Q3_CLIENT_CONNECTED) continue;
            if (!client_current(scope, slot, &client, error) ||
                !application_native_q3_client_scoreboard(scope->provider, client.actor, error) ||
                !client_current(scope, slot, &client, error)) return false;
        }
    }
    return true;
}

bool application_native_q3_rank(application_provider *provider, qa_error *error)
{
    rank_scope scope;
    if (!begin(provider, &scope, error)) return false;
    return finish(&scope, calculate(&scope, error), error);
}
