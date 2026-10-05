#include "network_q2_private.h"
#include "client_events.h"
#include "native_q2_visibility.h"
#include "qa/application_network.h"
#include "qa/network_local.h"
#include "qa/text.h"
#include "native_q2_wire_engine.h"

static const qa_net_client *client(qa_application_network_q2 *owner,
    qa_net_client_id id, qa_error *error)
{
    const qa_net_client *value = owner && owner->bindings.runtime ?
        qa_net_connections_get(qa_network_connections(owner->bindings.runtime), id) : NULL;
    if (!value || value->protocol.kind != owner->host.protocol.kind ||
        value->protocol.revision != owner->host.protocol.revision ||
        value->protocol.flags != owner->host.protocol.flags ||
        !value->seat_count || value->seat_count > QA_Q2_MAX_SEATS) {
        application_fail(error, QA_ERROR_ARGUMENT, "Q2 Source hook lost its canonical connection");
        return NULL;
    }
    return value;
}

static bool seat_actor(qa_application_network_q2 *owner, qa_net_client_id id,
    qa_net_seat_id seat, qa_actor_id *out, qa_error *error)
{
    const qa_net_client *connection = client(owner, id, error);
    if (!connection || !qa_net_client_owns_seat(connection, seat) ||
        !qa_application_remote_player_actor(owner->app, id, seat, out))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 seat has no admitted canonical full actor");
    qa_network_q2_player physical;
    return qa_application_network_q2_player(owner, *out, &physical, error);
}

static bool actors(qa_application_network_q2 *owner, qa_net_client_id id,
    qa_actor_id values[QA_Q2_MAX_SEATS], size_t *count, qa_error *error)
{
    const qa_net_client *connection = client(owner, id, error);
    if (!connection) return false;
    for (size_t i = 0; i < connection->seat_count; ++i) {
        if (connection->seats[i].remote_index != i ||
            !seat_actor(owner, id, connection->seats[i].seat, &values[i], error))
            return application_fail(error, QA_ERROR_FORMAT, "Q2 ordered wire seats differ from their canonical admission");
    }
    *count = connection->seat_count;
    return true;
}

static bool player(void *context, qa_net_client_id id, qa_net_seat_id seat,
    qa_network_q2_player *out, qa_error *error)
{
    qa_application_network_q2 *owner = context;
    qa_actor_id actor;
    return out && seat_actor(owner, id, seat, &actor, error) &&
        qa_application_network_q2_player(owner, actor, out, error);
}

static bool game_state(void *context, qa_net_client_id id,
    qa_q2_game_state *out, qa_error *error)
{
    qa_application_network_q2 *owner = context;
    qa_actor_id values[QA_Q2_MAX_SEATS];
    size_t count;
    if (!actors(owner, id, values, &count, error) ||
        !qa_application_network_q2_game_state(owner, values, count, out, error)) return false;
    return !application_network_q2_materials_required(owner) ||
        (owner->materials_bound && owner->materials_capability) ||
        application_fail(error, QA_ERROR_UNSUPPORTED, "Q2 peer did not advertise the required actual material consumer");
}

static bool begin(void *context, qa_net_client_id id, qa_net_seat_id seat, qa_error *error)
{
    qa_application_network_q2 *owner = context;
    qa_actor_id actor;
    if (!seat_actor(owner, id, seat, &actor, error)) return false;
    application_snapshot_mutated(owner->app);
    return qa_application_remote_player_begin(owner->app, id, seat, error);
}

static bool input(void *context, qa_net_client_id id, qa_net_seat_id seat,
    const qa_network_q2_player *receipt, const qa_q2_usercmd *command,
    uint64_t sequence, qa_error *error)
{
    qa_application_network_q2 *owner = context;
    qa_network_q2_player actual;
    if (!receipt || !command || !player(owner, id, seat, &actual, error) ||
        !qa_actor_id_equal(actual.actor, receipt->actor) ||
        actual.source_owner != receipt->source_owner || actual.source_slot != receipt->source_slot ||
        actual.movement != receipt->movement)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 raw command lost its physical Source receipt");
    application_snapshot_mutated(owner->app);
    return owner->bindings.input(owner->bindings.context, id, seat, receipt, command, sequence, error);
}

static bool console(qa_application_network_q2 *owner, qa_actor_id actor,
    qa_console **out, qa_command_context *command, qa_error *error)
{
    application_provider *provider = application_network_q2_provider(owner);
    qa_cvars *registry = NULL;
    if (!provider || !application_network_q2_current(owner, error)) return false;
    if (owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN) {
        if (!application_native_q2_console_at(provider, out, &registry, command))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 GAME has no actual Source console");
    } else {
        struct application_native_q2 *engine = provider->state.native.q2_engine;
        if (!engine || !engine->console || !application_native_q2_idle(provider))
            return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 GAME has no idle Source console");
        *out = engine->console; *command = engine->command_context;
    }
    command->actor = actor; command->owner = provider->owner;
    command->origin = QA_COMMAND_REMOTE;
    if (actor.registry && !qa_application_player_seat(owner->app, actor, &command->seat))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 Source command lost its actual application seat");
    return true;
}

static bool expand_command(void *context, qa_net_client_id id,
    const char *text, qa_buffer *out, qa_error *error)
{
    qa_application_network_q2 *owner = context;
    qa_actor_id values[QA_Q2_MAX_SEATS]; size_t count;
    qa_console *source; qa_command_context command;
    return text && out && actors(owner, id, values, &count, error) &&
        console(owner, values[0], &source, &command, error) &&
        qa_console_expand_command(source, &command, text, out, error);
}

static bool command(void *context, qa_net_client_id id, qa_net_seat_id seat,
    const char *text, qa_error *error)
{
    qa_application_network_q2 *owner = context;
    qa_actor_id actor; qa_console *source; qa_command_context invocation;
    if (!text || !seat_actor(owner, id, seat, &actor, error) ||
        !console(owner, actor, &source, &invocation, error) ||
        owner->app->operation != APPLICATION_IDLE) return false;
    application_snapshot_mutated(owner->app);
    application_provider *provider = application_network_q2_provider(owner);
    bool handled = false, ok;
    if (owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL)
        return application_native_q2_console_command(provider, actor, text, &handled, error);
    qa_command_tokens tokens = {0};
    if (!qa_command_tokenize(text, invocation.dialect, false, &tokens, error)) return false;
    qa_command_invocation call = {.console = source, .context = invocation,
        .argc = tokens.count, .argv = (const char *const *)tokens.values,
        .args_text = tokens.args_text, .raw = text};
    owner->app->operation = APPLICATION_CONFIGURING;
    ok = !tokens.count || qa_q2_game_console_command(provider->state.q2, actor, &call, &handled, error);
    owner->app->operation = APPLICATION_IDLE;
    qa_command_tokens_free(&tokens);
    if (!ok) application_fault(owner->app, error);
    return ok;
}

static bool info_valid(qa_application_network_q2 *owner, const char *text, qa_error *error)
{
    size_t length = strlen(text);
    bool rr = owner->host.source.edition == QA_Q2_RERELEASE;
    bool enhanced = owner->host.protocol.kind == QA_NET_R1Q2_35 ||
        owner->host.protocol.kind == QA_NET_Q2PRO_36;
    if (length >= (rr ? 2048u : 512u) ||
        (rr && !qa_utf8_valid((qa_bytes){(const uint8_t *)text, length})))
        return application_fail(error, QA_ERROR_FORMAT, "Q2 userinfo exceeds its physical Source string profile");
    if (!rr && !enhanced) return true;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor) {
        if (*cursor == '\\') ++cursor;
        size_t key = 0, value = 0;
        while (*cursor && *cursor != '\\') {
            unsigned c = *cursor++;
            if (c < 32 || (!rr && c >= 127) || c == '"' || c == ';' || ++key >= 64)
                return application_fail(error, QA_ERROR_FORMAT, "Q2 userinfo has an invalid Source key");
        }
        if (!key || !*cursor) return application_fail(error, QA_ERROR_FORMAT, "Q2 userinfo has a missing Source key/value");
        ++cursor;
        while (*cursor && *cursor != '\\') {
            unsigned c = *cursor++;
            if (c < 32 || (!rr && c >= 127) || c == '"' || c == ';' || ++value >= (rr ? 256u : 64u))
                return application_fail(error, QA_ERROR_FORMAT, "Q2 userinfo has an invalid Source value");
        }
        if (enhanced && !value) return application_fail(error, QA_ERROR_FORMAT, "Q2 userinfo has an empty enhanced Source value");
        if (*cursor == '\\' && !cursor[1]) return application_fail(error, QA_ERROR_FORMAT, "Q2 userinfo has a trailing Source separator");
    }
    return *text || application_fail(error, QA_ERROR_FORMAT, "Q2 Source userinfo is empty");
}

static bool userinfo(void *context, qa_net_client_id id, qa_net_seat_id seat,
    const char *text, qa_buffer *result, qa_error *error)
{
    qa_application_network_q2 *owner = context;
    qa_actor_id actor; qa_network_q2_player physical;
    if (!text || !result || result->data || result->size || !seat_actor(owner, id, seat, &actor, error) ||
        !qa_application_network_q2_player(owner, actor, &physical, error) ||
        owner->app->operation != APPLICATION_IDLE) return false;
    if (!info_valid(owner, text, error)) return false;
    application_player_record *row = NULL;
    for (size_t i = 0; owner->app->players && i < owner->app->players->count; ++i) {
        application_player_record *candidate = &owner->app->players->records[i];
        if (!candidate->retiring && candidate->remote && qa_actor_id_equal(candidate->actor, actor) &&
            qa_net_client_id_equal(candidate->remote_client, id) &&
            candidate->remote_seat.owner == seat.owner && candidate->remote_seat.index == seat.index) {
            if (row) return application_fail(error, QA_ERROR_FORMAT, "Q2 userinfo aliases two canonical roster rows");
            row = candidate;
        }
    }
    if (!row) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 userinfo lost its canonical Source admission");
    application_snapshot_mutated(owner->app);
    application_provider *provider = application_network_q2_provider(owner);
    owner->app->operation = APPLICATION_CONFIGURING;
    bool ok = owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN ?
        qa_q2_player_userinfo(provider->state.q2, actor, text, error) :
        application_native_q2_client_userinfo(provider, physical.source_slot, text, error);
    owner->app->operation = APPLICATION_IDLE;
    if (!ok) { application_fault(owner->app, error); return false; }
    qa_actor_id after;
    if (!qa_application_remote_player_actor(owner->app, id, seat, &after) ||
        !qa_actor_id_equal(after, actor) || !qa_application_network_q2_player(owner, actor, &physical, error)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Q2 userinfo callback retired its admitted Source actor");
        application_fault(owner->app, error);
        return false;
    }
    const char *returned = NULL;
    if (owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_BUILTIN) {
        if (!qa_q2_player_userinfo_read(provider->state.q2, actor, &returned, error)) return false;
    } else {
        struct application_native_q2 *engine = provider->state.native.q2_engine;
        if (!engine || !engine->clients[physical.source_slot].userinfo_present)
            return application_fail(error, QA_ERROR_ARGUMENT, "Original Q2 userinfo has no returned Source dictionary");
        returned = engine->clients[physical.source_slot].userinfo;
    }
    char *normalized = application_network_q2_copy(returned, error);
    if (!normalized) { application_fault(owner->app, error); return false; }
    ok = application_client_userinfo_publish(owner->app, actor, returned, error);
    qa_network_q2_player current;
    if (ok && (!seat_actor(owner, id, seat, &after, error) ||
        !qa_actor_id_equal(after, actor) ||
        !qa_application_network_q2_player(owner, actor, &current, error) ||
        current.source_owner != physical.source_owner || current.source_slot != physical.source_slot ||
        current.movement != physical.movement))
        ok = application_fail(error, QA_ERROR_ARGUMENT,
            "Q2 userinfo notification changed its physical Source recipient");
    if (!ok) { free(normalized); application_fault(owner->app, error); return false; }
    *result = (qa_buffer){.data = (uint8_t *)normalized, .size = strlen(normalized) + 1};
    return true;
}

static bool download_source(void *context, qa_net_client_id id,
    qa_network_q2_download_source *out, qa_error *error)
{
    qa_application_network_q2 *owner = context;
    return client(owner, id, error) && qa_application_network_q2_download_source(owner, out, error);
}

static bool drop(void *context, qa_net_client_id id, const char *reason, qa_error *error)
{
    qa_application_network_q2 *owner = context;
    return client(owner, id, error) &&
        owner->bindings.drop(owner->bindings.context, id, reason, error);
}

static application_provider *recipient_source(qa_application *app, qa_actor_owner owner)
{
    application_provider *found = NULL;
    for (size_t i = 0; app && i < app->provider_count; ++i) {
        application_provider *provider = app->providers[i];
        if (provider->owner != owner) continue;
        if (found) return NULL;
        found = provider;
    }
    return found;
}

struct application_q2_recipient_binding {
    application_provider *provider;
    struct application_native_q2 *engine;
    qa_application_network_q2_bindings bindings;
    size_t references, users;
};

static void recipient_binding_release(struct application_q2_recipient_binding *binding)
{
    if (binding && !--binding->references) free(binding);
}

static void recipient_binding_engine(struct application_q2_recipient_binding *binding)
{
    struct application_native_q2 *engine = binding->engine;
    if (!engine) return;
    engine->network_recipient_binding = binding->provider ? binding : NULL;
    engine->network_recipient_users = binding->users;
    engine->network_recipient_runtime = binding->users ? binding->bindings.runtime : NULL;
    engine->network_recipient_context = binding->users ? binding->bindings.recipient_context : NULL;
    engine->network_recipient = binding->users ? binding->bindings.recipient : NULL;
    engine->network_unicast = binding->users ? binding->bindings.unicast : NULL;
}

void application_network_q2_retire_source_bindings(application_provider *provider)
{
    if (!provider || !provider->q2_recipient_binding) return;
    struct application_q2_recipient_binding *binding = provider->q2_recipient_binding;
    provider->q2_recipient_binding = NULL; binding->provider = NULL;
    binding->users = 0;
    recipient_binding_engine(binding);
    binding->engine = NULL; binding->bindings = (qa_application_network_q2_bindings){0};
    recipient_binding_release(binding);
}

void application_network_q2_retire_bindings(struct application_native_q2 *engine)
{
    if (engine && engine->network_recipient_binding)
        application_network_q2_retire_source_bindings(engine->network_recipient_binding->provider);
}

void application_network_q2_unbind(qa_application_network_q2 *owner)
{
    if (!owner || !owner->recipient_binding) return;
    struct application_q2_recipient_binding *binding = owner->recipient_binding;
    if (binding->provider && binding->users) {
        --binding->users;
        recipient_binding_engine(binding);
        if (!binding->users) binding->bindings = (qa_application_network_q2_bindings){0};
    }
    owner->recipient_binding = NULL; recipient_binding_release(binding);
}

bool qa_application_network_q2_recipient(qa_application *app, qa_actor_owner source,
    qa_actor_id actor, qa_application_network_q2_recipient_view *out,
    bool *present, qa_error *error)
{
    if (!out || !present || !app || !app->session || !source || !actor.registry ||
        !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 recipient requires a live full Source actor");
    application_provider *provider = recipient_source(app, source);
    if (!provider || !provider->constructed || !provider->attached || provider->close_pending ||
        !provider->product || provider->product->family != QA_GAME_Q2)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 recipient lost its actual GAME Source");
    struct application_q2_recipient_binding *binding = provider->q2_recipient_binding;
    *out = (qa_application_network_q2_recipient_view){0}; *present = false;
    struct application_native_q2 *engine = provider->kind == APPLICATION_PROVIDER_NATIVE ?
        provider->state.native.q2_engine : NULL;
    uint32_t slot = 0;
    qa_q2_player_info player;
    if (provider->kind == APPLICATION_PROVIDER_Q2) {
        if (!provider->state.q2 || !qa_q2_player_read(provider->state.q2, actor, &player))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 recipient has no physical Source player");
        if (!player.connected) return true;
    } else {
        if (!engine || engine->provider != provider || !engine->initialized || !engine->map_ready ||
            engine->world != app->world || !provider->state.native.host ||
            (engine->profile != QA_NATIVE_Q2_GAME_API3 && engine->profile != QA_NATIVE_Q2_GAME_API2023))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 recipient lost its actual original GAME Source");
        for (uint32_t i = 1; i < 257; ++i) {
            if (!qa_actor_id_equal(engine->clients[i].actor, actor)) continue;
            if (slot) return application_fail(error, QA_ERROR_FORMAT, "Q2 recipient aliases two actual Source clients");
            slot = i;
        }
        if (!slot)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 recipient has no physical Source client");
        qa_native_slot_binding physical;
        if (!qa_native_slot(qa_native_host_instance(provider->state.native.host), slot, &physical, error) ||
            physical.kind == QA_NATIVE_SLOT_FREE || physical.owner != source || physical.source_slot != slot ||
            !qa_actor_id_equal(physical.actor, actor))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 recipient lost its actual SDK client binding");
        bool disconnecting = engine->disconnect_client == slot && engine->calls;
        if (!engine->clients[slot].connected || (engine->clients[slot].disconnect_started && !disconnecting)) return true;
    }
    if (!binding || !binding->users) return true;
    qa_application_network_q2_recipient_view value = {0};
    bool found = false;
    if (!binding->bindings.recipient(binding->bindings.recipient_context, actor, &value, &found, error)) return false;
    if (!found) return true;
    const qa_net_client *connection = qa_net_connections_get(qa_network_connections(binding->bindings.runtime), value.client);
    if (!connection || !qa_actor_id_equal(value.actor, actor) || !value.connection_epoch ||
        qa_network_epoch(binding->bindings.runtime, value.client) != value.connection_epoch ||
        value.remote_index >= connection->seat_count ||
        connection->seats[value.remote_index].remote_index != value.remote_index ||
        connection->seats[value.remote_index].seat.owner != value.seat.owner ||
        connection->seats[value.remote_index].seat.index != value.seat.index ||
        recipient_source(app, source) != provider || provider->q2_recipient_binding != binding ||
        binding->provider != provider || !binding->users ||
        (engine ? (provider->state.native.q2_engine != engine || !engine->clients[slot].connected ||
            (engine->clients[slot].disconnect_started && !(engine->disconnect_client == slot && engine->calls)) ||
            !qa_actor_id_equal(engine->clients[slot].actor, actor)) :
            !qa_q2_player_read(provider->state.q2, actor, &player)) ||
        !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 recipient changed its authentic connection group");
    *out = value; *present = true;
    return true;
}

bool application_network_q2_print_recipients(application_provider *provider, qa_actor_id actor,
    qa_arena *arena, const qa_application_network_q2_recipient_view **out, size_t *count, qa_error *error)
{
    struct application_q2_recipient_binding *binding = provider ? provider->q2_recipient_binding : NULL;
    if (!provider || !arena || !out || !count || !provider->constructed || !provider->attached ||
        provider->close_pending ||
        !binding || binding->provider != provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 print lost its actual Source recipient lifetime");
    *out = NULL; *count = 0;
    if (!binding->users) return true;
    if (actor.registry) {
        qa_application_network_q2_recipient_view recipient;
        bool present;
        if (!qa_application_network_q2_recipient(provider->application, provider->owner,
            actor, &recipient, &present, error)) return false;
        if (!present) return true;
        qa_application_network_q2_recipient_view *copy = qa_arena_alloc(arena, sizeof(*copy), _Alignof(qa_application_network_q2_recipient_view), error);
        if (!copy) return false;
        *copy = recipient; *out = copy; *count = 1; return true;
    }
    if (!qa_actor_id_equal(actor, (qa_actor_id){0}))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 broadcast print has invalid actor provenance");
    const qa_net_connections *connections = qa_network_connections(binding->bindings.runtime);
    uint32_t cursor = 0; const qa_net_client *client;
    size_t capacity = 0;
    while (qa_net_connections_next(connections, &cursor, &client)) {
        if (client->phase != QA_NET_ACTIVE) continue;
        if (client->seat_count > SIZE_MAX / sizeof(qa_application_network_q2_recipient_view) - capacity)
            return application_fail(error, QA_ERROR_MEMORY, "Q2 broadcast recipient extent overflows");
        capacity += client->seat_count;
    }
    if (!capacity) return true;
    qa_application_network_q2_recipient_view *copies = qa_arena_alloc(arena,
        capacity * sizeof(*copies), _Alignof(qa_application_network_q2_recipient_view), error);
    if (!copies) return false;
    cursor = 0;
    while (qa_net_connections_next(connections, &cursor, &client)) {
        if (client->phase != QA_NET_ACTIVE) continue;
        for (size_t i = 0; i < client->seat_count; ++i) {
            qa_actor_id player;
            bool found = qa_application_remote_player_actor(provider->application, client->id, client->seats[i].seat, &player);
            if (!found && client->attachment == QA_NET_LOCAL_SEAT) {
                qa_network_local_player local;
                if (!qa_network_local_player_read(binding->bindings.runtime, client->id, &local, error)) return false;
                player = local.actor; found = true;
            }
            if (!found) continue;
            qa_application_network_q2_recipient_view recipient;
            bool present;
            if (!qa_application_network_q2_recipient(provider->application, provider->owner,
                player, &recipient, &present, error)) return false;
            if (!present) continue;
            if (*count == capacity || !qa_net_client_id_equal(recipient.client, client->id) ||
                recipient.seat.owner != client->seats[i].seat.owner || recipient.seat.index != client->seats[i].seat.index ||
                recipient.remote_index != client->seats[i].remote_index)
                return application_fail(error, QA_ERROR_ARGUMENT, "Q2 broadcast changed its authentic recipient group");
            copies[(*count)++] = recipient;
        }
    }
    if (*count) *out = copies;
    return true;
}

bool qa_application_network_q2_unicast(qa_application *app, qa_actor_owner source,
    qa_actor_id actor, uint32_t key, bool remember, bool *duplicate, qa_error *error)
{
    if (!duplicate) return application_fail(error, QA_ERROR_ARGUMENT, "Q2 unicast requires a duplicate receipt");
    *duplicate = false;
    if (!key) return true;
    qa_application_network_q2_recipient_view recipient;
    bool present;
    if (!qa_application_network_q2_recipient(app, source, actor, &recipient, &present, error)) return false;
    if (!present) return true;
    application_provider *provider = recipient_source(app, source);
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    qa_clock_state clock;
    if (!engine || !engine->calls || !engine->network_unicast || !app->map_resource ||
        !qa_session_clock(app->session, source, &clock) || clock.frame.provider != source ||
        clock.frame.kind != (engine->profile == QA_NATIVE_Q2_GAME_API3 ? QA_CLOCK_Q2_CLASSIC : QA_CLOCK_Q2_RERELEASE) ||
        (clock.frame.number && (engine->frame.provider != source ||
            engine->frame.number != clock.frame.number || engine->frame.time_ns != clock.frame.time_ns)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 unicast lost its executing Source clock or cache owner");
    qa_q2_unicast_claim claim = {.client = recipient.client, .connection_epoch = recipient.connection_epoch,
        .source = source, .map_revision = app->map_revision,
        .source_frame = clock.frame.number, .source_time_ns = clock.frame.time_ns, .key = key};
    claim.map = *qa_resource_digest(app->map_resource);
    if (!engine->network_unicast(engine->network_recipient_context, &claim, remember, duplicate, error)) return false;
    qa_application_network_q2_recipient_view after;
    bool current;
    if (!qa_application_network_q2_recipient(app, source, actor, &after, &current, error) || !current ||
        !qa_net_client_id_equal(after.client, recipient.client) || after.connection_epoch != recipient.connection_epoch ||
        app->map_revision != claim.map_revision || recipient_source(app, source) != provider ||
        provider->state.native.q2_engine != engine ||
        !qa_session_clock(app->session, source, &clock) || clock.frame.number != claim.source_frame ||
        clock.frame.time_ns != claim.source_time_ns)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 unicast changed its actual Source or recipient group");
    return true;
}

bool qa_application_network_q2_entity_number(qa_application *app, qa_actor_owner source,
    qa_actor_id actor, uint32_t *out, qa_error *error)
{
    application_provider *provider = recipient_source(app, source);
    struct application_native_q2 *engine = provider && provider->kind == APPLICATION_PROVIDER_NATIVE ?
        provider->state.native.q2_engine : NULL;
    if (!engine || !engine->calls || !provider->constructed || !provider->attached ||
        provider->close_pending || engine->world != app->world ||
        engine->profile == QA_NATIVE_Q2_CGAME_API2023)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 entity reference requires its genuine executing GAME namespace");
    if (!engine->wire_engine && !application_native_q2_wire_begin(engine, error)) return false;
    application_provider *physical = application_world_provider(app, QA_ROLE_ENTITIES, "");
    struct application_native_q2 *host = physical && physical->kind == APPLICATION_PROVIDER_NATIVE ?
        physical->state.native.q2_engine : NULL;
    if (host && host->profile != QA_NATIVE_Q2_CGAME_API2023 && physical->constructed && physical->attached &&
        !physical->close_pending && host->world == app->world) {
        if (!host->wire_engine && !application_native_q2_wire_begin(host, error)) return false;
        return application_native_q2_wire_admit(host, actor, out, error);
    }
    if (physical && physical->kind == APPLICATION_PROVIDER_Q2 && physical->constructed && physical->attached &&
        !physical->close_pending)
        return qa_q2_wire_entity_number(physical->state.q2, actor, out, error);
    return application_native_q2_wire_number(engine, actor, out, error);
}

bool qa_application_network_q2_event_entity(qa_application_network_q2 *owner, qa_actor_owner emitter,
    qa_actor_id actor, uint32_t *out, qa_error *error)
{
    if (!owner || !out || !actor.registry || !application_network_q2_current(owner, error)) return false;
    application_provider *source = recipient_source(owner->app, emitter);
    if (!source || !source->constructed || !source->attached || source->close_pending || !source->product ||
        source->product->family != QA_GAME_Q2)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 event entity lost its actual emitting Source");
    if (owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL) {
        application_provider *physical = application_network_q2_provider(owner);
        struct application_native_q2 *engine = physical ? physical->state.native.q2_engine : NULL;
        return application_native_q2_wire_admit(engine, actor, out, error);
    }
    qa_q2_game *game = (qa_q2_game *)owner->host.source.source.game;
    return qa_q2_wire_entity_number(game, actor, out, error);
}

bool qa_application_network_q2_hooks(qa_application_network_q2 *owner,
    const qa_application_network_q2_bindings *bindings,
    qa_network_q2_server_hooks *out, qa_error *error)
{
    if (!out || !bindings || !bindings->runtime || !bindings->input || !bindings->drop || !bindings->recipient || !bindings->unicast ||
        !application_network_q2_current(owner, error) ||
        (owner->bindings.runtime && (owner->bindings.runtime != bindings->runtime ||
            owner->bindings.context != bindings->context || owner->bindings.input != bindings->input ||
            owner->bindings.drop != bindings->drop || owner->bindings.recipient_context != bindings->recipient_context ||
            owner->bindings.recipient != bindings->recipient || owner->bindings.unicast != bindings->unicast)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 host hooks require their real Network input and retirement owners");
    application_provider *provider = application_network_q2_provider(owner);
    struct application_native_q2 *engine = owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL && provider ?
        provider->state.native.q2_engine : NULL;
    if (!provider || (owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL && !engine))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 hooks lost their physical Source binding owner");
    struct application_q2_recipient_binding *binding = provider->q2_recipient_binding;
    if (binding && binding->users &&
        (binding->bindings.runtime != bindings->runtime ||
            binding->bindings.recipient_context != bindings->recipient_context ||
            binding->bindings.recipient != bindings->recipient || binding->bindings.unicast != bindings->unicast))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 peers disagree on their actual Source recipient owner");
    if (!binding) {
        binding = calloc(1, sizeof(*binding));
        if (!binding) return application_fail(error, QA_ERROR_MEMORY, "Retaining actual Q2 Source recipient lifetime");
        binding->provider = provider; binding->engine = engine; binding->references = 1;
        provider->q2_recipient_binding = binding;
    }
    if (!owner->recipient_binding) {
        if (binding->users == SIZE_MAX || binding->references == SIZE_MAX)
            return application_fail(error, QA_ERROR_MEMORY, "Q2 recipient lifetime reference count overflows");
        binding->bindings = *bindings; ++binding->users; ++binding->references;
        owner->recipient_binding = binding; recipient_binding_engine(binding);
    } else if (owner->recipient_binding != binding)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 publisher retained a retired Source recipient binding");
    owner->bindings = *bindings;
    *out = (qa_network_q2_server_hooks){.context = owner, .player = player, .game_state = game_state,
        .begin = begin, .input = input, .expand_command = expand_command, .command = command,
        .userinfo = userinfo, .download_source = download_source, .drop = drop};
    return true;
}

bool qa_application_network_q2_client_frame_ready(qa_application_network_q2 *owner,
    qa_net_client_id id, bool *out, qa_error *error)
{
    qa_actor_id values[QA_Q2_MAX_SEATS]; size_t count;
    if (!out || !application_network_q2_current(owner, error) || !actors(owner, id, values, &count, error)) return false;
    *out = true;
    if (owner->host.source.kind != QA_APPLICATION_NATIVE_Q2_ORIGINAL) return true;
    qa_network_q2_player players[QA_Q2_MAX_SEATS];
    for (size_t i = 0; i < count; ++i)
        if (!qa_application_network_q2_player(owner, values[i], &players[i], error)) return false;
    application_provider *physical = application_network_q2_provider(owner);
    struct application_native_q2 *engine = physical ? physical->state.native.q2_engine : NULL;
    return application_native_q2_visibility_ready(engine, players, count, out, error);
}

bool qa_application_network_q2_client_frame(qa_application_network_q2 *owner,
    qa_net_client_id id, qa_q2_wire_frame *out, qa_error *error)
{
    qa_actor_id values[QA_Q2_MAX_SEATS]; size_t count;
    if (!actors(owner, id, values, &count, error) ||
        !qa_application_network_q2_frame(owner, values, count, out, error)) return false;
    return !application_network_q2_materials_required(owner) ||
        (owner->materials_bound && owner->materials_capability) ||
        application_fail(error, QA_ERROR_UNSUPPORTED, "Q2 peer did not advertise the required actual material consumer");
}

bool qa_application_network_q2_discovery(qa_application_network_q2 *owner,
    qa_q2_status *out, const char **name, const char **map, qa_error *error)
{
    if (!name || !map || !qa_application_network_q2_status(owner, out, error)) return false;
    const qa_cvar_view *hostname = qa_cvars_find(owner->host.cvars, "hostname");
    *name = hostname ? hostname->value : "";
    *map = qa_strings_cstr(qa_session_strings(owner->app->session), owner->app->current_map);
    return *map || application_fail(error, QA_ERROR_FORMAT, "Q2 discovery has no actual Source map");
}

bool qa_application_network_q2_download_server(qa_application_network_q2 *owner,
    const char **out, qa_error *error)
{
    if (!out || !application_network_q2_current(owner, error)) return false;
    const qa_cvar_view *value = qa_cvars_find(owner->host.cvars, "sv_downloadserver");
    const char *url = value ? value->value : "";
    if (*url && strncmp(url, "http://", 7) && strncmp(url, "https://", 8))
        return application_fail(error, QA_ERROR_FORMAT, "Q2 Source download server is not an HTTP URL");
    for (const unsigned char *p = (const unsigned char *)url; *p; ++p)
        if (*p <= 32 || *p == '"' || *p == '\\')
            return application_fail(error, QA_ERROR_FORMAT, "Q2 Source download server has invalid protocol characters");
    *out = url;
    return true;
}
