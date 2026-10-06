#include "guest_q3_private.h"
#include "guest_q3_factory.h"
#include "guest_projection_private.h"
#include "qa/application_players.h"
#include "q3_world_restart.h"
#include "guest_q3_restart.h"
#include "native_q3_wire_state.h"
#include "native_q3_console.h"
#include "guest_q3_combat.h"
#include "guest_q3_weapons_services.h"
#include "qa/json.h"

bool application_q3_guest_actor_bound(application_provider *provider, qa_actor_id actor,
    uint32_t *slot)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !slot || !engine->game || !engine->game->initialized ||
        !qa_actors_get(qa_session_actors(provider->application->session), actor)) return false;
    uint32_t actual;
    if (!qa_q3_host_actor_slot(engine->game->host, actor, &actual, NULL) || actual >= 64) return false;
    const q3g_client *client = &engine->clients[actual];
    if (!(client->reserved || client->allocated) || client->pending_retirement ||
        !qa_actor_id_equal(client->actor, actor)) return false;
    *slot = actual;
    return true;
}

bool application_q3_guest_client_reserve(application_provider *provider, uint32_t slot,
    qa_actor_id actor, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || slot >= 64 || !provider->constructed || !provider->attached ||
        !engine->game || !engine->game->initialized || engine->restore_pending || engine->calls ||
        engine->draining_clients || provider->application->operation != APPLICATION_CONFIGURING ||
        !qa_session_safe(provider->application->session) || !qa_world_idle(engine->world) ||
        !application_q3_guest_idle(provider) ||
        (engine->round.phase != Q3G_ROUND_NONE &&
         (engine->round.phase != Q3G_ROUND_SETTLING || engine->round.completed_frames != 3)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source client reservation requires its genuine prejoin cut");
    q3g_client *client = &engine->clients[slot];
    const qa_actor_record *record = qa_actors_get(qa_session_actors(provider->application->session), actor);
    bool borrowed = record && record->owner != provider->owner;
    if (!record || client->connected || client->pending_retirement ||
        (client->actor.registry && !qa_actor_id_equal(client->actor, actor)) ||
        (!borrowed && (!record->has_source || record->source_slot != slot)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source client reservation changes its physical actor binding");
    if (!qa_q3_host_bind_actor(engine->game->host, slot, actor, borrowed, error)) return false;
    client->actor = actor;
    client->reserved = true;
    return true;
}

bool application_q3_guest_actor_client(application_provider *provider, qa_actor_id actor,
                                         uint32_t *slot)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !slot || !engine->game || !engine->game->initialized ||
        !qa_actors_get(qa_session_actors(provider->application->session), actor)) return false;
    for (uint32_t i = 0; i < 64; ++i) {
        const q3g_client *client = &engine->clients[i];
        if (client->connected && client->begun && !client->pending_retirement &&
            qa_actor_id_equal(client->actor, actor)) { *slot = i; return true; }
    }
    return false;
}

bool application_q3_guest_input_values_read(application_provider *provider,uint32_t seat,
    qa_actor_id actor,uint8_t *weapon,float *sensitivity,qa_error *error)
{
    struct application_q3_guest *engine=q3g_engine(provider);
    uint32_t slot;
    if (!engine || engine->calls || engine->entered_role || engine->draining_clients ||
        engine->restore_pending || engine->round.phase!=Q3G_ROUND_NONE || !engine->map_ready ||
        !application_q3_guest_actor_bound(provider,actor,&slot))
        return application_fail(error,QA_ERROR_ARGUMENT,"Q3 command selection lost its returned foreign GAME source");
    const q3g_client *client=engine->clients+slot;
    if (!client->connected || !client->begun || client->bot || client->pending_retirement ||
        client->disconnect_pending || client->disconnect_started || engine->seats[slot]!=seat)
        return application_fail(error,QA_ERROR_ARGUMENT,"Q3 command selection lost its current foreign CLIENT");
    *weapon=(uint8_t)client->weapon; *sensitivity=client->sensitivity;
    return true;
}

static q3g_client *client_slot(application_provider *provider, uint32_t slot,
                               struct application_q3_guest **engine, qa_error *error)
{
    *engine = q3g_engine(provider);
    if (!*engine || !(*engine)->game || !(*engine)->game->initialized || slot >= 64 ||
        ((*engine)->round.phase != Q3G_ROUND_NONE && !(*engine)->round.source_entry)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Q3 client requires an initialized game and source slot");
        return NULL;
    }
    return &(*engine)->clients[slot];
}

bool application_q3_guest_client_connect(application_provider *provider, uint32_t slot,
                                          qa_actor_id actor, const char *info, bool first_time, bool bot,
                                          bool *accepted, qa_error *error)
{
    struct application_q3_guest *engine;
    q3g_client *client = client_slot(provider, slot, &engine, error);
    if (!client) return false;
    if (!info || !accepted || client->connected || client->pending_retirement)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client connection arguments or lifecycle are invalid");
    *accepted = false;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(provider->application->session), actor);
    bool borrowed = record && record->owner != provider->owner;
    if (!record || (!borrowed && (!record->has_source || record->source_slot != slot)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client actor/source ownership mismatch");
    if (!bot) {
        uint32_t seat;
        if (!qa_application_player_seat(provider->application, actor, &seat))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 human client has no canonical seat admission");
        if (engine->seats[slot] != UINT32_MAX && engine->seats[slot] != seat)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source human slot belongs to another admitted seat");
        engine->seats[slot] = seat;
    }
    char *copy = q3g_copy_text(info, error); if (!copy) return false;
    if (!qa_q3_host_bind_actor(engine->game->host, slot, actor, borrowed, error)) { free(copy); return false; }
    free(client->userinfo); client->userinfo = copy;
    client->actor = actor; client->bot = bot; client->allocated = true;
    client->begun = false;
    client->disconnect_started = false;
    client->entered_ns = qa_session_elapsed(provider->application->session);
    application_q3_world_startup startup;
    client->carry_pending = !first_time &&
        application_q3_world_restart_source(provider->application, provider, &startup);
    int32_t arguments[] = {(int32_t)slot, first_time ? 1 : 0, bot ? 1 : 0}, result;
    if (engine->game->native) {
        qa_buffer denial = {0};
        ++engine->calls;
        bool ok = qa_native_host_q3_client_connect(engine->game->native, slot, first_time, bot, &denial, error);
        --engine->calls;
        if (ok) *accepted = !denial.data;
        qa_buffer_free(&denial);
        if (!ok) return false;
    } else {
        if (!q3g_call(engine->game, 2, arguments, 3, &result, error)) return false;
        if (result) { qa_bytes denial; if (!qa_qvm_read_string(engine->game->vm, result, &denial, error)) return false; }
        else *accepted = true;
    }
    if (client->pending_retirement) *accepted = false;
    client->connected = *accepted;
    client->reserved = false;
    if (!*accepted && !client->pending_retirement &&
        !application_guest_client_drop(provider, slot, "", error)) return false;
    return application_guest_bots_admit(provider, error) && application_guest_clients_drain(provider, error);
}

bool application_q3_guest_client_enter_command(application_provider *provider, uint32_t slot,
    qa_actor_id actor, const qa_q3_usercmd *command, qa_error *error)
{
    struct application_q3_guest *engine;
    q3g_client *client = client_slot(provider, slot, &engine, error);
    if (!client || !command || !provider->constructed || !provider->attached ||
        engine->calls || engine->draining_clients || engine->restore_pending ||
        !client->allocated || !client->connected || client->begun || client->bot ||
        client->carry_pending || client->pending_retirement ||
        !qa_actor_id_equal(client->actor, actor) ||
        !qa_actors_get(qa_session_actors(provider->application->session), actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 enter command requires its accepted physical client before Begin");
    uint32_t actual;
    if (!qa_q3_host_actor_slot(engine->game->host, actor, &actual, error)) return false;
    if (actual != slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 enter command changes the actual source client slot");
    client->command = *command;
    return true;
}

bool application_q3_guest_client_begin(application_provider *provider, uint32_t slot, qa_error *error)
{
    struct application_q3_guest *engine;
    q3g_client *client = client_slot(provider, slot, &engine, error);
    if (!client || !client->connected || client->carry_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client has not completed its source command admission");
    int32_t argument = (int32_t)slot, result;
    if (!q3g_call(engine->game, 3, &argument, 1, &result, error)) return false;
    if (!client->connected || client->pending_retirement ||
        !qa_actors_get(qa_session_actors(provider->application->session), client->actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client retired during source begin");
    client->begun = true;
    client->carry_pending = false;
    if (!application_guest_actor_admit(provider, client->actor, error)) return false;
    if (application_provider_for(provider->application, client->actor, QA_ROLE_CHARACTER, "") == provider) {
        if (engine->game->combat && !application_q3_combat_admit(engine->game->combat, client->actor, error)) return false;
        if (engine->game->weapon_services &&
            !application_q3_guest_selected_respawn(provider, client->actor, error)) return false;
    }
    uint32_t seat;
    if (engine->game->weapon_services &&
        application_world_provider(provider->application, QA_ROLE_ENTITIES, "") == provider &&
        qa_application_player_seat(provider->application, client->actor, &seat) &&
        !application_q3_weapons_services_match_admit(engine->game->weapon_services, client->actor, error)) return false;
    if (!client->gamestate) {
        client->gamestate = malloc(sizeof(*client->gamestate));
        if (!client->gamestate) return application_fail(error, QA_ERROR_MEMORY, "retaining Q3 local client gamestate");
    }
    *client->gamestate = engine->gamestate;
    client->gamestate->client_number = (int32_t)slot;
    client->gamestate->command_sequence = client->reliable.sequence;
    bool clamped;
    if (!qa_q3_reliable_ack(&client->reliable, QA_Q3_SERVER, client->reliable.sequence, &clamped, error)) return false;
    client->consumed_server_command = client->reliable.sequence;
    if (!q3g_arsenal_client_admit(provider, slot, error)) return false;
    return application_guest_bots_admit(provider, error) && application_guest_clients_drain(provider, error);
}

bool application_q3_guest_client_userinfo(application_provider *provider, uint32_t slot,
                                           const char *info, qa_error *error)
{
    struct application_q3_guest *engine;
    q3g_client *client = client_slot(provider, slot, &engine, error);
    if (!client || !client->connected || !info)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 userinfo requires a connected client");
    char *copy = q3g_copy_text(info, error); if (!copy) return false;
    free(client->userinfo); client->userinfo = copy;
    int32_t argument = (int32_t)slot, result;
    return q3g_call(engine->game, 4, &argument, 1, &result, error);
}

bool application_q3_guest_client_disconnect(application_provider *provider, uint32_t slot, qa_error *error)
{
    struct application_q3_guest *engine;
    q3g_client *client = client_slot(provider, slot, &engine, error);
    if (!client) return false;
    qa_error first = {0};
    bool ok = true;
    if (client->connected) {
        int32_t argument = (int32_t)slot, result;
        client->connected = false;
        client->disconnect_started = true;
        ok = q3g_call(engine->game, 5, &argument, 1, &result, &first);
    }
    qa_error current = {0};
    if (!application_guest_client_drop(provider, slot, "", &current) && ok) { ok = false; first = current; }
    if (!application_guest_clients_drain(provider, &current) && ok) { ok = false; first = current; }
    if (!ok && error) *error = first;
    return ok;
}

bool application_q3_guest_client_think(application_provider *provider, uint32_t slot,
                                        const qa_q3_usercmd *command, qa_error *error)
{
    struct application_q3_guest *engine;
    q3g_client *client = client_slot(provider, slot, &engine, error);
    if (!client || !client->connected || !command || client->command_sequence == INT32_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 input requires a connected client and available sequence");
    client->command = *command;
    ++client->command_sequence;
    client->commands[(uint32_t)client->command_sequence & (QA_Q3_USERCMDS - 1)] = *command;
    if (!qa_q3_host_player_motion(engine->game->host, slot, true, error)) return false;
    int32_t argument = (int32_t)slot, result;
    bool ok = q3g_call(engine->game, 7, &argument, 1, &result, error);
    qa_error unwind = {0};
    if (!qa_q3_host_player_motion(engine->game->host, slot, false, &unwind)) {
        if (ok && error) *error = unwind;
        ok = false;
    }
    if (ok) ok = application_guest_bots_admit(provider, error);
    if (ok) ok = application_guest_clients_drain(provider, error);
    return ok;
}

bool application_q3_guest_client_command(application_provider *provider, uint32_t slot,
                                           const char *text, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    bool shutdown = engine && engine->game && engine->game->shutdown_entry && engine->calls;
    q3g_client *client = shutdown ? (slot < 64 ? &engine->clients[slot] : NULL) :
        client_slot(provider, slot, &engine, error);
    if (!client || !client->connected || !text)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 command requires a connected client");
    qa_command_tokens next = {0};
    if (!qa_command_tokenize(text, QA_CONSOLE_Q3, false, &next, error)) return false;
    qa_command_tokens prior = engine->arguments; engine->arguments = next;
    int32_t argument = (int32_t)slot, result;
    bool ok = q3g_call(engine->game, 6, &argument, 1, &result, error);
    qa_command_tokens_free(&engine->arguments); engine->arguments = prior;
    if (ok && !shutdown) ok = application_guest_bots_admit(provider, error);
    if (ok && !shutdown) ok = application_guest_clients_drain(provider, error);
    return ok;
}

bool application_q3_guest_client_command_vector(application_provider *provider, qa_actor_id actor,
    const char *const *values, size_t count, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    uint32_t slot;
    if (!engine || !engine->game || !engine->game->host || !count || !values ||
        count > SIZE_MAX / sizeof(char *) ||
        !qa_q3_host_actor_slot(engine->game->host, actor, &slot, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Source command vector requires its genuine GAME actor");
    q3g_client *client = client_slot(provider, slot, &engine, error);
    if (!client || !client->connected || !qa_actor_id_equal(client->actor, actor))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Source command vector lost its connected full actor");
    size_t storage = 0, args = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!values[i]) return application_fail(error, QA_ERROR_ARGUMENT, "Source command vector has an absent word");
        size_t length = strlen(values[i]);
        if (length == SIZE_MAX || length + 1 > SIZE_MAX - storage ||
            (i && length + (i > 1) > SIZE_MAX - args))
            return application_fail(error, QA_ERROR_MEMORY, "Source command vector exceeds its retained text extent");
        storage += length + 1;
        if (i) args += length + (i > 1);
    }
    if (args == SIZE_MAX) return application_fail(error, QA_ERROR_MEMORY, "Source command arguments exceed address space");
    qa_command_tokens next = {.count = count, .values = calloc(count, sizeof(char *)),
        .storage = malloc(storage), .args_text = malloc(args + 1)};
    if (!next.values || !next.storage || !next.args_text) {
        qa_command_tokens_free(&next);
        return application_fail(error, QA_ERROR_MEMORY, "Retaining exact Source command vector");
    }
    size_t offset = 0, tail = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t length = strlen(values[i]);
        next.values[i] = next.storage + offset;
        memcpy(next.values[i], values[i], length + 1); offset += length + 1;
        if (i) {
            if (i > 1) next.args_text[tail++] = ' ';
            memcpy(next.args_text + tail, values[i], length); tail += length;
        }
    }
    next.args_text[tail] = 0;
    qa_command_tokens prior = engine->arguments; engine->arguments = next;
    int32_t argument = (int32_t)slot, result;
    bool ok = q3g_call(engine->game, 6, &argument, 1, &result, error);
    qa_command_tokens_free(&engine->arguments); engine->arguments = prior;
    if (ok) ok = application_guest_bots_admit(provider, error);
    if (ok) ok = application_guest_clients_drain(provider, error);
    return ok;
}

void q3g_clients_clear(struct application_q3_guest *engine)
{
    for (size_t i = 0; i < 64; ++i) {
        q3g_client *client = &engine->clients[i];
        free(client->userinfo); free(client->retirement_reason); free(client->big_configstring); free(client->gamestate);
        for (size_t j = 0; j < QA_Q3_PACKET_BACKUP; ++j) free(client->snapshots[j].entities);
        *client = (q3g_client){.sensitivity = 1};
        qa_q3_reliable_init(&client->reliable);
    }
}

bool application_q3_guest_publish_snapshot(application_provider *provider, uint32_t slot,
                                             const qa_q3_snapshot *value, int32_t ping,
                                             qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || slot >= 64 || !value || !value->valid || value->message_number < 0 ||
        value->entity_count > 256 || (value->entity_count && !value->entities) ||
        value->area_bytes > sizeof(value->area_mask))
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 snapshot publication");
    q3g_client *client = &engine->clients[slot];
    if (client->has_snapshot && value->message_number <= client->snapshot_sequence)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 snapshot publication is stale");
    q3g_snapshot *snapshot = &client->snapshots[(uint32_t)value->message_number & (QA_Q3_PACKET_BACKUP - 1)];
    if (snapshot->capacity < value->entity_count) {
        qa_q3_entity *entities = realloc(snapshot->entities, value->entity_count * sizeof(*entities));
        if (!entities) return application_fail(error, QA_ERROR_MEMORY, "retaining Q3 snapshot entities");
        snapshot->entities = entities; snapshot->capacity = value->entity_count;
    }
    if (value->entity_count) memcpy(snapshot->entities, value->entities, value->entity_count * sizeof(*value->entities));
    snapshot->value = *value; snapshot->value.entities = snapshot->entities; snapshot->ping = ping;
    client->snapshot_sequence = value->message_number; client->has_snapshot = true;
    return true;
}

static q3g_client *role_client(q3g_role *role, qa_error *error)
{
    if (!role->client_engine || role->client >= 64) { application_fail(error, QA_ERROR_ARGUMENT, "Q3 client role has no source client ordinal"); return NULL; }
    return &role->client_engine->clients[role->client];
}
static const qa_q3_gamestate *gamestate(void *context)
{
    q3g_role *role = context;
    return role->client_engine && role->client < 64 ? role->client_engine->clients[role->client].gamestate : NULL;
}
static bool current_snapshot(void *context, int32_t *number, int32_t *time, qa_error *error)
{
    q3g_role *role = context; q3g_client *client = role_client(role, error); if (!client) return false;
    *number = client->has_snapshot ? client->snapshot_sequence : 0;
    *time = client->has_snapshot ? client->snapshots[(uint32_t)client->snapshot_sequence & (QA_Q3_PACKET_BACKUP - 1)].value.server_time : 0;
    return true;
}
static bool snapshot(void *context, int32_t number, const qa_q3_snapshot **out, int32_t *ping, qa_error *error)
{
    q3g_role *role = context; q3g_client *client = role_client(role, error); if (!client) return false;
    if (number > client->snapshot_sequence) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 snapshot is in the future");
    *out = NULL; *ping = 0;
    if (number < 0 || !client->has_snapshot || (int64_t)client->snapshot_sequence - number >= QA_Q3_PACKET_BACKUP) return true;
    const q3g_snapshot *value = &client->snapshots[(uint32_t)number & (QA_Q3_PACKET_BACKUP - 1)];
    if (value->value.valid && value->value.message_number == number) {
        const qa_q3_snapshot *latest = &client->snapshots[(uint32_t)client->snapshot_sequence &
            (QA_Q3_PACKET_BACKUP - 1)].value;
        uint32_t distance_bits = (uint32_t)latest->parse_entities_number +
            (uint32_t)latest->entity_count - (uint32_t)value->value.parse_entities_number;
        int32_t distance;
        memcpy(&distance, &distance_bits, sizeof(distance));
        if (distance >= 2048) return true;
        *out = &value->value; *ping = value->ping;
    }
    return true;
}

static int32_t source_integer(const char *text)
{
    while (*text == ' ' || (*text >= '\t' && *text <= '\r')) ++text;
    bool negative = *text == '-';
    if (*text == '-' || *text == '+') ++text;
    uint32_t value = 0, limit = negative ? UINT32_C(2147483648) : UINT32_C(2147483647);
    while (*text >= '0' && *text <= '9') {
        unsigned digit = (unsigned)(*text++ - '0');
        value = value > (limit - digit) / 10 ? limit : value * 10 + digit;
    }
    uint32_t bits = negative ? 0u - value : value;
    int32_t result;
    memcpy(&result, &bits, sizeof(result));
    return result;
}

static bool server_command(void *context, int32_t number, bool *present, qa_error *error)
{
    q3g_role *role = context; q3g_client *client = role_client(role, error); if (!client) return false;
    *present = false;
    if (number > client->reliable.sequence)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 server command is in the future");
    if (number <= 0) return true;
    if ((int64_t)client->reliable.sequence - number >= QA_Q3_RELIABLE)
        return application_fail(error, QA_ERROR_FORMAT, "Q3 server command has left the retained history");
    const char *text = qa_q3_reliable_lookup(&client->reliable, number);
rescan: ;
    qa_command_tokens tokens = {0};
    if (!qa_command_tokenize(text, QA_CONSOLE_Q3, false, &tokens, error)) return false;
    qa_command_tokens_free(&role->arguments); role->arguments = tokens;
    if (tokens.count && !strcmp(tokens.values[0], "disconnect")) {
        const char *reason = tokens.count >= 2 ? tokens.values[1] : "Server disconnected";
        return q3g_client_effect(role, QA_APPLICATION_Q3_DISCONNECT, reason, error);
    }
    if (tokens.count && (!strcmp(tokens.values[0], "bcs0") ||
        !strcmp(tokens.values[0], "bcs1") || !strcmp(tokens.values[0], "bcs2"))) {
        const char *fragment = tokens.count > 2 ? tokens.values[2] : "";
        if (!client->big_configstring) {
            client->big_configstring = malloc(Q3G_BIG_INFO_CHARS);
            if (!client->big_configstring)
                return application_fail(error, QA_ERROR_MEMORY, "retaining Q3 configstring continuation");
            client->big_configstring[0] = 0;
        }
        if (!strcmp(tokens.values[0], "bcs0")) {
            const char *index_text = tokens.count > 1 ? tokens.values[1] : "";
            int length = snprintf(client->big_configstring, Q3G_BIG_INFO_CHARS,
                "cs %s \"%s", index_text, fragment);
            if (length < 0)
                return application_fail(error, QA_ERROR_FORMAT, "Q3 continued configstring start cannot be encoded");
            client->big_configstring_length = strlen(client->big_configstring);
        } else {
            size_t size = strlen(fragment), quote = !strcmp(tokens.values[0], "bcs2") ? 1u : 0u;
            if (size + quote >= Q3G_BIG_INFO_CHARS - client->big_configstring_length)
                return application_fail(error, QA_ERROR_FORMAT, "Q3 continued configstring exceeds capacity");
            memcpy(client->big_configstring + client->big_configstring_length, fragment, size + 1);
            client->big_configstring_length += size;
        }
        if (!strcmp(tokens.values[0], "bcs2")) {
            client->big_configstring[client->big_configstring_length++] = '"';
            client->big_configstring[client->big_configstring_length] = 0;
            text = client->big_configstring;
            goto rescan;
        }
    } else *present = true;
    if (*present && role->arguments.count && !strcmp(role->arguments.values[0], "cs")) {
        qa_command_tokens *args = &role->arguments;
        if (!client->gamestate)
            return application_fail(error, QA_ERROR_FORMAT, "Q3 configstring command requires admitted client gamestate");
        int32_t index = source_integer(args->count > 1 ? args->values[1] : "");
        if (index < 0 || index >= QA_Q3_CONFIGSTRINGS)
            return application_fail(error, QA_ERROR_FORMAT, "invalid Q3 client configstring command index");
        const char *value = args->count > 1 ? args->args_text + strlen(args->values[1]) +
            (args->count > 2 ? 1u : 0u) : "";
        bool changed = strcmp(qa_q3_configstring(client->gamestate, (unsigned)index), value) != 0;
        if (changed && !qa_q3_configstring_set(client->gamestate, (unsigned)index, value, error)) return false;
        if (changed && index == 1) client->pending_system_info = true;
        if (index == 1 && client->pending_system_info) {
            if (!q3g_client_effect(role, QA_APPLICATION_Q3_SYSTEM_INFO,
                    qa_q3_configstring(client->gamestate, 1), error)) return false;
            client->pending_system_info = false;
        }
        qa_command_tokens restored = {0};
        if (!qa_command_tokenize(text, QA_CONSOLE_Q3, false, &restored, error)) return false;
        qa_command_tokens_free(&role->arguments); role->arguments = restored;
    }
    if (*present && role->arguments.count && !strcmp(role->arguments.values[0], "map_restart") &&
        !q3g_client_effect(role, QA_APPLICATION_Q3_MAP_RESTART, text, error)) return false;
    if (*present && role->arguments.count && !strcmp(role->arguments.values[0], "clientLevelShot") &&
        !q3g_client_effect(role, QA_APPLICATION_Q3_LEVEL_SHOT, text, error)) return false;
    if (client->gamestate && number > client->gamestate->command_sequence)
        client->gamestate->command_sequence = number;
    bool clamped;
    int32_t acknowledged = number > client->reliable.acknowledged ? number : client->reliable.acknowledged;
    if (!qa_q3_reliable_ack(&client->reliable, QA_Q3_SERVER, acknowledged, &clamped, error)) return false;
    if (number > client->consumed_server_command) client->consumed_server_command = number;
    return true;
}
static int32_t current_command(void *context)
{
    q3g_role *role = context; return role->client_engine && role->client < 64 ? role->client_engine->clients[role->client].command_sequence : 0;
}
static bool user_command(void *context, int32_t number, qa_q3_usercmd *out, bool *present, qa_error *error)
{
    q3g_role *role = context; q3g_client *client = role_client(role, error); if (!client) return false;
    if (number > client->command_sequence) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 user command is in the future");
    *present = number > 0 && (int64_t)client->command_sequence - number < QA_Q3_USERCMDS;
    if (*present) *out = client->commands[(uint32_t)number & (QA_Q3_USERCMDS - 1)];
    return true;
}
static bool command_values(void *context, int32_t weapon, float sensitivity, qa_error *error)
{
    q3g_role *role = context; q3g_client *client = role_client(role, error); if (!client) return false;
    if (!isfinite(sensitivity)) return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 command sensitivity");
    client->weapon = weapon; client->sensitivity = sensitivity; return true;
}
static bool source_actor(void *context, uint32_t number, qa_actor_id *out,
                            bool *present, qa_error *error)
{
    q3g_role *role = context;
    *out = (qa_actor_id){0}; *present = false;
    qa_q3_host_game_data data;
    if (!role->client_engine || !role->client_engine->game || !qa_q3_host_game_data_read(role->client_engine->game->host, &data) ||
        number >= data.entity_count) return true;
    if (!qa_q3_host_actor(role->client_engine->game->host, number, false, out, error)) return false;
    *present = out->registry && qa_actors_get(qa_session_actors(role->engine->provider->application->session), *out);
    return true;
}
application_provider *q3g_game_source(qa_application *application)
{
    const qa_launch_snapshot *snapshot = application->routing_snapshot;
    if (!snapshot) snapshot = qa_application_launch(application);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
    if (!choices) return NULL;
    const qa_launch_binding *binding = qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "");
    if (!binding) return NULL;
    application_provider **providers = application->routing_providers ?
        application->routing_providers : application->providers;
    size_t count = application->routing_providers ?
        application->routing_provider_count : application->provider_count;
    for (size_t i = 0; i < count; ++i) {
        application_provider *provider = providers[i];
        if (!provider || !provider->launch || strcmp(provider->launch->selection.instance, binding->instance)) continue;
        if (provider->kind == APPLICATION_PROVIDER_Q3) return provider;
        struct application_q3_guest *engine = q3g_engine(provider);
        if (engine && (engine->game || (engine->console &&
            q3g_primary_role(provider->launch->selection.artifact) == QA_QVM_GAME))) return provider;
    }
    return NULL;
}

application_provider *q3g_native_game_source(qa_application *application)
{
    application_provider *source = q3g_game_source(application);
    return source && source->kind == APPLICATION_PROVIDER_Q3 ? source : NULL;
}

bool q3g_client_bind(q3g_role *role, qa_q3_host_options *options, qa_error *error)
{
    if (role->kind == QA_QVM_GAME || options->client.gamestate) {
        if (options->client_time_from_game)
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 timing source request requires a genuine local CGAME binding");
        return true;
    }
    application_provider *source = role->client_source ? role->client_source :
        q3g_game_source(role->engine->provider->application);
    if (!source || source->application != role->engine->provider->application ||
        !source->constructed || source->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client role lacks its selected actual GAME source");
    struct application_q3_guest *source_engine = q3g_engine(source);
    const uint32_t *seats = source_engine ? source_engine->seats : role->engine->seats;
    for (uint32_t i = 0; i < 64; ++i)
        if (seats[i] == role->seat) { role->client = i; break; }
    if (role->client >= 64) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client role requires its actual GAME seat");
    if (source->kind == APPLICATION_PROVIDER_Q3) {
        if (!source->constructed || !source->state.q3 || !source->native_q3_wire || source->close_pending)
            return application_fail(error, QA_ERROR_ARGUMENT,
                "Q3 client role requires its constructed actual native GAME source");
        if (!application_native_q3_wire_client_bind(source, role->engine->provider->owner,
            role->seat, role->client, &role->arguments, &role->native_client, options, error)) return false;
        role->local_client = true;
        role->source_owner = source->owner;
        role->client_source = source;
    } else {
        if (!source_engine || !source_engine->game || !source_engine->game->host ||
            source_engine->world != role->engine->world ||
            (source_engine != role->engine && source_engine->client_leases == SIZE_MAX))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client binding lost its actual original GAME host");
        role->local_client = true;
        role->source_owner = source->owner;
        role->client_source = source;
        role->client_engine = source_engine;
        if (source_engine != role->engine) ++source_engine->client_leases;
        options->client = (qa_q3_host_client_services){.context = role,
            .gamestate = gamestate, .current_snapshot = current_snapshot, .snapshot = snapshot,
            .server_command = server_command, .current_command = current_command,
            .user_command = user_command, .command_values = command_values, .source_actor = source_actor};
    }
    if (options->client_time_from_game || options->client_time_cvars) {
        qa_cvars *actual = source->kind == APPLICATION_PROVIDER_Q3 ?
            application_native_q3_console_registry(source) : NULL;
        if (source_engine && source_engine->game)
            qa_q3_host_console(source_engine->game->host, &actual, NULL);
        if (!actual || (options->client_time_from_game &&
            (role->kind != QA_QVM_CGAME || options->client_time_cvars || options->client_time_owner)) ||
            (!options->client_time_from_game && (options->client_time_cvars != actual ||
                options->client_time_owner != role->source_owner)))
            return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client timing authority requires its explicit actual GAME registry");
        options->client_time_cvars = actual;
        options->client_time_owner = role->source_owner;
        options->client_time_from_game = false;
    }
    return true;
}

bool application_q3_guest_client_gamestate(application_provider *provider, uint32_t slot,
                                            const qa_q3_gamestate **out, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || !out || slot >= 64 || !engine->clients[slot].connected || !engine->clients[slot].gamestate)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 local client gamestate is not admitted");
    *out = engine->clients[slot].gamestate; return true;
}

bool q3g_arsenal_client_admit(application_provider *provider, uint32_t slot, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    qa_application *app = provider ? provider->application : NULL;
    if (!engine || !app || slot >= 64 || !engine->game || !engine->game->vm || !app->q3_client_prepare)
        return true;
    q3g_client *client = engine->clients + slot;
    if (!client->connected || !client->begun || client->pending_retirement || client->bot) return true;
    bool hud = application_provider_for(app, client->actor, QA_ROLE_HUD, "") == provider;
    if (!hud && application_provider_for(app, client->actor, QA_ROLE_ARSENAL, "") != provider) return true;
    uint32_t seat;
    if (!qa_application_player_seat(app, client->actor, &seat) || engine->seats[slot] != seat ||
        engine->calls || engine->restore_pending || !engine->map_ready || !client->gamestate)
        return application_fail(error, QA_ERROR_ARGUMENT, "Arsenal CG admission lost its actual GAME client and seat");
    q3g_role *cgame = NULL, *ui = NULL;
    for (q3g_role *r = engine->roles; r; r = r->next) if (r->seat == seat) {
        if (r->kind == QA_QVM_CGAME) {
            if (cgame) return application_fail(error, QA_ERROR_ARGUMENT, "Arsenal CG has ambiguous actual seat owners");
            cgame = r;
        } else if (r->kind == QA_QVM_UI) {
            if (ui) return application_fail(error, QA_ERROR_ARGUMENT, "Arsenal UI has ambiguous actual seat owners");
            ui = r;
        }
    }
    if (!cgame) {
        bool found; uint64_t size;
        if (!qa_vfs_probe(provider->launch->content, "cgame-weapon-models.json", &found, &size, error)) return false;
        if (!found && !hud) return true;
        qa_resource *declaration = NULL; qa_vfs_acquisition acquisition = {0};
        qa_json_document *document = NULL; qa_buffer path = {0}; char *normalized = NULL;
        bool ok = !found || (qa_vfs_acquire_receipt(provider->launch->content, "cgame-weapon-models.json",
            &declaration, &acquisition, error) && qa_json_parse(qa_resource_bytes(declaration), &document, error) &&
            qa_json_string(document, qa_json_get(document, qa_json_root(document), "artifactPath"), &path, error));
        if (ok && found && memchr(path.data, 0, path.size))
            ok = application_fail(error, QA_ERROR_FORMAT, "Arsenal CG declaration path contains a NUL");
        if (ok && found) { normalized = qa_vfs_normalize_path((const char *)path.data, error); ok = normalized != NULL; }
        if (ok) ok = q3g_role_create_client(engine, QA_QVM_CGAME, seat,
            found ? normalized : "vm/cgame.qvm", false, provider, &cgame, error);
        free(normalized); qa_buffer_free(&path); qa_json_destroy(document);
        qa_vfs_acquisition_dispose(&acquisition); qa_resource_release(declaration);
        if (!ok) return false;
        cgame->next = engine->roles; engine->roles = cgame;
    }
    if (!hud && cgame->ready && !cgame->retired && cgame->artifact &&
        !cgame->artifact->weapon_models_profile.present && !cgame->weapon_models) return true;
    if (!cgame->ready || cgame->retired || !cgame->artifact || cgame->client_source != provider ||
        cgame->client_engine != engine || cgame->client != slot ||
        ((!hud || cgame->artifact->weapon_models_profile.present) && !cgame->weapon_models))
        return application_fail(error, QA_ERROR_ARGUMENT, "Arsenal CG changed its genuine matching GAME and registry");
    if (!ui) {
        if (!application_guest_q3_source_ui_create(engine, seat, &ui, error)) return false;
        ui->next = engine->roles; engine->roles = ui;
    }
    if (ui->client_source != provider || ui->client_engine != engine || ui->client != slot)
        return application_fail(error, QA_ERROR_ARGUMENT, "Arsenal source UI changed its actual matching GAME");
    if (cgame->initialized)
        return cgame->init_succeeded || application_fail(error, QA_ERROR_ARGUMENT, "Arsenal CG retains a failed actual Init");
    int32_t message, time;
    if (!cgame->client_services.current_snapshot(cgame->client_services.context, &message, &time, error)) return false;
    const qa_q3_gamestate *state = cgame->client_services.gamestate(cgame->client_services.context);
    if (!state || state != client->gamestate)
        return application_fail(error, QA_ERROR_ARGUMENT, "Arsenal CG lost its admitted GAME gamestate");
    return application_q3_guest_role_initialize(provider, QA_QVM_CGAME, seat,
        message, state->command_sequence, state->client_number, true, error);
}
const qa_q3_gamestate *application_q3_guest_server_gamestate(application_provider *provider)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    return engine && engine->map_ready ? &engine->gamestate : NULL;
}
