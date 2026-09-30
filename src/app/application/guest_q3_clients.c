#include "guest_q3_private.h"
#include "guest_projection_private.h"
#include "qa/application_players.h"

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

static q3g_client *client_slot(application_provider *provider, uint32_t slot,
                               struct application_q3_guest **engine, qa_error *error)
{
    *engine = q3g_engine(provider);
    if (!*engine || !(*engine)->game || !(*engine)->game->initialized || slot >= 64) {
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
    if (!*accepted && !client->pending_retirement &&
        !application_guest_client_drop(provider, slot, "", error)) return false;
    return application_guest_bots_admit(provider, error) && application_guest_clients_drain(provider, error);
}

bool application_q3_guest_client_begin(application_provider *provider, uint32_t slot, qa_error *error)
{
    struct application_q3_guest *engine;
    q3g_client *client = client_slot(provider, slot, &engine, error);
    if (!client || !client->connected) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client has not connected");
    int32_t argument = (int32_t)slot, result;
    if (!q3g_call(engine->game, 3, &argument, 1, &result, error)) return false;
    if (!client->connected || client->pending_retirement ||
        !qa_actors_get(qa_session_actors(provider->application->session), client->actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client retired during source begin");
    client->begun = true;
    if (!application_guest_actor_admit(provider, client->actor, error)) return false;
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
    struct application_q3_guest *engine;
    q3g_client *client = client_slot(provider, slot, &engine, error);
    if (!client || !client->connected || !text)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 command requires a connected client");
    qa_command_tokens next = {0};
    if (!qa_command_tokenize(text, QA_CONSOLE_Q3, false, &next, error)) return false;
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
    if (role->client >= 64) { application_fail(error, QA_ERROR_ARGUMENT, "Q3 client role has no source client ordinal"); return NULL; }
    return &role->engine->clients[role->client];
}
static const qa_q3_gamestate *gamestate(void *context)
{
    q3g_role *role = context;
    return role->client < 64 ? role->engine->clients[role->client].gamestate : NULL;
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
    if (value->value.valid && value->value.message_number == number) { *out = &value->value; *ping = value->ping; }
    return true;
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
    qa_command_tokens tokens = {0};
    if (!qa_command_tokenize(text, QA_CONSOLE_Q3, false, &tokens, error)) return false;
    qa_command_tokens_free(&role->arguments); role->arguments = tokens;
    if (tokens.count && !strcmp(tokens.values[0], "disconnect")) {
        const char *reason = tokens.count >= 2 ? tokens.values[1] : "Server disconnected";
        return q3g_client_effect(role, QA_APPLICATION_Q3_DISCONNECT, reason, error);
    }
    if (tokens.count && !strncmp(tokens.values[0], "bcs", 3)) {
        if (tokens.count != 3 || (strcmp(tokens.values[0], "bcs0") &&
            strcmp(tokens.values[0], "bcs1") && strcmp(tokens.values[0], "bcs2")))
            return application_fail(error, QA_ERROR_FORMAT, "invalid Q3 configstring continuation");
        char *end; unsigned long index = strtoul(tokens.values[1], &end, 10);
        if (*end || index >= QA_Q3_CONFIGSTRINGS) return application_fail(error, QA_ERROR_FORMAT, "invalid Q3 continued configstring index");
        if (!strcmp(tokens.values[0], "bcs0")) {
            if (!client->big_configstring) {
                client->big_configstring = malloc(QA_Q3_GAMESTATE_CHARS);
                if (!client->big_configstring) return application_fail(error, QA_ERROR_MEMORY, "retaining Q3 configstring continuation");
            }
            client->big_configstring_length = 0; client->big_configstring_index = (uint32_t)index;
            client->big_configstring_active = true;
        }
        if (!client->big_configstring_active || client->big_configstring_index != index)
            return application_fail(error, QA_ERROR_FORMAT, "Q3 configstring continuation has no start");
        size_t size = strlen(tokens.values[2]);
        if (size >= QA_Q3_GAMESTATE_CHARS - client->big_configstring_length)
            return application_fail(error, QA_ERROR_FORMAT, "Q3 continued configstring exceeds capacity");
        memcpy(client->big_configstring + client->big_configstring_length, tokens.values[2], size + 1);
        client->big_configstring_length += size;
        if (!strcmp(tokens.values[0], "bcs2")) {
            size_t capacity = client->big_configstring_length + 32;
            char *command = malloc(capacity);
            if (!command) return application_fail(error, QA_ERROR_MEMORY, "publishing Q3 configstring continuation");
            snprintf(command, capacity, "cs %lu \"%s\"", index, client->big_configstring);
            qa_command_tokens complete = {0};
            bool ok = qa_command_tokenize(command, QA_CONSOLE_Q3, false, &complete, error); free(command);
            if (!ok) return false;
            qa_command_tokens_free(&role->arguments); role->arguments = complete;
            client->big_configstring_active = false; *present = true;
        }
    } else *present = true;
    if (*present && role->arguments.count && !strcmp(role->arguments.values[0], "cs")) {
        qa_command_tokens *args = &role->arguments;
        if (args->count != 3 || !client->gamestate)
            return application_fail(error, QA_ERROR_FORMAT, "Q3 configstring command requires admitted client gamestate");
        char *end; unsigned long index = strtoul(args->values[1], &end, 10);
        if (!*args->values[1] || *end || index >= QA_Q3_CONFIGSTRINGS)
            return application_fail(error, QA_ERROR_FORMAT, "invalid Q3 client configstring command index");
        bool changed = strcmp(qa_q3_configstring(client->gamestate, (unsigned)index), args->values[2]) != 0;
        if (changed && !qa_q3_configstring_set(client->gamestate, (unsigned)index, args->values[2], error)) return false;
        if (changed && index == 1) client->pending_system_info = true;
        if (index == 1 && client->pending_system_info) {
            if (!q3g_client_effect(role, QA_APPLICATION_Q3_SYSTEM_INFO,
                    qa_q3_configstring(client->gamestate, 1), error)) return false;
            client->pending_system_info = false;
        }
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
    if (number > client->consumed_server_command) client->consumed_server_command = number; return true;
}
static int32_t current_command(void *context)
{
    q3g_role *role = context; return role->client < 64 ? role->engine->clients[role->client].command_sequence : 0;
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
    if (!role->engine->game || !qa_q3_host_game_data_read(role->engine->game->host, &data) ||
        number >= data.entity_count) return true;
    if (!qa_q3_host_actor(role->engine->game->host, number, false, out, error)) return false;
    *present = out->registry && qa_actors_get(qa_session_actors(role->engine->provider->application->session), *out);
    return true;
}
bool q3g_client_bind(q3g_role *role, qa_q3_host_options *options, qa_error *error)
{
    if (role->kind == QA_QVM_GAME || options->client.gamestate) return true;
    for (uint32_t i = 0; i < 64; ++i)
        if (role->engine->seats[i] == role->seat) { role->client = i; break; }
    if (role->client >= 64) return application_fail(error, QA_ERROR_ARGUMENT, "Q3 client role requires a configured seat");
    role->local_client = true;
    options->client = (qa_q3_host_client_services){role, gamestate, current_snapshot, snapshot,
        server_command, current_command, user_command, command_values, source_actor};
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
const qa_q3_gamestate *application_q3_guest_server_gamestate(application_provider *provider)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    return engine && engine->map_ready ? &engine->gamestate : NULL;
}
