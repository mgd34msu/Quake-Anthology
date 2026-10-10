/* Source behavior from id Software's code/game/g_team.c and the TypeScript
 * TeamRuntime translation. Copyright (C) 1999-2005 Id Software, Inc.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "qa/text.h"
#include "native_q3_team_status.h"
#include "native_q3_console.h"
#include "native_q3_wire_state.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_wire.h"

#include <stdlib.h>
#include <string.h>

enum { TEAM_OVERLAY_CLIENTS = 32, TEAM_ENTRY_BYTES = 1024,
       TEAM_MESSAGE_BYTES = 8192, TEAM_FORMAT_BYTES = 32000 };

struct application_native_q3_team_status {
    application_provider *provider;
    qa_q3_game *game;
    qa_actor_owner owner;
    size_t calls;
    bool initialized;
};

static struct application_native_q3_team_status *owner_at(const application_provider *provider)
{
    return provider && provider->kind == APPLICATION_PROVIDER_Q3
        ? provider->native_q3_team_status : NULL;
}

bool application_native_q3_team_status_create(application_provider *provider, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || provider->native_q3_team_status)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 team owner is already installed");
    struct application_native_q3_team_status *owner = calloc(1, sizeof(*owner));
    if (!owner)
        return application_fail(error, QA_ERROR_MEMORY, "allocating native Q3 team source binding");
    owner->provider = provider;
    provider->native_q3_team_status = owner;
    return true;
}

bool application_native_q3_team_status_idle(const application_provider *provider)
{
    const struct application_native_q3_team_status *owner = owner_at(provider);
    return !owner || !owner->calls;
}

bool application_native_q3_team_status_bound(const application_provider *provider)
{
    const struct application_native_q3_team_status *owner = owner_at(provider);
    qa_application *application = provider ? provider->application : NULL;
    return owner && owner->provider == provider && owner->initialized && application &&
        !application->destroy_requested && provider->constructed && provider->attached &&
        !provider->close_pending && provider->state.q3 && provider->state.q3 == owner->game &&
        provider->owner == owner->owner && application_native_q3_console_settings_bound(provider);
}

static bool install(application_provider *provider, bool reconnect, qa_error *error)
{
    struct application_native_q3_team_status *owner = owner_at(provider);
    qa_application *application = provider ? provider->application : NULL;
    if (!owner || owner->provider != provider || owner->calls ||
        (reconnect && owner->initialized) || !application || application->destroy_requested ||
        !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->state.q3 || !application_native_q3_console_settings_bound(provider))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "native Q3 team binding requires its idle initialized GAME source");
    owner->game = provider->state.q3;
    owner->owner = provider->owner;
    owner->initialized = true;
    return true;
}

bool application_native_q3_team_status_initialize(application_provider *provider, qa_error *error)
{
    return install(provider, false, error);
}

bool application_native_q3_team_status_reconnect(application_provider *provider, qa_error *error)
{
    return install(provider, true, error);
}

bool application_native_q3_team_status_destroy(application_provider *provider, qa_error *error)
{
    struct application_native_q3_team_status *owner = owner_at(provider);
    if (!owner) return true;
    if (owner->calls)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 team source is borrowed");
    provider->native_q3_team_status = NULL;
    free(owner);
    return true;
}

typedef struct team_scope {
    struct application_native_q3_team_status *binding;
    application_provider *provider;
    qa_application *application;
    qa_q3_game *game;
    qa_actor_owner owner;
    uint32_t maximum;
} team_scope;

static bool live(const team_scope *scope, qa_error *error)
{
    application_provider *provider = scope->provider;
    if (!application_native_q3_team_status_bound(provider) ||
        owner_at(provider) != scope->binding ||
        scope->application->destroy_requested ||
        provider->application != scope->application ||
        provider->kind != APPLICATION_PROVIDER_Q3 ||
        provider->state.q3 != scope->game || provider->owner != scope->owner ||
        !provider->constructed || !provider->attached || provider->close_pending)
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "native Q3 team source retired during its callback");
    return true;
}

static bool begin(application_provider *provider, team_scope *scope, qa_error *error)
{
    qa_application *application = provider ? provider->application : NULL;
    if (!application || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "team status requires its native Q3 source owner");
    struct application_native_q3_team_status *binding = owner_at(provider);
    *scope = (team_scope){binding, provider, application, provider->state.q3, provider->owner, 0};
    if (!live(scope, error) ||
        !qa_q3_source_max_clients(scope->game, &scope->maximum, error)) return false;
    if (binding->calls == SIZE_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "native Q3 team callback depth is exhausted");
    if (!application_native_q3_console_borrow(provider, error)) return false;
    ++binding->calls;
    return true;
}

static bool finish(team_scope *scope, bool okay, qa_error *error)
{
    if (okay) okay = live(scope, error);
    application_native_q3_console_release(scope->provider);
    --scope->binding->calls;
    return okay;
}

static bool binding_current(const team_scope *scope, uint32_t slot,
    const qa_q3_source_binding *expected, qa_error *error)
{
    qa_q3_source_binding actual;
    if (!live(scope, error) ||
        !qa_q3_source_binding_read(scope->game, slot, &actual, error)) return false;
    if (actual.in_use != expected->in_use ||
        !qa_actor_id_equal(actual.actor, expected->actor) ||
        ((actual.in_use || actual.actor.registry) &&
         !qa_actors_get(qa_session_actors(scope->application->session), actual.actor)))
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "team client lost its physical source generation");
    return true;
}

static bool client_read(const team_scope *scope, uint32_t slot,
    qa_q3_source_binding *binding, qa_q3_native_client *client, qa_error *error)
{
    return live(scope, error) &&
        qa_q3_source_binding_read(scope->game, slot, binding, error) &&
        qa_q3_client_slot_read(scope->game, slot, client, error) &&
        binding_current(scope, slot, binding, error);
}

static int32_t signed_bits(uint32_t bits)
{
    int32_t value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static size_t entry_format(char out[TEAM_ENTRY_BYTES], const int32_t fields[6])
{
    size_t used = 0;
    for (size_t index = 0; index < 6; ++index) {
        char word[32];
        size_t length = qa_format_q3_integer(fields[index], word);
        out[used++] = ' ';
        memcpy(out + used, word, length);
        used += length;
    }
    out[used] = 0;
    return used;
}

static bool team_of(const team_scope *scope, uint32_t slot, int32_t *team,
    qa_error *error)
{
    qa_q3_client_session session;
    if (!qa_q3_client_session_slot_read(scope->game, slot, &session, error) ||
        !live(scope, error)) return false;
    *team = session.team;
    return true;
}

static bool overlay(const team_scope *scope, uint32_t recipient,
    const qa_q3_source_binding *recipient_binding, int32_t recipient_team,
    qa_error *error)
{
    qa_q3_source_client_counts ranks;
    if (!qa_q3_source_client_counts_read(scope->game, &ranks, error) ||
        !binding_current(scope, recipient, recipient_binding, error)) return false;
    int32_t clients[TEAM_OVERLAY_CLIENTS];
    size_t selected = 0;
    for (uint32_t index = 0; index < scope->maximum && selected < TEAM_OVERLAY_CLIENTS; ++index) {
        if (ranks.sorted_clients[index] >= QA_Q3_SOURCE_CLIENTS)
            return application_fail(error, QA_ERROR_FORMAT,
                                    "team overlay has an invalid actual sorted client slot");
        uint32_t slot = ranks.sorted_clients[index];
        qa_q3_source_binding binding;
        qa_q3_native_client client;
        int32_t team;
        if (!client_read(scope, slot, &binding, &client, error)) return false;
        if (!binding.in_use) continue;
        if (!team_of(scope, slot, &team, error)) return false;
        if (team == recipient_team) clients[selected++] = (int32_t)ranks.sorted_clients[index];
    }
    for (size_t index = 1; index < selected; ++index)
        for (size_t position = index; position && clients[position] < clients[position - 1]; --position) {
            int32_t swap = clients[position];
            clients[position] = clients[position - 1];
            clients[position - 1] = swap;
        }

    /* The donor sorts a local selection which its output loop never reads. */
    char message[TEAM_MESSAGE_BYTES + 1];
    size_t used = 0;
    int32_t count = 0;
    for (uint32_t slot = 0; slot < scope->maximum && count < TEAM_OVERLAY_CLIENTS; ++slot) {
        qa_q3_source_binding binding;
        qa_q3_native_client client;
        int32_t team;
        if (!client_read(scope, slot, &binding, &client, error)) return false;
        if (!binding.in_use) continue;
        if (!team_of(scope, slot, &team, error)) return false;
        if (team != recipient_team) continue;
        qa_q3_player player;
        qa_q3_entity entity;
        qa_q3_wire_visibility visibility;
        if (!qa_q3_wire_player_read(scope->game, slot, &player, error) ||
            !binding_current(scope, slot, &binding, error) ||
            !qa_q3_wire_entity_read(scope->game, slot, &entity, &visibility, error) ||
            !binding_current(scope, slot, &binding, error)) return false;
        unsigned armor = player.product == QA_Q3_TEAM_ARENA ? 4u : 3u;
        int32_t fields[6] = {(int32_t)slot, client.rule.team_location,
            player.stats[0] < 0 ? 0 : player.stats[0],
            player.stats[armor] < 0 ? 0 : player.stats[armor],
            player.weapon, entity.powerups};
        char entry[TEAM_ENTRY_BYTES];
        size_t length = entry_format(entry, fields);
        if (used + length > TEAM_MESSAGE_BYTES) break;
        memcpy(message + used, entry, length);
        used += length;
        ++count;
    }
    message[used] = 0;
    char command[TEAM_MESSAGE_BYTES + 32], number[32];
    size_t digits = qa_format_q3_integer(count, number);
    memcpy(command, "tinfo ", 6);
    memcpy(command + 6, number, digits);
    command[6 + digits] = ' ';
    memcpy(command + 7 + digits, message, used + 1);
    return binding_current(scope, recipient, recipient_binding, error) &&
        application_native_q3_send_command(scope->provider, (int32_t)recipient, command, error) &&
        binding_current(scope, recipient, recipient_binding, error);
}

static bool status(team_scope *scope, qa_error *error)
{
    int32_t now, previous;
    if (!qa_q3_source_clock(scope->game, &now, error) ||
        !qa_q3_source_team_location_time_read(scope->game, &previous, error)) return false;
    if (signed_bits((uint32_t)now - (uint32_t)previous) <= 1000) return true;
    if (!qa_q3_source_team_location_time_set(scope->game, now, error)) return false;
    for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
        qa_q3_source_binding binding;
        qa_q3_native_client client;
        int32_t team;
        if (!client_read(scope, slot, &binding, &client, error)) return false;
        if (client.rule.connected != QA_Q3_CLIENT_CONNECTED || !binding.in_use) continue;
        if (!team_of(scope, slot, &team, error)) return false;
        if (team != 1 && team != 2) continue;
        qa_q3_map_team_location location;
        bool found;
        if (!qa_q3_map_team_location_read(scope->game, binding.actor, &location, &found, error) ||
            !binding_current(scope, slot, &binding, error) ||
            !qa_q3_client_team_location(scope->game, slot, found ? location.id : 0, error))
            return false;
    }
    for (uint32_t slot = 0; slot < scope->maximum; ++slot) {
        qa_q3_source_binding binding;
        qa_q3_native_client client;
        int32_t team;
        if (!client_read(scope, slot, &binding, &client, error)) return false;
        if (client.rule.connected != QA_Q3_CLIENT_CONNECTED || !binding.in_use) continue;
        if (!team_of(scope, slot, &team, error)) return false;
        if ((team == 1 || team == 2) && client.rule.team_info &&
            !overlay(scope, slot, &binding, team, error)) return false;
    }
    return true;
}

bool application_native_q3_team_status(application_provider *provider, qa_error *error)
{
    team_scope scope;
    if (!begin(provider, &scope, error)) return false;
    return finish(&scope, status(&scope, error), error);
}

static void append(char *out, size_t capacity, size_t *used, const char *text)
{
    while (*text && *used + 1 < capacity) out[(*used)++] = *text++;
    out[*used] = 0;
}

static bool location_message(team_scope *scope, qa_actor_id actor, char *out,
    size_t capacity, bool *found, qa_error *error)
{
    uint32_t slot;
    qa_q3_source_binding binding;
    qa_q3_native_client client;
    qa_q3_map_team_location location;
    if (!qa_q3_native_client_slot(scope->game, actor, &slot, error) ||
        !client_read(scope, slot, &binding, &client, error)) return false;
    if (!qa_actor_id_equal(binding.actor, actor))
        return application_fail(error, QA_ERROR_NOT_FOUND,
                                "chat location has no current physical source client");
    if (!qa_q3_map_team_location_read(scope->game, actor, &location, found, error) ||
        !binding_current(scope, slot, &binding, error)) return false;
    if (!*found) return true;
    const char *message = location.message
        ? qa_strings_cstr(qa_session_strings(scope->application->session), location.message)
        : "(null)";
    if (!message)
        return application_fail(error, QA_ERROR_FORMAT, "source location message has no live string");
    bool colored = location.count != 0;
    int32_t color = location.count < 0 ? 0 : location.count > 7 ? 7 : location.count;
    if (colored &&
        !qa_q3_map_location_count_set(scope->game, location.actor, color, error)) return false;
    size_t length = strlen(message);
    if (length >= (size_t)TEAM_FORMAT_BYTES - (colored ? 4u : 0u))
        return application_fail(error, QA_ERROR_FORMAT, "source location exceeds its GAME format buffer");
    size_t used = 0;
    if (colored) {
        char prefix[3] = {'^', (char)('0' + color), 0};
        append(out, capacity, &used, prefix);
    }
    append(out, capacity, &used, message);
    if (colored) append(out, capacity, &used, "^7");
    return binding_current(scope, slot, &binding, error);
}

bool application_native_q3_team_location(application_provider *provider, qa_actor_id actor,
    char *out, size_t capacity, bool *found, qa_error *error)
{
    if (!out || !capacity || !found)
        return application_fail(error, QA_ERROR_ARGUMENT, "source location requires a bounded destination");
    *found = false;
    team_scope scope;
    if (!begin(provider, &scope, error)) return false;
    return finish(&scope, location_message(&scope, actor, out, capacity, found, error), error);
}
