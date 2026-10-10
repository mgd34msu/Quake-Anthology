#include "native_q3_wire_state.h"
#include "native_q3_console.h"
#include "native_q3_clients.h"
#include "native_q3_wire.h"
#include "map_players_private.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_configstrings.h"
#include "qa/q3_abi.h"
#include "qa/source_save.h"
#include "qa/application_native_q3_wire.h"
#include "qa/application_startup_prepare.h"
#include "unified_q3_events.h"
#include "network_unified.h"
#include "control_frame.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CL_GetUserCmd uses the source CMD_BACKUP history, independently of the
 * smaller maximum usercmd bundle carried by a network message. */
#define NATIVE_Q3_COMMAND_BACKUP 64u
#define NATIVE_Q3_BIG_INFO_CHARS 8192u

typedef struct native_q3_wire_snapshot {
    qa_q3_snapshot value;
    qa_q3_entity *entities;
    size_t capacity;
    int32_t ping;
} native_q3_wire_snapshot;

typedef struct native_q3_wire_client {
    qa_actor_id actor;
    uint32_t seat;
    char *userinfo;
    char *drop_reason;
    qa_q3_reliable reliable;
    qa_q3_usercmd command;
    qa_q3_usercmd commands[NATIVE_Q3_COMMAND_BACKUP];
    int32_t command_sequence;
    int32_t snapshot_sequence, consumed_server_command, server_id, weapon;
    int32_t initial_server_command, config_commands[QA_Q3_CONFIGSTRINGS];
    uint64_t entered_ns;
    float sensitivity;
    qa_q3_gamestate *gamestate;
    qa_actor_id source_actors[QA_Q3_SOURCE_ENTITIES];
    int32_t bot_entities[256];
    uint32_t bot_entity_count;
    native_q3_wire_snapshot snapshots[QA_Q3_PACKET_BACKUP];
    char *big_configstring;
    size_t big_configstring_length;
    bool admitted, begun, bot, command_received, has_snapshot;
    bool drop_pending, drop_delivered;
    bool bot_snapshot_ready;
    qa_error drop_failure;
} native_q3_wire_client;

struct application_native_q3_wire_client_lease {
    struct application_native_q3_wire *wire;
    qa_actor_owner receiver;
    uint32_t seat, slot;
    qa_command_tokens *arguments;
    qa_command_tokens owned_arguments;
    qa_q3_game *game;
    qa_cvars *registry;
    uint64_t publication_generation, map_revision;
    int32_t initial_command_sequence, receipt_sequence;
    size_t calls;
    bool cgame, builtin, has_receipt, receipt_present;
};

struct application_native_q3_bot_cycle {
    struct application_native_q3_wire *wire;
    qa_q3_game *game;
    qa_world *world;
    qa_source_frame frame;
    uint64_t host_ns;
};

struct application_native_q3_wire {
    application_provider *provider;
    qa_world *world;
    qa_q3_host_server_services server;
    const qa_q3_host_options *preparing_services;
    void *frontend_lifetime;
    void (*release_frontend)(void *);
    native_q3_wire_client clients[QA_Q3_SOURCE_CLIENTS];
    uint32_t max_clients;
    size_t calls, client_leases;
    size_t slot_leases[QA_Q3_SOURCE_CLIENTS];
    size_t slot_readers[QA_Q3_SOURCE_CLIENTS];
    uint32_t lease_seats[QA_Q3_SOURCE_CLIENTS];
    uint64_t client_revision[QA_Q3_SOURCE_CLIENTS];
    struct application_native_q3_bot_cycle *bot_cycle;
    application_native_q3_bot_cycle bot_cycle_storage;
    uint8_t snapshot_bit;
    bool restore_pending, round_pending, replacement_pending, closing;
};

struct application_native_q3_wire_carry {
    qa_application *application;
    uint64_t registry;
    qa_actor_owner owner;
    qa_buffer bytes;
};

static struct application_native_q3_wire *wire_owner(application_provider *provider,
    qa_error *error)
{
    struct application_native_q3_wire *wire = provider ? provider->native_q3_wire : NULL;
    if (!wire || wire->provider != provider || provider->kind != APPLICATION_PROVIDER_Q3 ||
        !provider->state.q3 || wire->preparing_services || wire->restore_pending || wire->closing || provider->close_pending ||
        !provider->application || provider->application->destroy_requested) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 wire source is not admitted");
        return NULL;
    }
    return wire;
}

static bool source_binding(struct application_native_q3_wire *wire, uint32_t slot,
    qa_actor_id actor, qa_error *error)
{
    qa_q3_source_binding binding;
    uint32_t actual;
    return slot < wire->max_clients && actor.registry &&
        qa_q3_source_binding_read(wire->provider->state.q3, slot, &binding, error) &&
        qa_actor_id_equal(binding.actor, actor) &&
        qa_q3_native_client_slot(wire->provider->state.q3, actor, &actual, error) &&
        actual == slot;
}

static native_q3_wire_client *wire_client(application_provider *provider, uint32_t slot,
    struct application_native_q3_wire **owner, qa_error *error)
{
    *owner = wire_owner(provider, error);
    if (!*owner) return NULL;
    if (slot >= (*owner)->max_clients || !(*owner)->clients[slot].admitted ||
        !source_binding(*owner, slot, (*owner)->clients[slot].actor, error)) {
        application_fail(error, QA_ERROR_ARGUMENT,
                         "Native Q3 wire client has no current physical source binding");
        return NULL;
    }
    return &(*owner)->clients[slot];
}

static native_q3_wire_client *userinfo_client(application_provider *provider, uint32_t slot,
    struct application_native_q3_wire **owner, qa_error *error)
{
    *owner = wire_owner(provider, error);
    if (!*owner) return NULL;
    qa_q3_source_binding binding;
    if (slot >= (*owner)->max_clients ||
        !qa_q3_source_binding_read(provider->state.q3, slot, &binding, error) ||
        binding.client_slot != (int32_t)slot ||
        !source_binding(*owner, slot, binding.actor, error)) {
        application_fail(error, QA_ERROR_ARGUMENT,
                         "Native Q3 raw userinfo has no current fixed source client binding");
        return NULL;
    }
    native_q3_wire_client *client = &(*owner)->clients[slot];
    if (client->admitted && (!client->userinfo || !qa_actor_id_equal(client->actor, binding.actor))) {
        application_fail(error, QA_ERROR_ARGUMENT,
                         "Native Q3 raw userinfo differs from its connected source actor");
        return NULL;
    }
    return client;
}

static char *copy_text(const char *text, qa_error *error)
{
    if (!text) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 source text is absent");
        return NULL;
    }
    size_t length = strlen(text);
    if (length == SIZE_MAX) {
        application_fail(error, QA_ERROR_MEMORY, "Native Q3 source text extent overflow");
        return NULL;
    }
    char *copy = malloc(length + 1);
    if (!copy) {
        application_fail(error, QA_ERROR_MEMORY, "Retaining native Q3 source text");
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}

static int32_t source_integer(const char *text)
{
    while (*text == ' ' || (*text >= '\t' && *text <= '\r')) ++text;
    bool negative = *text == '-';
    if (*text == '-' || *text == '+') ++text;
    uint32_t value = 0;
    uint32_t limit = negative ? UINT32_C(2147483648) : UINT32_C(2147483647);
    while (*text >= '0' && *text <= '9') {
        uint32_t digit = (uint32_t)(*text++ - '0');
        value = value > (limit - digit) / 10 ? limit : value * 10 + digit;
    }
    uint32_t bits = negative ? 0u - value : value;
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static void bot_view_clear(native_q3_wire_client *client)
{
    client->bot_entity_count = 0;
    client->bot_snapshot_ready = false;
    memset(client->bot_entities, 0, sizeof(client->bot_entities));
}

static void client_world_clear(native_q3_wire_client *client)
{
    bot_view_clear(client);
    client->actor = (qa_actor_id){0};
    client->begun = false;
    memset(client->source_actors, 0, sizeof(client->source_actors));
    free(client->gamestate);
    client->gamestate = NULL;
    free(client->big_configstring);
    client->big_configstring = NULL;
    client->big_configstring_length = 0;
    client->initial_server_command = 0;
    memset(client->config_commands, 0, sizeof(client->config_commands));
    for (size_t i = 0; i < QA_Q3_PACKET_BACKUP; ++i) {
        free(client->snapshots[i].entities);
        client->snapshots[i] = (native_q3_wire_snapshot){0};
    }
    client->has_snapshot = false;
    client->snapshot_sequence = 0;
    memset(client->commands, 0, sizeof(client->commands));
    client->command_sequence = 0;
    client->server_id = 0;
    client->weapon = 0;
    client->sensitivity = 1;
}

static void clients_clear(struct application_native_q3_wire *wire)
{
    for (size_t i = 0; i < QA_Q3_SOURCE_CLIENTS; ++i) {
        free(wire->clients[i].userinfo);
        free(wire->clients[i].drop_reason);
        free(wire->clients[i].gamestate);
        free(wire->clients[i].big_configstring);
        for (size_t j = 0; j < QA_Q3_PACKET_BACKUP; ++j)
            free(wire->clients[i].snapshots[j].entities);
        wire->clients[i] = (native_q3_wire_client){.seat = UINT32_MAX, .sensitivity = 1};
        qa_q3_reliable_init(&wire->clients[i].reliable);
    }
}

bool qa_application_native_q3_wire_preconstruction_current(const qa_application *app,
    qa_actor_owner owner, uint32_t seat, const qa_q3_host_options *services)
{
    if (!app || !owner || seat != UINT32_MAX || !services || app->destroy_requested)
        return false;
    for (const application_provider *provider = app->live_providers; provider;
         provider = provider->next_live) {
        if (provider->owner != owner) continue;
        const struct application_native_q3_wire *wire = provider->native_q3_wire;
        return provider->application == app && provider->kind == APPLICATION_PROVIDER_Q3 &&
            provider->constructed && !provider->attached && !provider->close_pending &&
            provider->state.q3 && wire && wire->provider == provider && !wire->closing &&
            wire->calls == 1 && wire->preparing_services == services &&
            services->role == QA_QVM_GAME && services->owner == owner &&
            services->session == app->session && services->world == wire->world;
    }
    return false;
}

bool application_native_q3_wire_create(application_provider *provider, qa_world *source_world,
    bool restoring, qa_error *error)
{
    if (!provider || provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !provider->application || !source_world || provider->native_q3_wire ||
        !application_native_q3_console_registry(provider))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Native Q3 wire construction requires its GAME and scoped console");
    struct application_native_q3_wire *wire = calloc(1, sizeof(*wire));
    if (!wire)
        return application_fail(error, QA_ERROR_MEMORY, "Creating native Q3 wire owner");
    wire->provider = provider;
    wire->world = source_world;
    wire->restore_pending = restoring;
    clients_clear(wire);
    for (size_t i = 0; i < QA_Q3_SOURCE_CLIENTS; ++i) wire->lease_seats[i] = UINT32_MAX;
    bool ok = application_native_q3_wire_bind_sources(provider, error) &&
        qa_q3_source_max_clients(provider->state.q3, &wire->max_clients, error) &&
        wire->max_clients >= 1 && wire->max_clients <= QA_Q3_SOURCE_CLIENTS;
    qa_q3_host_options services = {.role = QA_QVM_GAME, .session = provider->application->session,
        .world = source_world, .owner = provider->owner};
    if (ok && provider->application->q3_services) {
        provider->native_q3_wire = wire;
        wire->preparing_services = &services;
        ++wire->calls;
        ok = provider->application->q3_services(provider->application->guest_context,
            provider->application, provider->owner, QA_QVM_GAME, UINT32_MAX, &services, error);
        --wire->calls;
        wire->preparing_services = NULL;
    }
    if (!ok) {
        provider->native_q3_wire = NULL;
        if (services.release_frontend) services.release_frontend(services.frontend_lifetime);
        free(wire);
        if (!error || !error->code)
            application_fail(error, QA_ERROR_FORMAT, "Native Q3 wire has invalid physical client capacity");
        return false;
    }
    wire->server = services.server;
    wire->frontend_lifetime = services.frontend_lifetime;
    wire->release_frontend = services.release_frontend;
    provider->native_q3_wire = wire;
    return true;
}

bool application_native_q3_wire_idle(const application_provider *provider)
{
    const struct application_native_q3_wire *wire = provider ? provider->native_q3_wire : NULL;
    return !wire || !wire->calls;
}

bool application_native_q3_wire_destroy_ready(const application_provider *provider)
{
    const struct application_native_q3_wire *wire = provider ? provider->native_q3_wire : NULL;
    return !wire || (wire->provider == provider && !wire->calls && !wire->client_leases && !wire->closing);
}

bool application_native_q3_wire_destroy(application_provider *provider, qa_error *error)
{
    struct application_native_q3_wire *wire = provider ? provider->native_q3_wire : NULL;
    if (!wire) return true;
    if (wire->provider != provider || wire->calls || wire->client_leases || wire->closing)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 wire source is still borrowed");
    wire->closing = true;
    if (wire->release_frontend) wire->release_frontend(wire->frontend_lifetime);
    clients_clear(wire);
    provider->native_q3_wire = NULL;
    free(wire);
    return true;
}

bool application_native_q3_wire_connect(application_provider *provider, uint32_t slot,
    qa_actor_id actor, uint32_t seat, const char *userinfo, bool bot, qa_error *error)
{
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    if (!wire) return false;
    if (!source_binding(wire, slot, actor, error) || (bot && seat != UINT32_MAX) ||
        (wire->slot_leases[slot] && (bot || wire->lease_seats[slot] != seat)) ||
        wire->client_revision[slot] == UINT64_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Native Q3 wire Connect has no unique physical client admission");
    if (wire->round_pending && wire->clients[slot].admitted) {
        native_q3_wire_client *client = &wire->clients[slot];
        if (!userinfo || client->seat != seat || client->bot != bot || client->begun ||
            (client->actor.registry && !qa_actor_id_equal(client->actor, actor)))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 retained Connect differs from its carried source client");
        char *copy = copy_text(userinfo, error);
        if (!copy) return false;
        free(client->userinfo);
        client->userinfo = copy;
        client->actor = actor;
        ++wire->client_revision[slot];
        return true;
    }
    if (wire->clients[slot].admitted)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 source client is already connected");
    char *copy = copy_text(userinfo, error);
    if (!copy) return false;
    native_q3_wire_client *client = &wire->clients[slot];
    free(client->userinfo);
    free(client->drop_reason);
    *client = (native_q3_wire_client){.actor = actor, .seat = seat,
        .userinfo = copy, .admitted = true, .bot = bot, .sensitivity = 1,
        .entered_ns = qa_session_elapsed(provider->application->session)};
    qa_q3_reliable_init(&client->reliable);
    ++wire->client_revision[slot];
    return true;
}

bool application_native_q3_wire_begin(application_provider *provider, uint32_t slot,
    qa_error *error)
{
    struct application_native_q3_wire *wire;
    native_q3_wire_client *client = wire_client(provider, slot, &wire, error);
    qa_q3_native_client source;
    if (!client) return false;
    if (!qa_q3_client_slot_read(provider->state.q3, slot, &source, error) ||
        source.rule.connected != QA_Q3_CLIENT_CONNECTED)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Native Q3 wire Begin requires completed source ClientBegin");
    client->begun = true;
    return true;
}

bool application_native_q3_wire_disconnect(application_provider *provider, uint32_t slot,
    qa_error *error)
{
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    if (!wire) return false;
    if (slot >= wire->max_clients || wire->client_revision[slot] == UINT64_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 wire Disconnect exceeds source capacity");
    native_q3_wire_client *client = &wire->clients[slot];
    char *userinfo = client->userinfo;
    free(client->drop_reason);
    free(client->gamestate);
    free(client->big_configstring);
    for (size_t i = 0; i < QA_Q3_PACKET_BACKUP; ++i)
        free(client->snapshots[i].entities);
    *client = (native_q3_wire_client){.seat = UINT32_MAX, .userinfo = userinfo, .sensitivity = 1};
    qa_q3_reliable_init(&client->reliable);
    ++wire->client_revision[slot];
    return true;
}

bool application_native_q3_wire_userinfo(application_provider *provider, uint32_t slot,
    const char *text, qa_error *error)
{
    struct application_native_q3_wire *wire;
    native_q3_wire_client *client = userinfo_client(provider, slot, &wire, error);
    if (!client) return false;
    char *copy = copy_text(text, error);
    if (!copy) return false;
    free(client->userinfo);
    client->userinfo = copy;
    return true;
}

bool application_native_q3_wire_userinfo_read(application_provider *provider, uint32_t slot,
    const char **out, qa_error *error)
{
    struct application_native_q3_wire *wire;
    native_q3_wire_client *client = userinfo_client(provider, slot, &wire, error);
    if (!client || !out)
        return client ? application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 userinfo output is absent") : false;
    *out = client->userinfo ? client->userinfo : "";
    return true;
}

bool application_native_q3_wire_source_userinfo_read(application_provider *provider, uint32_t slot,
    const char **out, qa_error *error)
{
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    if (!wire) return false;
    uint32_t maximum;
    if (!out || slot >= wire->max_clients ||
        !qa_q3_source_max_clients(provider->state.q3, &maximum, error) ||
        maximum != wire->max_clients ||
        (wire->clients[slot].admitted && !wire->clients[slot].userinfo))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 fixed source slot has no retained userinfo");
    *out = wire->clients[slot].userinfo ? wire->clients[slot].userinfo : "";
    return true;
}

bool application_native_q3_wire_command(application_provider *provider, uint32_t slot,
    const qa_q3_usercmd *command, qa_error *error)
{
    struct application_native_q3_wire *wire;
    native_q3_wire_client *client = wire_client(provider, slot, &wire, error);
    if (!client) return false;
    if (!command || client->drop_pending || wire->round_pending || client->command_sequence == INT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 accepted command sequence is unavailable");
    client->command = *command;
    client->command_received = true;
    ++client->command_sequence;
    client->commands[(uint32_t)client->command_sequence & (NATIVE_Q3_COMMAND_BACKUP - 1)] = *command;
    return true;
}

bool application_native_q3_wire_command_seed(application_provider *provider, uint32_t slot,
    const qa_q3_usercmd *command, qa_error *error)
{
    struct application_native_q3_wire *wire;
    native_q3_wire_client *client = wire_client(provider, slot, &wire, error);
    if (!client) return false;
    if (!command || client->begun || client->drop_pending || wire->round_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 command seed requires its connected pre-Begin client");
    client->command = *command;
    client->command_received = true;
    return true;
}

static bool client_view(application_provider *provider, uint32_t slot,
    application_native_q3_wire_client_view *out, bool *present, bool stable, qa_error *error)
{
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    if (!wire) return false;
    if (!out || !present || slot >= wire->max_clients || (stable && wire->round_pending))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client read has no stable source slot");
    *out = (application_native_q3_wire_client_view){0};
    *present = wire->clients[slot].admitted &&
        (!stable || !wire->clients[slot].drop_pending);
    if (!*present) return true;
    native_q3_wire_client *client = &wire->clients[slot];
    if (!source_binding(wire, slot, client->actor, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client read lost its physical source binding");
    *out = (application_native_q3_wire_client_view){.actor = client->actor,
        .userinfo = client->userinfo, .seat = client->seat, .entered_ns = client->entered_ns,
        .begun = client->begun, .bot = client->bot,
        .command_received = client->command_received};
    if (out->command_received) out->command = client->command;
    return true;
}

bool application_native_q3_wire_client_read(application_provider *provider, uint32_t slot,
    application_native_q3_wire_client_view *out, bool *present, qa_error *error)
{
    return client_view(provider, slot, out, present, true, error);
}

bool application_native_q3_wire_client_admission_read(application_provider *provider, uint32_t slot,
    application_native_q3_wire_client_view *out, bool *present, qa_error *error)
{
    return client_view(provider, slot, out, present, false, error);
}

bool application_native_q3_wire_drop(application_provider *provider, uint32_t slot,
    const char *reason, qa_error *error)
{
    struct application_native_q3_wire *wire;
    native_q3_wire_client *client = wire_client(provider, slot, &wire, error);
    if (!client) return false;
    if (!reason)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client DROP reason is absent");
    if (client->drop_pending) return true;
    char *copy = copy_text(reason, error);
    if (!copy) return false;
    if (!application_unified_q3_text(provider, APPLICATION_Q3_SOURCE_DROP, (int32_t)slot, copy, error)) {
        free(copy); return false;
    }
    client->drop_reason = copy;
    client->drop_pending = true;
    return true;
}

bool application_native_q3_wire_drop_read(application_provider *provider, uint32_t slot,
    const char **reason, bool *pending, qa_error *error)
{
    qa_actor_id actor;
    return application_native_q3_wire_drop_client_read(provider, slot, &actor, reason, pending, error);
}

bool application_native_q3_wire_drop_client_read(application_provider *provider, uint32_t slot,
    qa_actor_id *actor, const char **reason, bool *pending, qa_error *error)
{
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    if (!wire) return false;
    if (!actor || !reason || !pending || slot >= wire->max_clients)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 DROP read has no actual engine source slot");
    native_q3_wire_client *client = &wire->clients[slot];
    *pending = client->admitted && client->drop_pending;
    *reason = *pending ? client->drop_reason : NULL;
    *actor = *pending ? client->actor : (qa_actor_id){0};
    if (*pending && !source_binding(wire, slot, *actor, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 pending DROP lost its retained actor generation");
    return true;
}

bool application_native_q3_wire_drop_transport(application_provider *provider, uint32_t slot,
    qa_error *error)
{
    struct application_native_q3_wire *wire;
    native_q3_wire_client *client = wire_client(provider, slot, &wire, error);
    if (!client) return false;
    if (!client->drop_pending || !client->drop_reason || wire->calls == SIZE_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 transport DROP has no retained source request");
    if (client->drop_delivered) {
        if (client->drop_failure.code) {
            if (error) *error = client->drop_failure;
            return false;
        }
        return true;
    }
    if (!wire->server.drop_client) {
        qa_actor_id actor;
        qa_net_client_id recipient;
        qa_net_seat_id seat;
        bool remote;
        if (!application_unified_source_drop_recipient(provider->application, provider->owner, slot,
            client->drop_reason, &actor, &recipient, &seat, &remote, error)) return false;
        if (remote)
            return application_fail(error, QA_ERROR_UNSUPPORTED,
                "Native Q3 remote DROP has no actual transport owner");
    }
    /* Mark entry before invoking a fallible external transport. A failed
     * callback may have committed its peer retirement and must not replay. */
    client->drop_delivered = true;
    if (!wire->server.drop_client) return true;
    char *reason = copy_text(client->drop_reason, error);
    if (!reason) {
        client->drop_delivered = false;
        return false;
    }
    ++wire->calls;
    qa_q3_game *game = provider->state.q3;
    qa_actor_id actor = client->actor;
    uint64_t revision = wire->client_revision[slot];
    qa_error failure = {0};
    bool ok = wire->server.drop_client(wire->server.context, slot, reason, &failure);
    free(reason);
    bool current = wire_owner(provider, NULL) == wire && provider->state.q3 == game &&
        wire->client_revision[slot] == revision && client->admitted && client->drop_pending &&
        qa_actor_id_equal(client->actor, actor) && source_binding(wire, slot, actor, NULL);
    if (ok && !current)
        ok = application_fail(&failure, QA_ERROR_ARGUMENT, "Native Q3 transport DROP retired or replaced its source request");
    --wire->calls;
    if (!ok) {
        if (!failure.code)
            application_fail(&failure, QA_ERROR_IO, "Native Q3 transport DROP failed after source entry");
        if (current) client->drop_failure = failure;
        if (error) *error = failure;
    }
    return ok;
}

bool application_native_q3_wire_local_publication(application_provider *provider, uint32_t slot,
    application_native_q3_wire_publication *out, bool *wanted, qa_error *error)
{
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    if (!wire) return false;
    if (!out || !wanted || slot >= wire->max_clients || wire->round_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 local publication has no completed engine source");
    *out = (application_native_q3_wire_publication){0};
    native_q3_wire_client *client = &wire->clients[slot];
    bool snapshots = wire->slot_readers[slot] != 0 || client->bot;
    bool gamestate = wire->slot_leases[slot] != 0 || client->bot;
    *wanted = client->admitted && client->begun && !client->drop_pending &&
              (snapshots || (gamestate && !client->gamestate));
    if (!*wanted) return true;
    if (!source_binding(wire, slot, client->actor, error) ||
        (snapshots && client->snapshot_sequence == INT32_MAX))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 local publication lost its physical binding or sequence");
    *out = (application_native_q3_wire_publication){
        .next_message = client->snapshot_sequence + 1,
        .reliable_sequence = client->reliable.sequence,
        .snapshot_bit = wire->snapshot_bit,
        .gamestate_needed = client->gamestate == NULL ||
            (snapshots && !client->bot && !client->has_snapshot),
        .snapshot_needed = snapshots, .has_snapshot = client->has_snapshot};
    if (client->has_snapshot) out->previous_time = client->snapshots[
        (uint32_t)client->snapshot_sequence & (QA_Q3_PACKET_BACKUP - 1)].value.server_time;
    return true;
}

static bool bot_cycle_current(const application_native_q3_bot_cycle *cycle, qa_error *error)
{
    struct application_native_q3_wire *wire = cycle ? cycle->wire : NULL;
    application_provider *provider = wire ? wire->provider : NULL;
    qa_source_frame actual;
    uint64_t host_ns;
    if (!wire || wire->bot_cycle != cycle || wire_owner(provider, error) != wire ||
        provider->state.q3 != cycle->game || provider->application->world != cycle->world ||
        !qa_session_active_frame(provider->application->session, provider->owner, &actual) ||
        !qa_session_frame_host_time(provider->application->session, &host_ns) || host_ns != cycle->host_ns ||
        actual.kind != cycle->frame.kind || actual.number != cycle->frame.number ||
        actual.start_ns != cycle->frame.start_ns || actual.elapsed_ns != cycle->frame.elapsed_ns ||
        actual.time_ns != cycle->frame.time_ns)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 bot view left its actual source frame");
    return true;
}

bool application_native_q3_bot_cycle_begin(application_provider *provider, const qa_source_frame *frame,
    uint64_t host_ns, application_native_q3_bot_cycle **out, qa_error *error)
{
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    if (!wire) return false;
    qa_source_frame actual;
    uint64_t actual_host;
    if (!frame || !out || *out || wire->bot_cycle || wire->round_pending || wire->calls == SIZE_MAX ||
        !provider->constructed || !provider->attached || provider->application->state != QA_APPLICATION_RUNNING ||
        wire->world != provider->application->world || frame->provider != provider->owner ||
        !qa_session_active_frame(provider->application->session, provider->owner, &actual) ||
        !qa_session_frame_host_time(provider->application->session, &actual_host) || actual_host != host_ns ||
        actual.kind != QA_RULESET_Q3 || actual.kind != frame->kind || actual.number != frame->number ||
        actual.start_ns != frame->start_ns || actual.elapsed_ns != frame->elapsed_ns ||
        actual.time_ns != frame->time_ns)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 bot cycle requires its genuine active source admission");
    application_native_q3_bot_cycle *cycle = &wire->bot_cycle_storage;
    *cycle = (application_native_q3_bot_cycle){.wire = wire, .game = provider->state.q3,
        .world = wire->world, .frame = actual, .host_ns = host_ns};
    for (uint32_t i = 0; i < QA_Q3_SOURCE_CLIENTS; ++i) bot_view_clear(&wire->clients[i]);
    wire->bot_cycle = cycle;
    ++wire->calls;
    *out = cycle;
    return true;
}

void application_native_q3_bot_cycle_end(application_native_q3_bot_cycle **owned)
{
    application_native_q3_bot_cycle *cycle = owned ? *owned : NULL;
    if (!cycle) return;
    cycle->wire->bot_cycle = NULL;
    --cycle->wire->calls;
    *owned = NULL;
}

bool application_native_q3_bot_snapshot_entity(application_provider *provider, qa_actor_id actor,
    int32_t index, int32_t *number, bool *present, qa_error *error)
{
    uint32_t slot;
    if (!provider || !number || !present ||
        !qa_q3_native_client_slot(provider->state.q3, actor, &slot, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 bot snapshot request has no actual source client");
    struct application_native_q3_wire *wire;
    native_q3_wire_client *client = wire_client(provider, slot, &wire, error);
    if (!client) return false;
    if (!client->bot || !client->begun || client->drop_pending || wire->round_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 bot snapshot source is not begun");
    *number = -1;
    *present = false;
    if (index < 0) return true;
    if (wire->bot_cycle) {
        if (!bot_cycle_current(wire->bot_cycle, error)) return false;
    } else if (!qa_session_safe(provider->application->session) || !qa_world_idle(wire->world))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 bot view has no retained source cycle or idle source");
    if (!client->bot_snapshot_ready) {
        if (wire->calls == SIZE_MAX)
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 bot view source is already fully borrowed");
        qa_unified_frame_lease *storage=application_control_storage_acquire(provider->application,error);
        if (!storage) return false;
        qa_q3_visible_entities *visible=qa_unified_frame_lease_alloc(storage,1,sizeof(*visible),
            _Alignof(qa_q3_visible_entities),error);
        if (!visible) { qa_unified_frame_lease_release(storage);return false; }
        qa_q3_player player;
        ++wire->calls;
        bool ok = application_native_q3_wire_current_view(provider, slot, &player, visible, error);
        if (ok && (wire_owner(provider, error) != wire ||
            !source_binding(wire, slot, actor, error) || client->drop_pending ||
            (wire->bot_cycle && !bot_cycle_current(wire->bot_cycle, error))))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 bot visibility retired during selection");
        if (ok) {
            client->bot_entity_count = (uint32_t)visible->count;
            for (size_t i = 0; i < visible->count; ++i) client->bot_entities[i] = visible->entities[i].number;
            client->bot_snapshot_ready = true;
        }
        --wire->calls;
        qa_unified_frame_lease_release(storage);
        if (!ok) return false;
    }
    if ((uint32_t)index < client->bot_entity_count) {
        *number = client->bot_entities[index];
        *present = true;
    }
    return true;
}

bool application_native_q3_wire_record_read(application_provider *provider,uint32_t slot,
    qa_q3_gamestate *state,int32_t *sequence,qa_error *error)
{
    struct application_native_q3_wire *wire;
    native_q3_wire_client *client=wire_client(provider,slot,&wire,error);
    if(!client)return false;
    if(!sequence||wire->calls||wire->round_pending||!client->begun||client->bot||
        client->drop_pending||!client->gamestate||!qa_session_safe(provider->application->session))
        return application_fail(error,QA_ERROR_ARGUMENT,"Native Q3 recording requires its returned local client publication");
    if(state) {
        *state=*client->gamestate;
        for(uint32_t i=0;i<QA_Q3_CONFIGSTRINGS;++i) {
            const char *text;
            if(!qa_q3_configstring_read(provider->state.q3,i,&text,error)||
                !qa_q3_configstring_set(state,i,text,error))return false;
        }
        state->command_sequence=client->reliable.sequence;
    }
    *sequence=client->reliable.sequence;return true;
}
bool application_native_q3_wire_record_command(application_provider *provider,uint32_t slot,
    int32_t sequence,const char **text,qa_error *error)
{
    int32_t latest;
    if(!text||!application_native_q3_wire_record_read(provider,slot,NULL,&latest,error))return false;
    if(sequence<=0||sequence>latest||(int64_t)latest-sequence>=QA_Q3_RELIABLE)
        return application_fail(error,QA_ERROR_FORMAT,"Native Q3 recording command left its actual retained reliable ring");
    *text=qa_q3_reliable_lookup(&provider->native_q3_wire->clients[slot].reliable,sequence);
    return true;
}

bool application_native_q3_wire_gamestate(application_provider *provider, uint32_t slot,
    const qa_q3_gamestate *value, qa_error *error)
{
    struct application_native_q3_wire *wire;
    native_q3_wire_client *client = wire_client(provider, slot, &wire, error);
    if (!client) return false;
    if (!value || value->client_number != (int32_t)slot ||
        value->command_sequence != client->reliable.sequence ||
        !value->string_bytes || value->string_bytes > QA_Q3_GAMESTATE_CHARS ||
        value->strings[0] || client->has_snapshot || wire->round_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 gamestate publication has no initial source observation");
    for (size_t i = 0; i < QA_Q3_CONFIGSTRINGS; ++i)
        if (value->config_offsets[i] >= value->string_bytes ||
            !memchr(value->strings + value->config_offsets[i], 0,
                    value->string_bytes - value->config_offsets[i]))
            return application_fail(error, QA_ERROR_FORMAT, "Native Q3 gamestate has an invalid configstring view");
    qa_q3_gamestate *copy = malloc(sizeof(*copy));
    if (!copy)
        return application_fail(error, QA_ERROR_MEMORY, "Retaining native Q3 source gamestate");
    *copy = *value;
    bool clamped;
    if (!client->bot && !qa_q3_reliable_ack(&client->reliable, QA_Q3_SERVER,
                            client->reliable.sequence, &clamped, error)) {
        free(copy);
        return false;
    }
    free(client->gamestate);
    client->gamestate = copy;
    client->initial_server_command = value->command_sequence;
    for (size_t i = 0; i < QA_Q3_CONFIGSTRINGS; ++i)
        client->config_commands[i] = value->command_sequence;
    if (!client->bot) client->consumed_server_command = client->reliable.sequence;
    return true;
}

bool application_native_q3_wire_snapshot(application_provider *provider, uint32_t slot,
    const qa_q3_snapshot *value, int32_t ping, qa_error *error)
{
    struct application_native_q3_wire *wire;
    native_q3_wire_client *client = wire_client(provider, slot, &wire, error);
    if (!client) return false;
    if (!value || !value->valid || !client->begun || !client->gamestate ||
        wire->round_pending || value->message_number <= client->snapshot_sequence ||
        value->server_command_number != client->reliable.sequence ||
        value->player.clientNum < 0 || (uint32_t)value->player.clientNum >= QA_Q3_SOURCE_CLIENTS ||
        value->entity_count > 256 ||
        (value->entity_count && !value->entities) || value->area_bytes > sizeof(value->area_mask) ||
        value->flags != wire->snapshot_bit)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 snapshot does not follow its actual source client");
    int32_t previous = -1;
    for (size_t i = 0; i < value->entity_count; ++i) {
        if (value->entities[i].number <= previous ||
            value->entities[i].number >= QA_Q3_ENTITY_WORLD)
            return application_fail(error, QA_ERROR_FORMAT, "Native Q3 snapshot entity order is invalid");
        previous = value->entities[i].number;
    }
    qa_actor_id observed[QA_Q3_SOURCE_ENTITIES];
    memcpy(observed, client->source_actors, sizeof(observed));
    uint32_t count;
    if (!qa_q3_source_entity_count(provider->state.q3, &count, error) || count > QA_Q3_SOURCE_ENTITIES)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q3 snapshot source namespace exceeds its physical extent");
    for (uint32_t number = 0; number < count; ++number) {
        qa_q3_source_binding binding;
        if (!qa_q3_source_binding_read(provider->state.q3, number, &binding, error)) return false;
        if (binding.actor.registry) observed[number] = binding.actor;
    }
    native_q3_wire_snapshot *snapshot = &client->snapshots[
        (uint32_t)value->message_number & (QA_Q3_PACKET_BACKUP - 1)];
    if (snapshot->capacity < value->entity_count) {
        qa_q3_entity *entities = realloc(snapshot->entities,
                                         value->entity_count * sizeof(*entities));
        if (!entities)
            return application_fail(error, QA_ERROR_MEMORY, "Retaining native Q3 snapshot entities");
        memset(entities + snapshot->capacity, 0,
               (value->entity_count - snapshot->capacity) * sizeof(*entities));
        snapshot->entities = entities;
        snapshot->capacity = value->entity_count;
    }
    uint32_t cursor = 0;
    if (client->has_snapshot) {
        const qa_q3_snapshot *latest = &client->snapshots[
            (uint32_t)client->snapshot_sequence & (QA_Q3_PACKET_BACKUP - 1)].value;
        cursor = (uint32_t)latest->parse_entities_number + (uint32_t)latest->entity_count;
    }
    if (value->entity_count)
        memcpy(snapshot->entities, value->entities,
               value->entity_count * sizeof(*value->entities));
    snapshot->value = *value;
    snapshot->value.entities = snapshot->entities;
    snapshot->value.parse_entities_number = cursor;
    snapshot->ping = ping;
    client->snapshot_sequence = value->message_number;
    client->has_snapshot = true;
    memcpy(client->source_actors, observed, sizeof(observed));
    return true;
}

bool application_native_q3_wire_snapshot_bit(application_provider *provider, uint8_t *out,
    qa_error *error)
{
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    if (!wire) return false;
    if (!out) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q3 snapshot bit output");
    *out = wire->snapshot_bit;
    return true;
}

bool application_native_q3_wire_round_ready(application_provider *provider, qa_error *error)
{
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    if (!wire) return false;
    return (!wire->round_pending && !wire->calls &&
            application_native_q3_wire_finish(provider, error)) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 wire round requires stable physical source clients");
}

bool application_native_q3_wire_round_begin(application_provider *provider, qa_error *error)
{
    if (!application_native_q3_wire_round_ready(provider, error)) return false;
    struct application_native_q3_wire *wire = provider->native_q3_wire;
    wire->round_pending = true;
    wire->snapshot_bit ^= 4;
    for (uint32_t slot = 0; slot < wire->max_clients; ++slot) {
        native_q3_wire_client *client = &wire->clients[slot];
        if (!client->admitted) continue;
        bot_view_clear(client);
        client->actor = (qa_actor_id){0};
        client->begun = false;
        memset(client->source_actors, 0, sizeof(client->source_actors));
    }
    return true;
}

bool application_native_q3_wire_round_bind(application_provider *provider, uint32_t slot,
    qa_actor_id actor, qa_error *error)
{
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    if (!wire) return false;
    if (!wire->round_pending || slot >= wire->max_clients ||
        !wire->clients[slot].admitted ||
        (wire->clients[slot].actor.registry &&
         !qa_actor_id_equal(wire->clients[slot].actor, actor)) ||
        !source_binding(wire, slot, actor, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 round bind has no fresh physical source client");
    wire->clients[slot].actor = actor;
    return true;
}

bool application_native_q3_wire_round_finish(application_provider *provider, qa_error *error)
{
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    if (!wire) return false;
    if (!wire->round_pending || wire->calls)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 round completion has no idle retained wire cut");
    for (uint32_t slot = 0; slot < wire->max_clients; ++slot)
        if (wire->clients[slot].admitted && !wire->clients[slot].begun)
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 retained client has not completed source ClientBegin");
    if (!application_native_q3_wire_finish(provider, error)) return false;
    wire->round_pending = false;
    wire->replacement_pending = false;
    return true;
}

bool application_native_q3_wire_map_begin(application_provider *provider, qa_error *error)
{
    if (!application_native_q3_wire_round_ready(provider, error)) return false;
    struct application_native_q3_wire *wire = provider->native_q3_wire;
    if (!provider->attached || !provider->constructed ||
        wire->world != provider->application->world ||
        !qa_session_safe(provider->application->session) || !qa_world_idle(wire->world))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 world cut has no stable retained source");
    for (uint32_t slot = 0; slot < wire->max_clients; ++slot)
        if (wire->clients[slot].admitted && wire->clients[slot].drop_pending)
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 world cut requires its pending source DROP to finish");
    wire->round_pending = true;
    wire->replacement_pending = true;
    wire->snapshot_bit ^= 4;
    for (uint32_t slot = 0; slot < wire->max_clients; ++slot)
        if (wire->clients[slot].admitted) client_world_clear(&wire->clients[slot]);
    return true;
}

bool application_native_q3_wire_map_finish(application_provider *provider, qa_error *error)
{
    return application_native_q3_wire_carry_finish(provider, error);
}

static bool send_command_transport(application_provider *provider, int32_t slot,
    const char *text, qa_error *error)
{
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    if (!wire) return false;
    if (!text || strlen(text) >= QA_Q3_COMMAND_CHARS || slot < -1 ||
        slot >= (int32_t)wire->max_clients || wire->calls == SIZE_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 server command recipient is invalid");
    uint64_t recipients = 0, retain = 0, overflow = 0;
    char *drop_reasons[QA_Q3_SOURCE_CLIENTS] = {0};
    uint64_t revisions[QA_Q3_SOURCE_CLIENTS] = {0};
    qa_actor_id actors[QA_Q3_SOURCE_CLIENTS] = {0};
    qa_q3_game *game = provider->state.q3;
    uint32_t maximum = wire->max_clients;
    bool round_pending = wire->round_pending;
    bool replacement_pending = wire->replacement_pending;
    for (uint32_t i = 0; i < wire->max_clients; ++i) {
        native_q3_wire_client *client = &wire->clients[i];
        if (!client->admitted || (slot >= 0 && i != (uint32_t)slot)) continue;
        if (!wire->round_pending && !source_binding(wire, i, client->actor, error)) {
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 reliable recipient lost its source admission");
        }
        recipients |= UINT64_C(1) << i;
        revisions[i] = wire->client_revision[i];
        actors[i] = client->actor;
        bool local = wire->slot_readers[i] != 0;
        /* A remote peer is the sole owner of its channel's reliable sequence.
         * Retained source queues belong to real bots and local client readers. */
        if ((!local && !client->bot) || client->drop_pending) continue;
        int64_t outstanding = (int64_t)client->reliable.sequence - client->reliable.acknowledged;
        if (outstanding < 0 || outstanding > QA_Q3_RELIABLE ||
            client->reliable.sequence == INT32_MAX)
            return application_fail(error, QA_ERROR_FORMAT, "Native Q3 retained source reliable capacity is exhausted");
        retain |= UINT64_C(1) << i;
        if (outstanding == QA_Q3_RELIABLE) overflow |= UINT64_C(1) << i;
    }
    char *retained = copy_text(text, error);
    if (!retained) return false;
    for (uint32_t i = 0; i < maximum; ++i) {
        if (!(overflow & (UINT64_C(1) << i))) continue;
        drop_reasons[i] = copy_text("Server command overflow", error);
        if (!drop_reasons[i]) {
            for (uint32_t j = 0; j < maximum; ++j) free(drop_reasons[j]);
            free(retained);
            return false;
        }
    }
    text = retained;
    ++wire->calls;
    bool ok = true;
    /* Retain the authored source command before transport callbacks. Nested
     * authored sends then follow it in each real local/bot command history. */
    for (uint32_t i = 0; ok && i < maximum; ++i) {
        if (!(retain & (UINT64_C(1) << i))) continue;
        native_q3_wire_client *client = &wire->clients[i];
        qa_error current = {0};
        bool added = qa_q3_reliable_add(&client->reliable, QA_Q3_SERVER, text, &current);
        if (!added && (overflow & (UINT64_C(1) << i))) {
            client->drop_reason = drop_reasons[i];
            drop_reasons[i] = NULL;
            client->drop_pending = true;
        } else if (!added) {
            if (error) *error = current;
            ok = false;
        }
    }
    bool bot_target = slot >= 0 && wire->clients[slot].admitted && wire->clients[slot].bot;
    if (ok && !bot_target && wire->server.send_command)
        ok = wire->server.send_command(wire->server.context, slot, text, error);
    if (ok && (wire_owner(provider, error) != wire || provider->state.q3 != game ||
        wire->max_clients != maximum || wire->round_pending != round_pending ||
        wire->replacement_pending != replacement_pending))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 reliable callback retired or replaced its source owner");
    for (uint32_t i = 0; ok && i < maximum; ++i) {
        if (!(recipients & (UINT64_C(1) << i))) continue;
        native_q3_wire_client *client = &wire->clients[i];
        if (!client->admitted || wire->client_revision[i] != revisions[i] ||
            !qa_actor_id_equal(client->actor, actors[i]) ||
            (!round_pending && !source_binding(wire, i, actors[i], error)))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 reliable callback changed its retained recipient");
    }
    --wire->calls;
    for (uint32_t i = 0; i < maximum; ++i) free(drop_reasons[i]);
    free(retained);
    return ok;
}

bool application_native_q3_send_command(application_provider *provider, int32_t slot,
    const char *text, qa_error *error)
{
    if (!text || slot < -1 || slot >= (int32_t)QA_Q3_SOURCE_CLIENTS || strlen(text) >= QA_Q3_COMMAND_CHARS)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 Source command has no authored client or text");
    return application_unified_q3_text(provider, APPLICATION_Q3_SOURCE_COMMAND, slot, text, error) &&
        send_command_transport(provider, slot, text, error);
}

bool application_native_q3_server_command(void *context, int32_t slot,
    const char *text, qa_error *error)
{
    return application_native_q3_send_command(context, slot, text, error);
}

static bool ui_configstring_publish(struct application_native_q3_wire *wire,
    uint32_t index, const char *text, qa_error *error)
{
    for (uint32_t slot = 0; slot < wire->max_clients; ++slot) {
        native_q3_wire_client *client = &wire->clients[slot];
        if (!wire->slot_leases[slot] || wire->slot_readers[slot] || !client->admitted ||
            !client->begun || client->bot || client->drop_pending || !client->gamestate) continue;
        if (!source_binding(wire, slot, client->actor, error) ||
            !qa_q3_configstring_set(client->gamestate, index, text, error)) return false;
    }
    return true;
}

bool application_native_q3_configstring_changed(void *context, uint32_t index,
    const char *text, qa_error *error)
{
    application_provider *provider = context;
    if (index >= QA_Q3_CONFIGSTRINGS || !text)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 committed configstring is invalid");
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    if (!wire) return false;
    qa_q3_game *game = provider->state.q3;
    uint64_t revision;
    if (!qa_q3_configstring_revision(game, index, &revision, error)) return false;
    char *retained = copy_text(text, error);
    if (!retained) return false;
    size_t length = strlen(retained), chunk = QA_Q3_COMMAND_CHARS - 25;
    char command[QA_Q3_COMMAND_CHARS];
    bool ok = application_unified_q3_configstring(provider, index, retained, error);
    size_t offset = 0;
    if (!ok) { free(retained); return false; }
    do {
        const char *current;
        uint64_t actual;
        if (wire_owner(provider, error) != wire || provider->state.q3 != game ||
            !qa_q3_configstring_revision(game, index, &actual, error) ||
            !qa_q3_configstring_read(game, index, &current, error)) {
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 configstring publication lost its source owner");
            break;
        }
        if (actual != revision) break;
        if (strcmp(current, retained)) {
            ok = application_fail(error, QA_ERROR_FORMAT, "Native Q3 configstring text differs from its authored mutation");
            break;
        }
        if (!offset && !ui_configstring_publish(wire, index, retained, error)) {
            ok = false;
            break;
        }
        size_t bytes = length - offset < chunk ? length - offset : chunk;
        const char *name = length <= chunk ? "cs" : !offset ? "bcs0" :
            offset + bytes == length ? "bcs2" : "bcs1";
        snprintf(command, sizeof(command), "%s %u \"%.*s\"", name, index, (int)bytes, retained + offset);
        ok = send_command_transport(provider, -1, command, error);
        if (!ok) break;
        if (wire_owner(provider, error) != wire || provider->state.q3 != game ||
            !qa_q3_configstring_revision(game, index, &actual, error) ||
            !qa_q3_configstring_read(game, index, &current, error)) {
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 configstring callback retired its source owner");
            break;
        }
        if (actual != revision) break;
        if (strcmp(current, retained)) {
            ok = application_fail(error, QA_ERROR_FORMAT, "Native Q3 configstring callback changed text without a source mutation");
            break;
        }
        offset += bytes;
    } while (offset < length);
    free(retained);
    return ok;
}

bool application_native_q3_bot_console(application_provider *provider, qa_actor_id actor,
    char *text, size_t capacity, bool *found, qa_error *error)
{
    uint32_t slot;
    if (!provider || !text || !capacity || !found ||
        !qa_q3_native_client_slot(provider->state.q3, actor, &slot, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 bot console has no admitted source client");
    struct application_native_q3_wire *wire;
    native_q3_wire_client *client = wire_client(provider, slot, &wire, error);
    if (!client) return false;
    if (!client->bot || client->drop_pending || !qa_actor_id_equal(client->actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 bot console names another engine client");
    *found = false;
    text[0] = 0;
    if (client->reliable.acknowledged == client->reliable.sequence) return true;
    int32_t next = client->reliable.acknowledged + 1;
    const char *command = qa_q3_reliable_lookup(&client->reliable, next);
    bool clamped;
    if (!qa_q3_reliable_ack(&client->reliable, QA_Q3_SERVER, next, &clamped, error)) return false;
    if (!command || !*command) return true;
    size_t bytes = strlen(command);
    if (bytes >= capacity) bytes = capacity - 1;
    memcpy(text, command, bytes);
    text[bytes] = 0;
    *found = true;
    return true;
}

static bool installed_builtin_source(application_provider *provider)
{
    qa_application *app = provider->application;
    const qa_launch_snapshot *candidate = qa_application_launch(app);
    if (app->operation != APPLICATION_CONFIGURING ||
        !qa_application_startup_publication_cleanup(app, candidate))
        return provider == application_world_provider(app, QA_ROLE_ENTITIES, "");
    const qa_launch_snapshot *previous = qa_application_startup_publication_previous(app, candidate);
    const qa_launch_binding *binding = qa_launch_binding_for(qa_launch_snapshot_choices(previous),
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "");
    return app->players && app->players->map_provider == provider && binding &&
        provider->launch && !strcmp(binding->instance, provider->launch->selection.instance);
}

static native_q3_wire_client *leased_client(application_native_q3_wire_client_lease *lease,
    qa_error *error)
{
    if (!lease || !lease->wire || lease->wire->round_pending) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 cgame client source is suspended");
        return NULL;
    }
    struct application_native_q3_wire *wire;
    native_q3_wire_client *client = wire_client(lease->wire->provider, lease->slot, &wire, error);
    if (!client) return NULL;
    if (wire != lease->wire || !client->begun || client->bot || client->drop_pending ||
        client->seat != lease->seat) {
        application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 cgame lease belongs to another source seat");
        return NULL;
    }
    if (lease->builtin) {
        application_provider *provider = wire->provider;
        qa_application *app = provider->application;
        qa_actor_id physical;
        qa_q3_source_binding binding;
        qa_q3_native_client source;
        if (!provider->constructed || !provider->attached || !provider->map_bound ||
            !installed_builtin_source(provider) ||
            provider->state.q3 != lease->game ||
            application_native_q3_console_registry(provider) != lease->registry ||
            app->publication_generation != lease->publication_generation ||
            app->map_revision != lease->map_revision ||
            !qa_application_player_actor(app, lease->seat, &physical) ||
            !qa_actor_id_equal(physical, client->actor) ||
            !qa_q3_source_binding_read(lease->game, lease->slot, &binding, error) ||
            !binding.in_use || !binding.body_attached ||
            !qa_q3_client_slot_read(lease->game, lease->slot, &source, error) ||
            source.rule.connected != QA_Q3_CLIENT_CONNECTED) {
            application_fail(error, QA_ERROR_ARGUMENT, "Native CGAME reader lost its installed source lifetime");
            return NULL;
        }
    }
    return client;
}

static const qa_q3_gamestate *leased_gamestate(void *context)
{
    native_q3_wire_client *client = leased_client(context, NULL);
    return client ? client->gamestate : NULL;
}

static bool leased_current_snapshot(void *context, int32_t *number, int32_t *time,
    qa_error *error)
{
    native_q3_wire_client *client = leased_client(context, error);
    if (!client) return false;
    if (!number || !time)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q3 current snapshot output");
    *number = client->has_snapshot ? client->snapshot_sequence : 0;
    *time = client->has_snapshot ? client->snapshots[
        (uint32_t)client->snapshot_sequence & (QA_Q3_PACKET_BACKUP - 1)].value.server_time : 0;
    return true;
}

static bool leased_snapshot(void *context, int32_t number, const qa_q3_snapshot **out,
    int32_t *ping, qa_error *error)
{
    native_q3_wire_client *client = leased_client(context, error);
    if (!client) return false;
    if (!out || !ping || number > client->snapshot_sequence)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 snapshot request is absent or in the future");
    *out = NULL;
    *ping = 0;
    if (number < 0 || !client->has_snapshot ||
        (int64_t)client->snapshot_sequence - number >= QA_Q3_PACKET_BACKUP) return true;
    const native_q3_wire_snapshot *snapshot = &client->snapshots[
        (uint32_t)number & (QA_Q3_PACKET_BACKUP - 1)];
    if (!snapshot->value.valid || snapshot->value.message_number != number) return true;
    const qa_q3_snapshot *latest = &client->snapshots[
        (uint32_t)client->snapshot_sequence & (QA_Q3_PACKET_BACKUP - 1)].value;
    uint32_t distance_bits = (uint32_t)latest->parse_entities_number +
        (uint32_t)latest->entity_count - (uint32_t)snapshot->value.parse_entities_number;
    int32_t distance;
    memcpy(&distance, &distance_bits, sizeof(distance));
    if (distance >= 2048) return true;
    *out = &snapshot->value;
    *ping = snapshot->ping;
    return true;
}

static bool leased_effect(application_native_q3_wire_client_lease *lease,
    qa_application_q3_client_effect effect, const char *text, qa_error *error)
{
    native_q3_wire_client *client = leased_client(lease, error);
    if (!client) return false;
    struct application_native_q3_wire *wire = lease->wire;
    qa_application *app = wire->provider->application;
    if (!text || !app->q3_client_effect || lease->calls == SIZE_MAX || wire->calls == SIZE_MAX)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "Native Q3 client effect requires its retained frontend source owner");
    qa_actor_id actor = client->actor;
    uint64_t revision = wire->client_revision[lease->slot];
    qa_q3_game *game = wire->provider->state.q3;
    char *retained = copy_text(text, error);
    if (!retained) return false;
    text = retained;
    if (effect == QA_APPLICATION_Q3_SYSTEM_INFO) {
        char server[64];
        if (!qa_q3_info_value(text, "sv_serverid", server, sizeof(server), error)) {
            free(retained);
            return false;
        }
        char *end;
        long long value = strtoll(server, &end, 10);
        client->server_id = end == server ? 0 : value > INT32_MAX ? INT32_MAX :
                            value < INT32_MIN ? INT32_MIN : (int32_t)value;
    } else if (effect == QA_APPLICATION_Q3_MAP_RESTART) {
        /* Prediction history clears; the real accepted engine command remains
         * available to ClientBegin and the following source ClientThink. */
        memset(client->commands, 0, sizeof(client->commands));
    }
    ++lease->calls;
    ++wire->calls;
    bool ok = app->q3_client_effect(app->guest_context, app, lease->receiver,
                                   lease->seat, effect, text, error);
    if (ok && (wire_owner(wire->provider, error) != wire ||
        wire->provider->state.q3 != game || wire->client_revision[lease->slot] != revision ||
        !qa_actor_id_equal(wire->clients[lease->slot].actor, actor) ||
        leased_client(lease, error) != client))
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client effect replaced or retired its source admission");
    --wire->calls;
    --lease->calls;
    free(retained);
    return ok;
}

bool application_native_q3_wire_client_effect(application_native_q3_wire_client_lease *lease,
    qa_application_q3_client_effect effect, const char *text, qa_error *error)
{
    return leased_effect(lease, effect, text, error);
}

static bool leased_server_command(void *context, int32_t number, bool *present,
    qa_error *error)
{
    application_native_q3_wire_client_lease *lease = context;
    native_q3_wire_client *client = leased_client(lease, error);
    if (!client) return false;
    if (!present || number > client->reliable.sequence)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 server command is absent or in the future");
    *present = false;
    if (number <= 0) return true;
    if ((int64_t)client->reliable.sequence - number >= QA_Q3_RELIABLE)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q3 server command has left its retained history");
    const char *text = qa_q3_reliable_lookup(&client->reliable, number);
    if (!text)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q3 server command predates its source gamestate");
    qa_command_tokens tokens = {0};
    if (!qa_command_tokenize(text, QA_RULESET_Q3, false, &tokens, NULL, NULL, error)) return false;
    qa_command_tokens_free(lease->arguments);
    *lease->arguments = tokens;
    if (tokens.count && !strcmp(tokens.values[0], "disconnect")) {
        const char *reason = tokens.count >= 2 ? tokens.values[1] : "Server disconnected";
        return leased_effect(lease, QA_APPLICATION_Q3_DISCONNECT, reason, error);
    }
    if (tokens.count && (!strcmp(tokens.values[0], "bcs0") ||
        !strcmp(tokens.values[0], "bcs1") || !strcmp(tokens.values[0], "bcs2"))) {
        const char *fragment = tokens.count > 2 ? tokens.values[2] : "";
        if (!client->big_configstring) {
            client->big_configstring = calloc(NATIVE_Q3_BIG_INFO_CHARS, 1);
            if (!client->big_configstring)
                return application_fail(error, QA_ERROR_MEMORY, "Retaining native Q3 configstring continuation");
        }
        if (!strcmp(tokens.values[0], "bcs0")) {
            const char *index_text = tokens.count > 1 ? tokens.values[1] : "";
            snprintf(client->big_configstring, NATIVE_Q3_BIG_INFO_CHARS,
                     "cs %s \"%s", index_text, fragment);
            client->big_configstring_length = strlen(client->big_configstring);
        } else {
            size_t bytes = strlen(fragment);
            size_t quote = !strcmp(tokens.values[0], "bcs2") ? 1u : 0u;
            if (bytes + quote >= NATIVE_Q3_BIG_INFO_CHARS - client->big_configstring_length)
                return application_fail(error, QA_ERROR_FORMAT, "Native Q3 configstring continuation exceeds its source storage");
            memcpy(client->big_configstring + client->big_configstring_length,
                   fragment, bytes + 1);
            client->big_configstring_length += bytes;
        }
        if (!strcmp(tokens.values[0], "bcs2")) {
            client->big_configstring[client->big_configstring_length++] = '"';
            client->big_configstring[client->big_configstring_length] = 0;
            qa_command_tokens complete = {0};
            bool ok = qa_command_tokenize(client->big_configstring, QA_RULESET_Q3, false, &complete, NULL, NULL, error);
            if (!ok) return false;
            qa_command_tokens_free(lease->arguments);
            *lease->arguments = complete;
            text = client->big_configstring;
            *present = true;
        }
    } else *present = true;
    qa_command_tokens *args = lease->arguments;
    if (*present && args->count && !strcmp(args->values[0], "cs")) {
        if (!client->gamestate)
            return application_fail(error, QA_ERROR_FORMAT, "Native Q3 configstring command requires its client gamestate");
        int32_t index = source_integer(args->count > 1 ? args->values[1] : "");
        if (index < 0 || index >= QA_Q3_CONFIGSTRINGS)
            return application_fail(error, QA_ERROR_FORMAT, "Invalid native Q3 client configstring index");
        const char *value = args->count > 1 ? args->args_text + strlen(args->values[1]) +
            (args->count > 2 ? 1u : 0u) : "";
        bool changed = strcmp(qa_q3_configstring(client->gamestate, (unsigned)index), value) != 0;
        if (changed && !qa_q3_configstring_set(client->gamestate, (unsigned)index,
                                               value, error)) return false;
        client->config_commands[index] = number;
        /* SystemInfo may enter other command parsers. Preserve the immutable
         * reached command and restore its actual argv after that effect. */
        char *command_text = copy_text(text, error);
        if (!command_text) return false;
        bool okay = !(changed && index == 1) || leased_effect(lease, QA_APPLICATION_Q3_SYSTEM_INFO,
            qa_q3_configstring(client->gamestate, 1), error);
        qa_command_tokens restored = {0};
        if (okay) okay = qa_command_tokenize(command_text, QA_RULESET_Q3, false, &restored, NULL, NULL, error);
        free(command_text);
        if (!okay) return false;
        qa_command_tokens_free(lease->arguments);
        *lease->arguments = restored;
    }
    if (*present && args->count && !strcmp(args->values[0], "map_restart") &&
        !leased_effect(lease, QA_APPLICATION_Q3_MAP_RESTART, text, error)) return false;
    if (*present && args->count && !strcmp(args->values[0], "clientLevelShot") &&
        !leased_effect(lease, QA_APPLICATION_Q3_LEVEL_SHOT, text, error)) return false;
    if (client->gamestate && number > client->gamestate->command_sequence)
        client->gamestate->command_sequence = number;
    bool clamped;
    int32_t acknowledged = number > client->reliable.acknowledged ? number : client->reliable.acknowledged;
    if (!qa_q3_reliable_ack(&client->reliable, QA_Q3_SERVER, acknowledged, &clamped, error)) return false;
    if (number > client->consumed_server_command) client->consumed_server_command = number;
    return true;
}

static int32_t leased_current_command(void *context)
{
    native_q3_wire_client *client = leased_client(context, NULL);
    return client ? client->command_sequence : 0;
}

static bool leased_user_command(void *context, int32_t number, qa_q3_usercmd *out,
    bool *present, qa_error *error)
{
    native_q3_wire_client *client = leased_client(context, error);
    if (!client) return false;
    if (!out || !present || number > client->command_sequence)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 user command is absent or in the future");
    *present = (int64_t)client->command_sequence - number < NATIVE_Q3_COMMAND_BACKUP;
    if (*present) *out = client->commands[(uint32_t)number & (NATIVE_Q3_COMMAND_BACKUP - 1)];
    return true;
}

static bool leased_command_values(void *context, int32_t weapon, float sensitivity,
    qa_error *error)
{
    native_q3_wire_client *client = leased_client(context, error);
    if (!client) return false;
    if (!isfinite(sensitivity))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 command sensitivity is invalid");
    client->weapon = weapon;
    client->sensitivity = sensitivity;
    return true;
}

static bool leased_source_actor(void *context, uint32_t number, qa_actor_id *out,
    bool *present, qa_error *error)
{
    application_native_q3_wire_client_lease *lease = context;
    if (!leased_client(lease, error)) return false;
    if (!out || !present)
        return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q3 source actor output");
    *out = (qa_actor_id){0};
    *present = false;
    if (number >= QA_Q3_SOURCE_ENTITIES) return true;
    native_q3_wire_client *client = &lease->wire->clients[lease->slot];
    if (client->source_actors[number].registry) {
        *out = client->source_actors[number];
        *present = qa_actors_get(qa_session_actors(lease->wire->provider->application->session), *out) != NULL;
        return true;
    }
    uint32_t count;
    qa_q3_game *game = lease->wire->provider->state.q3;
    if (!qa_q3_source_entity_count(game, &count, error)) return false;
    if (number >= count) return true;
    qa_q3_source_binding binding;
    if (!qa_q3_source_binding_read(game, number, &binding, error)) return false;
    if (binding.actor.registry && qa_actors_get(qa_session_actors(
            lease->wire->provider->application->session), binding.actor)) {
        *out = binding.actor;
        *present = true;
    }
    return true;
}

static application_native_q3_wire_client_lease *client_lease_create(
    struct application_native_q3_wire *wire, qa_actor_owner receiver,
    uint32_t seat, uint32_t slot, qa_command_tokens *arguments, bool cgame,
    qa_error *error)
{
    application_native_q3_wire_client_lease *lease = calloc(1, sizeof(*lease));
    if (!lease) {
        application_fail(error, QA_ERROR_MEMORY, "Retaining native GAME client service lease");
        return NULL;
    }
    *lease = (application_native_q3_wire_client_lease){.wire = wire, .receiver = receiver,
        .seat = seat, .slot = slot, .arguments = arguments, .cgame = cgame};
    ++wire->client_leases;
    ++wire->slot_leases[slot];
    wire->lease_seats[slot] = seat;
    if (cgame) ++wire->slot_readers[slot];
    return lease;
}

bool application_native_q3_wire_client_bind(application_provider *provider,
    qa_actor_owner receiver, uint32_t seat, uint32_t slot, qa_command_tokens *arguments,
    application_native_q3_wire_client_lease **out, qa_q3_host_options *options, qa_error *error)
{
    struct application_native_q3_wire *wire = provider ? provider->native_q3_wire : NULL;
    if (!wire || wire->provider != provider || wire->closing ||
        provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !provider->application || provider->application->destroy_requested || provider->close_pending ||
        !receiver || seat == UINT32_MAX ||
        slot >= QA_Q3_SOURCE_CLIENTS || (!wire->restore_pending && slot >= wire->max_clients) ||
        !arguments || !out || *out || !options ||
        options->client.gamestate || options->owner != receiver ||
        (options->role != QA_QVM_CGAME && options->role != QA_QVM_UI) ||
        wire->client_leases == SIZE_MAX ||
        (wire->slot_leases[slot] && wire->lease_seats[slot] != seat) ||
        (wire->clients[slot].admitted &&
         (wire->clients[slot].bot || wire->clients[slot].seat != seat)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client binding has no unique retained source seat");
    application_native_q3_wire_client_lease *lease = client_lease_create(wire, receiver,
        seat, slot, arguments, options->role == QA_QVM_CGAME, error);
    if (!lease) return false;
    options->client = (qa_q3_host_client_services){.context = lease, .gamestate = leased_gamestate,
        .current_snapshot = leased_current_snapshot, .snapshot = leased_snapshot,
        .server_command = leased_server_command, .current_command = leased_current_command,
        .user_command = leased_user_command, .command_values = leased_command_values,
        .source_actor = leased_source_actor};
    *out = lease;
    return true;
}

bool application_native_q3_wire_client_unbind(application_native_q3_wire_client_lease **owned,
    qa_error *error)
{
    if (!owned) return application_fail(error, QA_ERROR_ARGUMENT, "Missing native Q3 client lease owner");
    application_native_q3_wire_client_lease *lease = *owned;
    if (!lease) return true;
    if (!lease->wire || !lease->wire->client_leases ||
        !lease->wire->slot_leases[lease->slot] || lease->calls ||
        (lease->cgame && !lease->wire->slot_readers[lease->slot]))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client lease is still executing its source callback");
    --lease->wire->client_leases;
    --lease->wire->slot_leases[lease->slot];
    if (!lease->wire->slot_leases[lease->slot]) lease->wire->lease_seats[lease->slot] = UINT32_MAX;
    if (lease->cgame) --lease->wire->slot_readers[lease->slot];
    if (lease->builtin) qa_command_tokens_free(&lease->owned_arguments);
    *owned = NULL;
    free(lease);
    return true;
}

bool application_native_q3_wire_client_arguments(application_native_q3_wire_client_lease *lease,
    qa_native_host_command_view *out, qa_error *error)
{
    if (!lease || !lease->wire || !lease->arguments || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client command view has no retained role owner");
    qa_command_tokens *args = lease->arguments;
    *out = (qa_native_host_command_view){.count = args->count,
        .arguments = (const char *const *)args->values,
        .tail = args->args_text ? args->args_text : ""};
    return true;
}

bool application_native_q3_wire_client_topology_read(application_native_q3_wire_client_lease *lease,
    application_native_q3_wire_client_topology *out, qa_error *error)
{
    struct application_native_q3_wire *wire = lease ? lease->wire : NULL;
    application_provider *source = wire ? wire->provider : NULL;
    if (!source || !out || source->native_q3_wire != wire || wire->closing ||
        source->kind != APPLICATION_PROVIDER_Q3 || !source->state.q3 ||
        !source->application || source->application->destroy_requested ||
        !lease->receiver || !lease->arguments || lease->slot >= QA_Q3_SOURCE_CLIENTS ||
        !wire->slot_leases[lease->slot] || wire->lease_seats[lease->slot] != lease->seat)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 role has no retained source topology");
    *out = (application_native_q3_wire_client_topology){.source = source,
        .source_owner = source->owner, .receiver = lease->receiver,
        .seat = lease->seat, .source_slot = lease->slot};
    return true;
}

bool application_native_q3_wire_client_time(application_native_q3_wire_client_lease *lease,
    int32_t *out, qa_error *error)
{
    return leased_client(lease, error) && application_q3_wire_time(lease->wire->provider, out, error);
}

bool application_native_q3_wire_client_command(application_native_q3_wire_client_lease *lease,
    const char *text, qa_error *error)
{
    native_q3_wire_client *client = leased_client(lease, error);
    if (!client) return false;
    if (!text || lease->calls == SIZE_MAX || lease->wire->calls == SIZE_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 client command has no retained source lease");
    qa_actor_id actor = client->actor;
    ++lease->calls;
    ++lease->wire->calls;
    bool ok = application_native_q3_client_text(lease->wire->provider, actor, text, error);
    --lease->wire->calls;
    --lease->calls;
    return ok;
}

bool qa_native_q3_wire_reader_acquire(qa_application *app, qa_actor_owner receiver,
    uint32_t seat, uint32_t slot, qa_actor_id actor, qa_native_q3_wire_reader **out,
    qa_error *error)
{
    application_provider *provider = app ? application_world_provider(app, QA_ROLE_ENTITIES, "") : NULL;
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    qa_actor_id local;
    qa_q3_source_binding binding;
    qa_q3_native_client source;
    if (!wire || !receiver || !out || *out || seat == UINT32_MAX ||
        !provider->constructed || !provider->attached || !provider->map_bound ||
        !application_native_q3_console_registry(provider) ||
        wire->round_pending || slot >= wire->max_clients || wire->client_leases == SIZE_MAX ||
        wire->slot_readers[slot] ||
        (wire->slot_leases[slot] && wire->lease_seats[slot] != seat) ||
        !qa_application_player_actor(app, seat, &local) || !qa_actor_id_equal(local, actor) ||
        !source_binding(wire, slot, actor, error) ||
        !qa_q3_source_binding_read(provider->state.q3, slot, &binding, error) ||
        !binding.in_use || !binding.body_attached ||
        !qa_q3_client_slot_read(provider->state.q3, slot, &source, error) ||
        source.rule.connected != QA_Q3_CLIENT_CONNECTED ||
        !wire->clients[slot].admitted || !wire->clients[slot].begun ||
        wire->clients[slot].bot || wire->clients[slot].drop_pending || wire->clients[slot].seat != seat)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native reader requires its installed sole local GAME client");
    application_native_q3_wire_client_lease *lease = client_lease_create(wire, receiver,
        seat, slot, NULL, true, error);
    if (!lease) return false;
    lease->builtin = true;
    lease->arguments = &lease->owned_arguments;
    lease->game = provider->state.q3;
    lease->registry = application_native_q3_console_registry(provider);
    lease->publication_generation = app->publication_generation;
    lease->map_revision = app->map_revision;
    *out = lease;
    /* This new local reader requests the real GAME publication. Receive it
     * before CG preparation reads SystemInfo, using the same completed-source
     * producer as ordinary frames. Retain the lease for cleanup on failure. */
    if (!wire->clients[slot].gamestate &&
        !application_q3_publish_local_snapshots(app, error)) return false;
    if (!qa_native_q3_wire_reader_current(lease))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Native reader acquisition lost its source during initial publication");
    /* CG_Init starts at the real CLIENT's last executed command. A new CGAME
     * owner can follow a seat reindex while the CLIENT and gamestate survive. */
    lease->initial_command_sequence = wire->clients[slot].consumed_server_command;
    return true;
}

bool qa_native_q3_wire_reader_destroy(qa_native_q3_wire_reader **owned, qa_error *error)
{
    if (!owned || (*owned && !(*owned)->builtin))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native reader release requires its genuine owned lease");
    return application_native_q3_wire_client_unbind(owned, error);
}
bool qa_native_q3_wire_reader_current(const qa_native_q3_wire_reader *reader)
{ return reader && reader->builtin && leased_client((qa_native_q3_wire_reader *)reader, NULL); }
bool qa_native_q3_wire_reader_idle(const qa_native_q3_wire_reader *reader)
{ return reader && reader->builtin && !reader->calls && reader->wire && !reader->wire->calls; }

bool qa_native_q3_wire_reader_basis(const qa_native_q3_wire_reader *reader,
    qa_native_q3_wire_basis *out, qa_error *error)
{
    if (!out || !qa_native_q3_wire_reader_current(reader))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native reader has no current installed source basis");
    application_provider *provider = reader->wire->provider;
    qa_q3_product product; int32_t match_start;
    if (!qa_q3_source_match_context_read(provider->state.q3, &product, &match_start, error)) return false;
    *out = (qa_native_q3_wire_basis){.application = provider->application,
        .session = provider->application->session, .source_game = reader->game,
        .source_cvars = reader->registry, .source_owner = provider->owner,
        .receiver = reader->receiver, .actor = reader->wire->clients[reader->slot].actor, .product = product,
        .seat = reader->seat, .physical_client = reader->slot,
        .publication_generation = reader->publication_generation, .map_revision = reader->map_revision};
    return true;
}
bool qa_native_q3_wire_reader_publication(const qa_native_q3_wire_reader *reader,
    qa_native_q3_wire_publication *out, qa_error *error)
{
    if (!out || !qa_native_q3_wire_reader_current(reader))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native reader publication has no current source");
    const native_q3_wire_client *client = &reader->wire->clients[reader->slot];
    *out = (qa_native_q3_wire_publication){.initial_command_sequence = reader->initial_command_sequence,
        .latest_command_sequence = client->reliable.sequence,
        .reached_command_sequence = client->consumed_server_command,
        .snapshot_number = client->has_snapshot ? client->snapshot_sequence : 0,
        .user_command_number = client->command_sequence,
        .has_gamestate = client->gamestate != NULL, .has_snapshot = client->has_snapshot};
    if (client->has_snapshot) out->snapshot_time = client->snapshots[
        (uint32_t)client->snapshot_sequence & (QA_Q3_PACKET_BACKUP - 1)].value.server_time;
    return true;
}
bool qa_native_q3_wire_reader_command(qa_native_q3_wire_reader *reader, int32_t sequence,
    qa_native_q3_wire_receipt *out, qa_error *error)
{
    if (!out || !qa_native_q3_wire_reader_current(reader) || reader->calls ||
        reader->wire->calls == SIZE_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native reliable claim requires its idle actual reader");
    native_q3_wire_client *client = &reader->wire->clients[reader->slot];
    if (!client->gamestate || client->consumed_server_command == INT32_MAX ||
        sequence != client->consumed_server_command + 1 || sequence > client->reliable.sequence)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native reliable claim must reach the next received command");
    if ((int64_t)client->reliable.sequence - sequence >= QA_Q3_RELIABLE ||
        !qa_q3_reliable_lookup(&client->reliable, sequence))
        return application_fail(error, QA_ERROR_FORMAT, "Native reliable claim has left its retained command history");
    *out = (qa_native_q3_wire_receipt){0};
    reader->has_receipt = false;
    /* LocalQ3ClientState claims lastExecuted before CL_GetServerCommand effects.
     * An entered failing source effect is not replayed by this reader. */
    client->consumed_server_command = sequence;
    ++reader->calls; ++reader->wire->calls;
    bool present = false;
    bool okay = leased_server_command(reader, sequence, &present, error);
    if (okay && !qa_native_q3_wire_reader_current(reader))
        okay = application_fail(error, QA_ERROR_ARGUMENT, "Native reliable claim retired its physical source");
    if (okay) {
        reader->receipt_sequence = sequence;
        reader->receipt_present = present;
        reader->has_receipt = true;
        *out = (qa_native_q3_wire_receipt){.reader = reader, .actor = reader->wire->clients[reader->slot].actor,
            .publication_generation = reader->publication_generation, .map_revision = reader->map_revision,
            .sequence = sequence, .present = present, .arguments = reader->arguments};
    }
    --reader->wire->calls; --reader->calls;
    return okay;
}
bool qa_native_q3_wire_receipt_current(const qa_native_q3_wire_receipt *receipt)
{
    const qa_native_q3_wire_reader *reader = receipt ? receipt->reader : NULL;
    return qa_native_q3_wire_reader_current(reader) && reader->has_receipt &&
        receipt->present == reader->receipt_present && receipt->sequence == reader->receipt_sequence &&
        receipt->arguments == reader->arguments &&
        receipt->publication_generation == reader->publication_generation &&
        receipt->map_revision == reader->map_revision && qa_actor_id_equal(receipt->actor, reader->wire->clients[reader->slot].actor) &&
        receipt->sequence == reader->wire->clients[reader->slot].consumed_server_command;
}
bool qa_native_q3_wire_reader_configstring(const qa_native_q3_wire_reader *reader,
    uint32_t index, const char **text, uint64_t *revision, qa_error *error)
{
    if (!text || !revision || index >= QA_Q3_CONFIGSTRINGS || !qa_native_q3_wire_reader_current(reader))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native reached configstring has no current reader/index");
    const native_q3_wire_client *client = &reader->wire->clients[reader->slot];
    if (!client->gamestate)
        return application_fail(error, QA_ERROR_NOT_FOUND, "Native reader has not received its real gamestate baseline");
    *text = qa_q3_configstring(client->gamestate, index);
    *revision = (uint32_t)client->config_commands[index];
    return true;
}
bool qa_native_q3_wire_reader_snapshot(qa_native_q3_wire_reader *reader, int32_t number,
    const qa_q3_snapshot **out, int32_t *ping, qa_error *error)
{ return reader && reader->builtin ? leased_snapshot(reader, number, out, ping, error) :
    application_fail(error, QA_ERROR_ARGUMENT, "Snapshot read requires its native reader"); }
bool qa_native_q3_wire_reader_user_command(qa_native_q3_wire_reader *reader, int32_t number,
    qa_q3_usercmd *out, bool *present, qa_error *error)
{ return reader && reader->builtin ? leased_user_command(reader, number, out, present, error) :
    application_fail(error, QA_ERROR_ARGUMENT, "User command read requires its native reader"); }
bool qa_native_q3_wire_reader_command_values(qa_native_q3_wire_reader *reader, int32_t weapon,
    float sensitivity, qa_error *error)
{ return reader && reader->builtin ? leased_command_values(reader, weapon, sensitivity, error) :
    application_fail(error, QA_ERROR_ARGUMENT, "Command values require their native reader"); }
bool application_native_q3_input_values_read(application_provider *provider,uint32_t seat,
    qa_actor_id actor,uint8_t *weapon,float *sensitivity,qa_error *error)
{
    uint32_t slot;
    if (!provider->constructed || !provider->attached || !provider->map_bound ||
        !qa_q3_native_client_slot(provider->state.q3,actor,&slot,error))
        return application_fail(error,QA_ERROR_ARGUMENT,"Q3 command selection lost its actual GAME source");
    struct application_native_q3_wire *wire;
    native_q3_wire_client *client=wire_client(provider,slot,&wire,error);
    if (!client) return false;
    if (wire->calls || wire->round_pending || !client->begun || client->bot || client->drop_pending ||
        client->seat!=seat || !qa_actor_id_equal(client->actor,actor))
        return application_fail(error,QA_ERROR_ARGUMENT,"Q3 command selection lost its current physical CLIENT");
    *weapon=(uint8_t)client->weapon; *sensitivity=client->sensitivity;
    return true;
}
bool qa_native_q3_wire_reader_actor(qa_native_q3_wire_reader *reader, uint32_t number,
    qa_actor_id *out, bool *present, qa_error *error)
{ return reader && reader->builtin ? leased_source_actor(reader, number, out, present, error) :
    application_fail(error, QA_ERROR_ARGUMENT, "Source actor read requires its native reader"); }
bool qa_native_q3_wire_reader_reliable(qa_native_q3_wire_reader *reader, const char *text, qa_error *error)
{ return reader && reader->builtin ? application_native_q3_wire_client_command(reader, text, error) :
    application_fail(error, QA_ERROR_ARGUMENT, "Reliable submission requires its native reader"); }
bool qa_native_q3_wire_reader_effect(qa_native_q3_wire_reader *reader,
    qa_application_q3_client_effect effect, const char *text, qa_error *error)
{ return reader && reader->builtin ? leased_effect(reader, effect, text, error) :
    application_fail(error, QA_ERROR_ARGUMENT, "Client effect requires its native reader"); }

static bool fields_failure(qa_source_save_io *io, const char *message)
{
    io->failed = true;
    return application_fail(io->error, QA_ERROR_FORMAT, message);
}

static bool text_fields(qa_source_save_io *io, char **text)
{
    bool present = *text != NULL;
    if (!qa_source_save_bool(io, &present) || !present) return !io->failed;
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*text) + 1 : 0;
    if (!qa_source_save_count(io, &length, SIZE_MAX) || !length)
        return fields_failure(io, "Native Q3 saved userinfo extent is invalid");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (io->offset > io->input.size || length > io->input.size - io->offset)
            return fields_failure(io, "Native Q3 saved userinfo is truncated");
        *text = malloc(length);
        if (!*text) {
            io->failed = true;
            return application_fail(io->error, QA_ERROR_MEMORY, "Restoring native Q3 engine userinfo");
        }
    }
    return qa_source_save_bytes(io, *text, length) &&
        ((!(*text)[length - 1] && !memchr(*text, 0, length - 1)) ||
         fields_failure(io, "Native Q3 saved userinfo terminator is invalid"));
}

static bool fixed_text_fields(qa_source_save_io *io, char *text, size_t capacity)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t length = 0;
    if (!reading) {
        const char *end = memchr(text, 0, capacity);
        if (!end) return fields_failure(io, "Native Q3 retained text has no terminator");
        length = (size_t)(end - text);
    }
    if (!qa_source_save_count(io, &length, capacity - 1) ||
        !qa_source_save_bytes(io, text, length)) return false;
    if (memchr(text, 0, length))
        return fields_failure(io, "Native Q3 retained text has an embedded terminator");
    if (reading) memset(text + length, 0, capacity - length);
    return true;
}

static bool write_record(void *context, size_t offset, qa_bytes bytes, qa_error *error)
{
    (void)error;
    memcpy((uint8_t *)context + offset, bytes.data, bytes.size);
    return true;
}
static bool reader_arguments_fields(qa_source_save_io *io,qa_command_tokens *arguments)
{
    const size_t maximum=9216;
    bool allocated=arguments->storage!=NULL;
    if (io->direction==QA_SOURCE_SAVE_WRITE &&
        ((arguments->values!=NULL)!=allocated || (arguments->args_text!=NULL)!=allocated ||
         (!allocated && arguments->count)))
        return fields_failure(io,"Native reader argument allocation is inconsistent");
    if (!qa_source_save_bool(io,&allocated) || !qa_source_save_count(io,&arguments->count,1024)) return false;
    if (!allocated) return !arguments->count || fields_failure(io,"Native reader arguments have no owned storage");
    size_t extent=0;
    if (io->direction==QA_SOURCE_SAVE_WRITE) {
        for (size_t i=0;i<arguments->count;++i) {
            if (arguments->values[i]!=arguments->storage+extent)
                return fields_failure(io,"Native reader argument view differs from its packed owner");
            size_t length=strlen(arguments->values[i])+1;
            if (length>maximum-extent) return fields_failure(io,"Native reader argument storage exceeds source token extent");
            extent+=length;
        }
    }
    if (!qa_source_save_count(io,&extent,maximum)) return false;
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (io->offset>io->input.size || extent>io->input.size-io->offset)
            return fields_failure(io,"Native reader argument storage is truncated");
        arguments->storage=calloc(extent?extent:1,1);
        arguments->values=calloc(arguments->count?arguments->count:1,sizeof(*arguments->values));
        if (!arguments->storage || !arguments->values) {
            io->failed=true; return application_fail(io->error,QA_ERROR_MEMORY,"Restoring native reader argument storage");
        }
    }
    if (!qa_source_save_bytes(io,arguments->storage,extent)) return false;
    size_t offset=0;
    for (size_t i=0;i<arguments->count;++i) {
        if (offset>=extent) return fields_failure(io,"Native reader argument table exceeds its storage");
        char *end=memchr(arguments->storage+offset,0,extent-offset);
        if (!end) return fields_failure(io,"Native reader argument has no terminator");
        if (io->direction==QA_SOURCE_SAVE_READ) arguments->values[i]=arguments->storage+offset;
        offset=(size_t)(end-arguments->storage)+1;
    }
    if (offset!=extent) return fields_failure(io,"Native reader argument storage retains an unproduced token tail");
    size_t length=io->direction==QA_SOURCE_SAVE_WRITE?strlen(arguments->args_text)+1:0;
    if (!qa_source_save_count(io,&length,maximum+1) || !length)
        return fields_failure(io,"Native reader args text extent is invalid");
    if (io->direction==QA_SOURCE_SAVE_READ) {
        if (io->offset>io->input.size || length>io->input.size-io->offset)
            return fields_failure(io,"Native reader args text is truncated");
        arguments->args_text=malloc(length);
        if (!arguments->args_text) {
            io->failed=true; return application_fail(io->error,QA_ERROR_MEMORY,"Restoring native reader args text");
        }
    }
    if (!qa_source_save_bytes(io,arguments->args_text,length) || arguments->args_text[length-1] ||
        memchr(arguments->args_text,0,length-1)) return fields_failure(io,"Native reader args text terminator is invalid");
    offset=0;
    for (size_t i=1;i<arguments->count;++i) {
        size_t bytes=strlen(arguments->values[i]);
        if (i>1 && (offset>=length-1 || arguments->args_text[offset++]!=' '))
            return fields_failure(io,"Native reader args text separator differs from its actual argv");
        if (bytes>length-1-offset || memcmp(arguments->args_text+offset,arguments->values[i],bytes))
            return fields_failure(io,"Native reader args text differs from its actual argv");
        offset+=bytes;
    }
    return offset==length-1 || fields_failure(io,"Native reader args text retains an unproduced tail");
}
static bool reader_fields(qa_source_save_io *io,const qa_native_q3_wire_basis *basis,
    qa_native_q3_wire_reader *continuation,const native_q3_wire_client *client)
{
    uint8_t magic[4]={'Q','3','W','R'}; uint32_t product=basis->product;
    qa_actor_owner source=basis->source_owner;
    uint64_t receiver=basis->receiver;
    uint32_t seat=basis->seat,slot=basis->physical_client;
    uint64_t publication=basis->publication_generation,map=basis->map_revision;
    qa_actor_id actor=basis->actor;
    if (!qa_source_save_bytes(io,magic,4) || memcmp(magic,"Q3WR",4) ||
        !qa_source_save_string(io,&source) || source!=basis->source_owner ||
        !qa_source_save_u64(io,&receiver) || receiver!=basis->receiver ||
        !qa_source_save_u32(io,&product) || product!=(uint32_t)basis->product ||
        !qa_source_save_u32(io,&seat) || seat!=basis->seat ||
        !qa_source_save_u32(io,&slot) || slot!=basis->physical_client ||
        !qa_source_save_u64(io,&publication) || publication!=basis->publication_generation ||
        !qa_source_save_u64(io,&map) || map!=basis->map_revision ||
        !qa_source_save_actor(io,&actor) || !qa_actor_id_equal(actor,basis->actor))
        return fields_failure(io,"Native reader continuation has a different installed source binding");
    if (!reader_arguments_fields(io,&continuation->owned_arguments) ||
        !qa_source_save_i32(io,&continuation->receipt_sequence) ||
        !qa_source_save_bool(io,&continuation->has_receipt) ||
        !qa_source_save_bool(io,&continuation->receipt_present)) return false;
    if (continuation->receipt_sequence<0 || continuation->receipt_sequence>client->reliable.sequence ||
        (continuation->has_receipt && (!continuation->receipt_sequence || !continuation->owned_arguments.storage)))
        return fields_failure(io,"Native reader receipt exceeds its actual reliable history");
    return true;
}
bool qa_native_q3_wire_reader_checkpoint(const qa_native_q3_wire_reader *reader,qa_buffer *out,qa_error *error)
{
    qa_native_q3_wire_basis basis;
    if (!out || out->data || out->size || !qa_native_q3_wire_reader_idle(reader) ||
        !qa_native_q3_wire_reader_basis(reader,&basis,error))
        return application_fail(error,QA_ERROR_ARGUMENT,"Native reader capture requires its idle installed owner");
    qa_native_q3_wire_reader *owner=(qa_native_q3_wire_reader *)reader;
    ++owner->calls; ++owner->wire->calls;
    qa_native_q3_wire_reader copy=*owner; qa_source_save_io io={0};
    bool okay=qa_source_save_writer(&io,basis.session,error) &&
        reader_fields(&io,&basis,&copy,&owner->wire->clients[owner->slot]) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io); --owner->wire->calls; --owner->calls;
    return okay;
}
bool qa_native_q3_wire_reader_restore(qa_native_q3_wire_reader *reader,qa_bytes bytes,qa_error *error)
{
    qa_native_q3_wire_basis basis;
    if (!qa_native_q3_wire_reader_idle(reader) || !qa_native_q3_wire_reader_basis(reader,&basis,error))
        return application_fail(error,QA_ERROR_ARGUMENT,"Native reader restore requires its idle imported source owner");
    ++reader->calls; ++reader->wire->calls;
    qa_native_q3_wire_reader candidate={0}; qa_source_save_io io={0};
    bool okay=qa_source_save_reader(&io,basis.session,bytes,error) &&
        reader_fields(&io,&basis,&candidate,&reader->wire->clients[reader->slot]) && qa_source_save_finish(&io,NULL);
    if (okay) {
        qa_command_tokens_free(&reader->owned_arguments);
        reader->owned_arguments=candidate.owned_arguments;
        reader->receipt_sequence=candidate.receipt_sequence;
        reader->has_receipt=candidate.has_receipt; reader->receipt_present=candidate.receipt_present;
        candidate.owned_arguments=(qa_command_tokens){0};
    }
    qa_command_tokens_free(&candidate.owned_arguments);
    qa_source_save_dispose(&io); --reader->wire->calls; --reader->calls;
    return okay;
}

static bool command_fields(qa_source_save_io *io, qa_q3_usercmd *command)
{
    uint8_t bytes[24] = {0};
    qa_q3_abi_record record = {.abi = QA_QVM_Q3_MODERN, .bytes = {bytes, sizeof(bytes)},
        .context = bytes, .write = write_record};
    return (io->direction != QA_SOURCE_SAVE_WRITE ||
            qa_q3_abi_write_usercmd(&record, 0, false, command, io->error)) &&
        qa_source_save_bytes(io, bytes, sizeof(bytes)) &&
        (io->direction != QA_SOURCE_SAVE_READ ||
         qa_q3_abi_read_usercmd(&record, 0, command, io->error));
}

static bool entity_fields(qa_source_save_io *io, qa_q3_entity *value)
{
    uint8_t bytes[208] = {0};
    qa_q3_abi_record record = {.abi = QA_QVM_Q3_MODERN, .bytes = {bytes, sizeof(bytes)},
        .context = bytes, .write = write_record};
    return (io->direction != QA_SOURCE_SAVE_WRITE ||
            qa_q3_abi_write_entity(&record, 0, true, value, io->error)) &&
        qa_source_save_bytes(io, bytes, sizeof(bytes)) &&
        (io->direction != QA_SOURCE_SAVE_READ ||
         qa_q3_abi_read_entity(&record, 0, true, value, io->error));
}

static bool player_fields(qa_source_save_io *io, qa_q3_player *value)
{
    uint8_t bytes[468] = {0};
    uint32_t product = value->product;
    qa_q3_abi_record record = {.abi = QA_QVM_Q3_MODERN, .bytes = {bytes, sizeof(bytes)},
        .context = bytes, .write = write_record};
    if (!qa_source_save_u32(io, &product) || product > QA_Q3_TEAM_ARENA)
        return fields_failure(io, "Native Q3 saved source player product is invalid");
    bool ok = (io->direction != QA_SOURCE_SAVE_WRITE ||
               qa_q3_abi_write_player(&record, 0, true, false, value, io->error)) &&
        qa_source_save_bytes(io, bytes, sizeof(bytes)) &&
        (io->direction != QA_SOURCE_SAVE_READ ||
         qa_q3_abi_read_player(&record, 0, true, value, io->error));
    if (ok && io->direction == QA_SOURCE_SAVE_READ) value->product = (qa_q3_product)product;
    return ok;
}

static bool gamestate_fields(qa_source_save_io *io, qa_q3_gamestate **owned)
{
    bool present = *owned != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (!present) return true;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        *owned = calloc(1, sizeof(**owned));
        if (!*owned) {
            io->failed = true;
            return application_fail(io->error, QA_ERROR_MEMORY, "Restoring native Q3 source gamestate");
        }
    }
    qa_q3_gamestate *value = *owned;
    if (!qa_source_save_i32(io, &value->command_sequence) ||
        !qa_source_save_i32(io, &value->client_number) ||
        !qa_source_save_i32(io, &value->checksum_feed) ||
        !qa_source_save_count(io, &value->string_bytes, QA_Q3_GAMESTATE_CHARS) ||
        !qa_source_save_bytes(io, value->strings, value->string_bytes)) return false;
    if (!value->string_bytes || value->strings[0])
        return fields_failure(io, "Native Q3 saved gamestate string storage is invalid");
    for (size_t i = 0; i < QA_Q3_CONFIGSTRINGS; ++i) {
        if (!qa_source_save_u16(io, &value->config_offsets[i])) return false;
        if (value->config_offsets[i] >= value->string_bytes ||
            !memchr(value->strings + value->config_offsets[i], 0,
                    value->string_bytes - value->config_offsets[i]))
            return fields_failure(io, "Native Q3 saved configstring view is invalid");
    }
    for (size_t i = 0; i < QA_Q3_ENTITIES; ++i)
        if (!qa_source_save_bool(io, &value->baseline_present[i]) ||
            (value->baseline_present[i] && (!entity_fields(io, &value->baselines[i]) ||
             value->baselines[i].number != (int32_t)i)))
            return fields_failure(io, "Native Q3 saved baseline has no physical source number");
    return true;
}

static bool snapshot_fields(qa_source_save_io *io, native_q3_wire_snapshot *slot,
    size_t ordinal)
{
    qa_q3_snapshot *value = &slot->value;
    if (!qa_source_save_bool(io, &value->valid)) return false;
    if (!value->valid) return true;
    if (!qa_source_save_i32(io, &value->message_number) ||
        !qa_source_save_i32(io, &value->server_time) ||
        !qa_source_save_i32(io, &value->delta_number) ||
        !qa_source_save_i32(io, &value->server_command_number) ||
        !qa_source_save_u64(io, &value->parse_entities_number) ||
        !qa_source_save_u8(io, &value->flags) || !qa_source_save_u8(io, &value->area_bytes) ||
        value->area_bytes > sizeof(value->area_mask) ||
        !qa_source_save_bytes(io, value->area_mask, value->area_bytes) ||
        !player_fields(io, &value->player) ||
        !qa_source_save_count(io, &value->entity_count, 256) ||
        !qa_source_save_i32(io, &slot->ping)) return false;
    if ((io->direction == QA_SOURCE_SAVE_WRITE && value->entity_count > slot->capacity) ||
        (value->valid && (value->message_number < 1 ||
         ((uint32_t)value->message_number & (QA_Q3_PACKET_BACKUP - 1)) != ordinal ||
         (value->flags != 0 && value->flags != 4) ||
         value->parse_entities_number > UINT32_MAX)))
        return fields_failure(io, "Native Q3 saved snapshot storage or ring identity is invalid");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        slot->capacity = value->entity_count;
        if (io->offset > io->input.size ||
            slot->capacity > (io->input.size - io->offset) / 208)
            return fields_failure(io, "Native Q3 saved snapshot entities are truncated");
        slot->entities = slot->capacity ? calloc(slot->capacity, sizeof(*slot->entities)) : NULL;
        if (slot->capacity && !slot->entities) {
            io->failed = true;
            return application_fail(io->error, QA_ERROR_MEMORY, "Restoring native Q3 snapshot entity allocation");
        }
        value->entities = slot->entities;
    } else if ((slot->capacity && !slot->entities) || value->entities != slot->entities) {
        return fields_failure(io, "Native Q3 saved snapshot entity allocation differs");
    }
    int32_t previous = -1;
    for (size_t i = 0; i < value->entity_count; ++i) {
        if (!entity_fields(io, &slot->entities[i])) return false;
        if (slot->entities[i].number <= previous || slot->entities[i].number >= QA_Q3_ENTITY_WORLD)
            return fields_failure(io, "Native Q3 saved snapshot entity order is invalid");
        previous = slot->entities[i].number;
    }
    return true;
}

static bool continued_string_fields(qa_source_save_io *io, native_q3_wire_client *client)
{
    bool present = client->big_configstring != NULL;
    if (!qa_source_save_count(io, &client->big_configstring_length, NATIVE_Q3_BIG_INFO_CHARS - 1) ||
        !qa_source_save_bool(io, &present)) return false;
    if (!present && client->big_configstring_length)
        return fields_failure(io, "Native Q3 saved configstring continuation has no source storage");
    if (!present) return true;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (io->offset > io->input.size ||
            client->big_configstring_length + 1 > io->input.size - io->offset)
            return fields_failure(io, "Native Q3 saved configstring continuation is truncated");
        client->big_configstring = calloc(NATIVE_Q3_BIG_INFO_CHARS, 1);
        if (!client->big_configstring) {
            io->failed = true;
            return application_fail(io->error, QA_ERROR_MEMORY, "Restoring native Q3 configstring continuation");
        }
    }
    if (!qa_source_save_bytes(io, client->big_configstring, client->big_configstring_length + 1)) return false;
    if (client->big_configstring[client->big_configstring_length] ||
        memchr(client->big_configstring, 0, client->big_configstring_length))
        return fields_failure(io, "Native Q3 saved configstring continuation is invalid");
    return true;
}

static bool actor_fields(qa_source_save_io *io, qa_actor_id *actor, bool checkpoint)
{
    if (checkpoint || io->direction == QA_SOURCE_SAVE_WRITE)
        return qa_source_save_actor(io, actor);
    bool present = false;
    qa_saved_actor_id saved = {0};
    if (!qa_source_save_bool(io, &present) || !qa_source_save_u64(io, &saved.generation) ||
        !qa_source_save_u32(io, &saved.slot)) return false;
    if (!present) {
        if (saved.generation || saved.slot)
            return fields_failure(io, "Absent native Q3 carry actor contains provenance");
        *actor = (qa_actor_id){0};
        return true;
    }
    if (!qa_actors_reference_saved(qa_session_actors(io->session), saved, false, actor, io->error)) {
        io->failed = true;
        return false;
    }
    return true;
}

static bool client_fields(qa_source_save_io *io, native_q3_wire_client *client, bool checkpoint)
{
    uint32_t failure_code = client->drop_failure.code;
    uint64_t failure_offset = client->drop_failure.offset;
    if (!actor_fields(io, &client->actor, checkpoint) || !qa_source_save_u32(io, &client->seat) ||
        !text_fields(io, &client->userinfo) || !qa_source_save_bool(io, &client->admitted) ||
        !text_fields(io, &client->drop_reason) ||
        !qa_source_save_bool(io, &client->drop_pending) ||
        !qa_source_save_bool(io, &client->drop_delivered) ||
        !qa_source_save_u32(io, &failure_code) ||
        !qa_source_save_u64(io, &failure_offset) ||
        !fixed_text_fields(io, client->drop_failure.message, sizeof(client->drop_failure.message)) ||
        !qa_source_save_bool(io, &client->begun) || !qa_source_save_bool(io, &client->bot) ||
        !qa_source_save_bool(io, &client->command_received) ||
        !command_fields(io, &client->command) ||
        !qa_source_save_i32(io, &client->command_sequence) ||
        !qa_source_save_i32(io, &client->snapshot_sequence) ||
        !qa_source_save_i32(io, &client->consumed_server_command) ||
        !qa_source_save_i32(io, &client->server_id) ||
        !qa_source_save_i32(io, &client->weapon) ||
        !qa_source_save_u64(io, &client->entered_ns) ||
        !qa_source_save_f32(io, &client->sensitivity) ||
        !qa_source_save_bool(io, &client->has_snapshot) ||
        !qa_source_save_i32(io, &client->reliable.sequence) ||
        !qa_source_save_i32(io, &client->reliable.acknowledged)) return false;
    if (failure_code > QA_ERROR_NOT_FOUND || failure_offset > SIZE_MAX ||
        !memchr(client->drop_failure.message, 0, sizeof(client->drop_failure.message)))
        return fields_failure(io, "Native Q3 saved DROP failure is invalid");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        client->drop_failure.code = (qa_status)failure_code;
        client->drop_failure.offset = (size_t)failure_offset;
    }
    for (size_t i = 0; i < NATIVE_Q3_COMMAND_BACKUP; ++i)
        if (!command_fields(io, &client->commands[i])) return false;
    if (!qa_source_save_bool(io, &client->bot_snapshot_ready) ||
        !qa_source_save_u32(io, &client->bot_entity_count) || client->bot_entity_count > 256 ||
        (!client->bot_snapshot_ready && client->bot_entity_count) ||
        (client->bot_snapshot_ready && (!client->admitted || !client->bot || !client->begun)))
        return fields_failure(io, "Native Q3 saved bot visibility has no actual source client");
    int32_t previous_bot = -1;
    for (uint32_t i = 0; i < client->bot_entity_count; ++i) {
        if (!qa_source_save_i32(io, &client->bot_entities[i])) return false;
        if (client->bot_entities[i] <= previous_bot || client->bot_entities[i] >= QA_Q3_ENTITY_WORLD)
            return fields_failure(io, "Native Q3 saved bot visibility has an invalid physical entity order");
        previous_bot = client->bot_entities[i];
    }
    size_t source_count = 0;
    if (io->direction == QA_SOURCE_SAVE_WRITE)
        for (size_t i = 0; i < QA_Q3_SOURCE_ENTITIES; ++i)
            if (client->source_actors[i].registry) ++source_count;
    if (!qa_source_save_count(io, &source_count, QA_Q3_SOURCE_ENTITIES)) return false;
    uint32_t previous_source = 0;
    for (size_t row = 0, cursor = 0; row < source_count; ++row) {
        uint32_t index = 0;
        if (io->direction == QA_SOURCE_SAVE_WRITE) {
            while (cursor < QA_Q3_SOURCE_ENTITIES && !client->source_actors[cursor].registry) ++cursor;
            index = (uint32_t)cursor++;
        }
        if (!qa_source_save_u32(io, &index) || index >= QA_Q3_SOURCE_ENTITIES ||
            (row && index <= previous_source) || !client->admitted ||
            !actor_fields(io, &client->source_actors[index], checkpoint) ||
            !client->source_actors[index].registry)
            return fields_failure(io, "Native Q3 saved source namespace has no engine admission");
        previous_source = index;
    }
    for (size_t i = 0; i < QA_Q3_RELIABLE; ++i)
        if (!fixed_text_fields(io, client->reliable.text[i], QA_Q3_COMMAND_CHARS)) return false;
    if (!gamestate_fields(io, &client->gamestate) ||
        !qa_source_save_i32(io, &client->initial_server_command) ||
        client->initial_server_command < 0 ||
        client->initial_server_command > client->reliable.sequence ||
        (!client->gamestate && client->initial_server_command) ||
        (client->gamestate && client->initial_server_command > client->gamestate->command_sequence))
        return fields_failure(io, "Native Q3 initial reliable baseline is inconsistent");
    for (size_t i = 0; i < QA_Q3_CONFIGSTRINGS; ++i)
        if (!qa_source_save_i32(io, &client->config_commands[i]) ||
            client->config_commands[i] < 0 || client->config_commands[i] > client->reliable.sequence ||
            (client->gamestate ? client->config_commands[i] < client->initial_server_command : client->config_commands[i] != 0))
            return fields_failure(io, "Native Q3 reached configstring sequence is inconsistent");
    if (!continued_string_fields(io, client)) return false;
    for (size_t i = 0; i < QA_Q3_PACKET_BACKUP; ++i)
        if (!snapshot_fields(io, &client->snapshots[i], i)) return false;
    if (client->command_sequence < 0 || client->reliable.sequence < 0 ||
        client->reliable.acknowledged < 0 || client->reliable.acknowledged > client->reliable.sequence ||
        ((int64_t)client->reliable.sequence - client->reliable.acknowledged > QA_Q3_RELIABLE &&
         (!client->drop_pending ||
          (int64_t)client->reliable.sequence - client->reliable.acknowledged != QA_Q3_RELIABLE + 1)) ||
        (client->admitted && (!client->actor.registry || !client->userinfo)) ||
        (!client->admitted && (client->actor.registry || client->begun ||
         client->bot || client->command_received || client->command_sequence || client->reliable.sequence ||
         client->reliable.acknowledged || client->seat != UINT32_MAX)) ||
        (client->bot && client->seat != UINT32_MAX))
        return fields_failure(io, "Native Q3 saved engine admission or sequence is invalid");
    if (!isfinite(client->sensitivity) || client->snapshot_sequence < 0 ||
        (client->drop_pending != (client->drop_reason != NULL)) ||
        (client->drop_delivered && !client->drop_pending) ||
        (client->drop_failure.code && !client->drop_delivered) ||
        client->consumed_server_command < 0 ||
        client->consumed_server_command > client->reliable.sequence ||
        (!client->admitted && (client->gamestate || client->has_snapshot ||
         client->snapshot_sequence || client->big_configstring ||
         client->drop_pending)) ||
        (client->has_snapshot && (!client->gamestate || !client->begun ||
         !client->snapshots[(uint32_t)client->snapshot_sequence &
             (QA_Q3_PACKET_BACKUP - 1)].value.valid)))
        return fields_failure(io, "Native Q3 saved local client continuation is invalid");
    for (size_t i = 0; i < QA_Q3_PACKET_BACKUP; ++i) {
        const qa_q3_snapshot *snapshot = &client->snapshots[i].value;
        if (snapshot->valid && (!client->has_snapshot ||
            snapshot->message_number > client->snapshot_sequence ||
            snapshot->server_command_number > client->reliable.sequence ||
            snapshot->server_command_number < 0))
            return fields_failure(io, "Native Q3 saved snapshot exceeds its retained source counters");
    }
    return true;
}

static bool wire_fields(qa_source_save_io *io, struct application_native_q3_wire *wire,
    qa_actor_owner source_owner, bool checkpoint)
{
    uint8_t magic[8] = {'Q','A','N','3','W','I','R',0};
    const uint8_t expected[8] = {'Q','A','N','3','W','I','R',0};
    uint32_t maximum = wire->max_clients;
    qa_actor_owner owner = source_owner;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, expected, sizeof(magic)) ||
        !qa_source_save_string(io, &owner) || owner != source_owner ||
        !qa_source_save_u32(io, &maximum) || maximum < 1 || maximum > QA_Q3_SOURCE_CLIENTS ||
        (io->direction == QA_SOURCE_SAVE_WRITE && maximum != wire->max_clients) ||
        !qa_source_save_u8(io, &wire->snapshot_bit) ||
        (wire->snapshot_bit != 0 && wire->snapshot_bit != 4))
        return fields_failure(io, "Native Q3 private wire record or source owner differs");
    if (io->direction == QA_SOURCE_SAVE_READ) wire->max_clients = maximum;
    for (uint32_t slot = 0; slot < QA_Q3_SOURCE_CLIENTS; ++slot) {
        native_q3_wire_client *client = &wire->clients[slot];
        /* Disconnect retains userinfo; the never-admitted constructor has no
         * client continuation. Rebuild that constructor instead of its arrays. */
        bool retained = io->direction == QA_SOURCE_SAVE_WRITE &&
            (client->admitted || client->userinfo != NULL);
        if (!qa_source_save_bool(io, &retained)) return false;
        if (!retained) continue;
        if (!client_fields(io, client, checkpoint)) return false;
        if (slot >= maximum && wire->clients[slot].admitted)
            return fields_failure(io, "Native Q3 saved client exceeds source capacity");
        if (wire->clients[slot].gamestate &&
            (wire->clients[slot].gamestate->client_number != (int32_t)slot ||
             wire->clients[slot].gamestate->command_sequence < 0 ||
             wire->clients[slot].gamestate->command_sequence > wire->clients[slot].reliable.sequence))
            return fields_failure(io, "Native Q3 saved gamestate has no exact admitted source client");
    }
    return true;
}

bool application_native_q3_wire_capture(application_provider *provider, qa_buffer *out,
    qa_error *error)
{
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    if (!wire) return false;
    if (!out || wire->calls || wire->round_pending ||
        !application_native_q3_wire_finish(provider, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 wire capture requires its idle complete source");
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, provider->application->session, error) &&
        wire_fields(&io, wire, provider->owner, true) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool application_native_q3_wire_restore(application_provider *provider, qa_bytes bytes,
    qa_error *error)
{
    struct application_native_q3_wire *wire = provider ? provider->native_q3_wire : NULL;
    if (!wire || !wire->restore_pending || wire->calls)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 wire restore requires an empty candidate owner");
    for (size_t i = 0; i < QA_Q3_SOURCE_CLIENTS; ++i)
        if (wire->clients[i].admitted || wire->clients[i].userinfo)
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 wire candidate was already restored");
    struct application_native_q3_wire *candidate = calloc(1, sizeof(*candidate));
    if (!candidate)
        return application_fail(error, QA_ERROR_MEMORY, "Restoring native Q3 private wire state");
    candidate->provider = provider;
    candidate->max_clients = wire->max_clients;
    clients_clear(candidate);
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, provider->application->session, bytes, error) &&
        wire_fields(&io, candidate, provider->owner, true) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok) {
        memcpy(wire->clients, candidate->clients, sizeof(wire->clients));
        wire->max_clients = candidate->max_clients;
        wire->snapshot_bit = candidate->snapshot_bit;
        memset(candidate->clients, 0, sizeof(candidate->clients));
    }
    clients_clear(candidate);
    free(candidate);
    return ok;
}

bool application_native_q3_wire_finish(application_provider *provider, qa_error *error)
{
    struct application_native_q3_wire *wire = provider ? provider->native_q3_wire : NULL;
    if (!wire || wire->provider != provider || wire->calls || wire->closing ||
        provider->kind != APPLICATION_PROVIDER_Q3 || !provider->state.q3 ||
        !provider->application || provider->application->destroy_requested || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 wire qualification has no idle source owner");
    uint32_t maximum;
    if (!qa_q3_source_max_clients(provider->state.q3, &maximum, error) || maximum != wire->max_clients)
        return application_fail(error, QA_ERROR_FORMAT, "Restored native Q3 wire capacity differs from GAME");
    for (uint32_t slot = 0; slot < maximum; ++slot) {
        native_q3_wire_client *client = &wire->clients[slot];
        qa_q3_native_client source;
        if (!qa_q3_client_slot_read(provider->state.q3, slot, &source, error)) return false;
        if (!client->admitted) {
            if (source.rule.connected != QA_Q3_CLIENT_DISCONNECTED)
                return application_fail(error, QA_ERROR_FORMAT, "Native Q3 connected source has no retained engine admission");
            continue;
        }
        if (!source_binding(wire, slot, client->actor, error))
            return application_fail(error, QA_ERROR_FORMAT, "Restored native Q3 engine client lost its physical actor generation");
        if (wire->slot_leases[slot] && (client->bot || client->seat != wire->lease_seats[slot]))
            return application_fail(error, QA_ERROR_FORMAT, "Restored native Q3 client role differs from its actual source seat");
        if (source.rule.connected == QA_Q3_CLIENT_DISCONNECTED ||
            (client->begun && source.rule.connected != QA_Q3_CLIENT_CONNECTED))
            return application_fail(error, QA_ERROR_FORMAT, "Restored native Q3 engine admission differs from source gclient");
    }
    for (uint32_t slot = maximum; slot < QA_Q3_SOURCE_CLIENTS; ++slot)
        if (wire->slot_leases[slot])
            return application_fail(error, QA_ERROR_FORMAT, "Restored native Q3 client role exceeds its actual GAME capacity");
    wire->restore_pending = false;
    return true;
}

bool application_native_q3_wire_carry_capture(application_provider *provider,
    application_native_q3_wire_carry **out, qa_error *error)
{
    if (!out || *out || !application_native_q3_wire_round_ready(provider, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 replacement carry requires its stable source and empty owner");
    application_native_q3_wire_carry *carry = calloc(1, sizeof(*carry));
    if (!carry)
        return application_fail(error, QA_ERROR_MEMORY, "Capturing native Q3 retained engine carry");
    carry->application = provider->application;
    carry->registry = qa_actors_identity(qa_session_actors(provider->application->session));
    carry->owner = provider->owner;
    if (!application_native_q3_wire_capture(provider, &carry->bytes, error)) {
        free(carry);
        return false;
    }
    *out = carry;
    return true;
}

static bool carry_apply(application_provider *provider,
    const application_native_q3_wire_carry *carry, bool refresh, qa_error *error)
{
    struct application_native_q3_wire *wire = provider ? provider->native_q3_wire : NULL;
    if (!wire || wire->provider != provider || !carry ||
        carry->application != provider->application || !carry->owner ||
        !carry->registry || carry->registry != qa_actors_identity(qa_session_actors(provider->application->session)) ||
        !carry->bytes.data || provider->attached || wire->calls || wire->closing ||
        provider->close_pending || provider->application->destroy_requested ||
        (refresh ? !wire->round_pending || !wire->replacement_pending || wire->restore_pending
                 : wire->round_pending))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 carry requires its detached unpublished candidate in the same application");
    for (size_t i = 0; i < QA_Q3_SOURCE_CLIENTS; ++i) {
        const native_q3_wire_client *client = &wire->clients[i];
        if (client->actor.registry || client->begun || client->gamestate || client->has_snapshot ||
            (!refresh && (client->admitted || client->userinfo)))
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q3 carry candidate has already admitted or published a source client");
    }
    struct application_native_q3_wire *candidate = calloc(1, sizeof(*candidate));
    if (!candidate)
        return application_fail(error, QA_ERROR_MEMORY, "Importing native Q3 retained engine carry");
    candidate->provider = provider;
    clients_clear(candidate);
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, provider->application->session,
        (qa_bytes){carry->bytes.data, carry->bytes.size}, error) &&
        wire_fields(&io, candidate, carry->owner, false) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    uint32_t maximum;
    if (ok) ok = qa_q3_source_max_clients(provider->state.q3, &maximum, error);
    for (uint32_t slot = 0; ok && slot < QA_Q3_SOURCE_CLIENTS; ++slot) {
        native_q3_wire_client *client = &candidate->clients[slot];
        if (wire->slot_leases[slot] && (slot >= maximum ||
            (client->admitted && (client->bot || client->seat != wire->lease_seats[slot])))) {
            ok = application_fail(error, QA_ERROR_ARGUMENT,
                                  "Native Q3 replacement carry differs from its genuine role topology");
            break;
        }
        if (!client->admitted) continue;
        if (slot >= maximum) {
            ok = application_fail(error, QA_ERROR_ARGUMENT,
                                  "Native Q3 replacement capacity excludes a retained physical client");
            break;
        }
        if (client->drop_pending) {
            ok = application_fail(error, QA_ERROR_ARGUMENT,
                                  "Native Q3 replacement carry requires actual old-source DROP completion");
            break;
        }
        client_world_clear(client);
    }
    if (ok) {
        clients_clear(wire);
        memcpy(wire->clients, candidate->clients, sizeof(wire->clients));
        memset(candidate->clients, 0, sizeof(candidate->clients));
        wire->max_clients = maximum;
        wire->snapshot_bit = candidate->snapshot_bit ^ 4;
        wire->round_pending = true;
        wire->replacement_pending = true;
        wire->restore_pending = false;
    }
    clients_clear(candidate);
    free(candidate);
    return ok;
}

bool application_native_q3_wire_carry_import(application_provider *provider,
    const application_native_q3_wire_carry *carry, qa_error *error)
{
    return carry_apply(provider, carry, false, error);
}

bool application_native_q3_wire_carry_refresh(application_provider *provider,
    const application_native_q3_wire_carry *carry, qa_error *error)
{
    return carry_apply(provider, carry, true, error);
}

void application_native_q3_wire_carry_dispose(application_native_q3_wire_carry *carry)
{
    if (!carry) return;
    qa_buffer_free(&carry->bytes);
    free(carry);
}

bool application_native_q3_wire_carry_finish(application_provider *provider, qa_error *error)
{
    struct application_native_q3_wire *wire = wire_owner(provider, error);
    if (!wire) return false;
    if (!wire->round_pending || !wire->replacement_pending || wire->calls ||
        !application_native_q3_wire_finish(provider, error))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Native Q3 replacement wire requires real fresh source connections");
    wire->round_pending = false;
    wire->replacement_pending = false;
    return true;
}
