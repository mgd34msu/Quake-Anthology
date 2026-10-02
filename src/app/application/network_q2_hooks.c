#include "network_q2_private.h"
#include "qa/application_network.h"
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
    return actors(owner, id, values, &count, error) &&
        qa_application_network_q2_game_state(owner, values, count, out, error);
}

static bool begin(void *context, qa_net_client_id id, qa_net_seat_id seat, qa_error *error)
{
    qa_application_network_q2 *owner = context;
    qa_actor_id actor;
    return seat_actor(owner, id, seat, &actor, error) &&
        qa_application_remote_player_begin(owner->app, id, seat, error);
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
    char *copy = application_network_q2_copy(returned, error);
    if (!copy) { application_fault(owner->app, error); return false; }
    char *normalized = application_network_q2_copy(returned, error);
    if (!normalized) { free(copy); application_fault(owner->app, error); return false; }
    row = NULL;
    for (size_t i = 0; owner->app->players && i < owner->app->players->count; ++i) {
        application_player_record *candidate = &owner->app->players->records[i];
        if (!candidate->retiring && candidate->remote && qa_actor_id_equal(candidate->actor, actor) &&
            qa_net_client_id_equal(candidate->remote_client, id) &&
            candidate->remote_seat.owner == seat.owner && candidate->remote_seat.index == seat.index) {
            if (row) { free(copy); free(normalized); return application_fail(error, QA_ERROR_FORMAT, "Q2 userinfo callback aliased its Source roster"); }
            row = candidate;
        }
    }
    if (!row) { free(copy); free(normalized); return application_fail(error, QA_ERROR_ARGUMENT, "Q2 userinfo callback lost its Source roster"); }
    free(row->userinfo); row->userinfo = copy;
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

void application_network_q2_unbind(qa_application_network_q2 *owner)
{
    if (!owner || !owner->recipient_engine) return;
    application_provider *provider = recipient_source(owner->app, owner->host.source.source_owner);
    struct application_native_q2 *engine = provider && provider->kind == APPLICATION_PROVIDER_NATIVE ?
        provider->state.native.q2_engine : NULL;
    if (engine == owner->recipient_engine && engine->network_recipient_users &&
        engine->network_recipient_runtime == owner->bindings.runtime &&
        engine->network_recipient_context == owner->bindings.recipient_context &&
        engine->network_recipient == owner->bindings.recipient &&
        engine->network_unicast == owner->bindings.unicast) {
        if (!--engine->network_recipient_users) {
            engine->network_recipient_runtime = NULL;
            engine->network_recipient_context = NULL;
            engine->network_recipient = NULL;
            engine->network_unicast = NULL;
        }
    }
    owner->recipient_engine = NULL;
}

bool qa_application_network_q2_recipient(qa_application *app, qa_actor_owner source,
    qa_actor_id actor, qa_application_network_q2_recipient_view *out,
    bool *present, qa_error *error)
{
    if (!out || !present || !app || !app->session || !source || !actor.registry ||
        !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 recipient requires a live full Source actor");
    application_provider *provider = recipient_source(app, source);
    struct application_native_q2 *engine = provider && provider->kind == APPLICATION_PROVIDER_NATIVE ?
        provider->state.native.q2_engine : NULL;
    if (!engine || !provider->constructed || !provider->attached || provider->close_pending ||
        engine->provider != provider || !engine->initialized || !engine->map_ready ||
        engine->world != app->world || !provider->state.native.host ||
        (engine->profile != QA_NATIVE_Q2_GAME_API3 && engine->profile != QA_NATIVE_Q2_GAME_API2023))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 recipient lost its actual original GAME Source");
    uint32_t slot = 0;
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
    *out = (qa_application_network_q2_recipient_view){0}; *present = false;
    bool disconnecting = engine->disconnect_client == slot && engine->calls;
    if (!engine->clients[slot].connected || (engine->clients[slot].disconnect_started && !disconnecting) ||
        !engine->network_recipient) return true;
    qa_application_network_q2_recipient_view value = {0};
    bool found = false;
    if (!engine->network_recipient(engine->network_recipient_context, actor, &value, &found, error)) return false;
    if (!found) return true;
    const qa_net_client *connection = engine->network_recipient_runtime ?
        qa_net_connections_get(qa_network_connections(engine->network_recipient_runtime), value.client) : NULL;
    if (!connection || !qa_actor_id_equal(value.actor, actor) || !value.connection_epoch ||
        qa_network_epoch(engine->network_recipient_runtime, value.client) != value.connection_epoch ||
        value.remote_index >= connection->seat_count ||
        connection->seats[value.remote_index].remote_index != value.remote_index ||
        connection->seats[value.remote_index].seat.owner != value.seat.owner ||
        connection->seats[value.remote_index].seat.index != value.seat.index ||
        recipient_source(app, source) != provider || provider->state.native.q2_engine != engine ||
        !engine->clients[slot].connected ||
        (engine->clients[slot].disconnect_started && !(engine->disconnect_client == slot && engine->calls)) ||
        !qa_actor_id_equal(engine->clients[slot].actor, actor) ||
        !qa_actors_get(qa_session_actors(app->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 recipient changed its authentic connection group");
    *out = value; *present = true;
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
        .source = source, .source_frame = clock.frame.number, .source_time_ns = clock.frame.time_ns, .key = key};
    qa_sha256(qa_resource_bytes(app->map_resource), &claim.map);
    if (!engine->network_unicast(engine->network_recipient_context, &claim, remember, duplicate, error)) return false;
    qa_application_network_q2_recipient_view after;
    bool current;
    if (!qa_application_network_q2_recipient(app, source, actor, &after, &current, error) || !current ||
        !qa_net_client_id_equal(after.client, recipient.client) || after.connection_epoch != recipient.connection_epoch ||
        recipient_source(app, source) != provider || provider->state.native.q2_engine != engine ||
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
    return application_native_q2_wire_number(engine, actor, out, error);
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
    if (owner->host.source.kind == QA_APPLICATION_NATIVE_Q2_ORIGINAL) {
        application_provider *provider = application_network_q2_provider(owner);
        struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
        if (!engine || (engine->network_recipient_users &&
            (engine->network_recipient_runtime != bindings->runtime ||
                engine->network_recipient_context != bindings->recipient_context ||
                engine->network_recipient != bindings->recipient || engine->network_unicast != bindings->unicast)))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 peers disagree on their actual Source recipient owner");
        if (!owner->recipient_engine) {
            if (engine->network_recipient_users == SIZE_MAX)
                return application_fail(error, QA_ERROR_MEMORY, "Q2 recipient binding count overflows");
            engine->network_recipient_runtime = bindings->runtime;
            engine->network_recipient_context = bindings->recipient_context;
            engine->network_recipient = bindings->recipient;
            engine->network_unicast = bindings->unicast;
            ++engine->network_recipient_users; owner->recipient_engine = engine;
        } else if (owner->recipient_engine != engine)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q2 publisher retained a retired Source recipient binding");
    }
    owner->bindings = *bindings;
    *out = (qa_network_q2_server_hooks){.context = owner, .player = player, .game_state = game_state,
        .begin = begin, .input = input, .expand_command = expand_command, .command = command,
        .userinfo = userinfo, .download_source = download_source, .drop = drop};
    return true;
}

bool qa_application_network_q2_client_frame(qa_application_network_q2 *owner,
    qa_net_client_id id, qa_q2_wire_frame *out, qa_error *error)
{
    qa_actor_id values[QA_Q2_MAX_SEATS]; size_t count;
    return actors(owner, id, values, &count, error) &&
        qa_application_network_q2_frame(owner, values, count, out, error);
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
