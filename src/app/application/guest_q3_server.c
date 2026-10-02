#include "guest_q3_private.h"
#include "guest_projection_private.h"
#include "native_q3_wire_state.h"
#include "guest_q3_combat.h"

char *q3g_copy_text(const char *text, qa_error *error)
{
    size_t length = strlen(text);
    char *copy = malloc(length + 1);
    if (!copy) { application_fail(error, QA_ERROR_MEMORY, "retaining Q3 service text"); return NULL; }
    memcpy(copy, text, length + 1); return copy;
}

bool q3g_arguments(void *context, qa_native_host_command_view *out, qa_error *error)
{
    q3g_role *role = context;
    if (role->native_client && !role->arguments_scoped) {
        if (!application_native_q3_wire_client_arguments(role->native_client, out, error)) return false;
    } else if (role->kind != QA_QVM_GAME && !role->local_client && !role->arguments_scoped && role->common.arguments) {
        if (!role->common.arguments(role->common.context, out, error)) return false;
    } else {
        qa_command_tokens *args = role->kind == QA_QVM_GAME ? &role->engine->arguments : &role->arguments;
        *out = (qa_native_host_command_view){args->count, (const char *const *)args->values,
            args->args_text ? args->args_text : ""};
    }
    out->canonical_configstrings = role->kind == QA_QVM_CGAME && !role->arguments_scoped;
    return true;
}

static void print(void *context, const char *text)
{
    q3g_role *role = context;
    if (role->common.print) role->common.print(role->common.context, text);
}
static uint32_t milliseconds(void *context)
{
    q3g_role *role = context;
    return role->common.milliseconds ? role->common.milliseconds(role->common.context) :
        (uint32_t)role->engine->milliseconds;
}
static int32_t calendar(void *context, qa_q3_host_calendar *out)
{
    q3g_role *role = context;
    if (role->common.calendar) return role->common.calendar(role->common.context, out);
    if (out) *out = (qa_q3_host_calendar){0};
    return -1;
}
static bool installed_mods(void *context, qa_vfs_listing *out, qa_error *error)
{
    q3g_role *role = context;
    return role->common.installed_mods ? role->common.installed_mods(role->common.context, out, error) :
        application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 installed mod enumeration is unbound");
}
static bool clipboard(void *context, qa_buffer *out, qa_error *error)
{
    q3g_role *role = context;
    return role->common.clipboard ? role->common.clipboard(role->common.context, out, error) :
        application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 clipboard is unbound");
}
static bool client_command(void *context, const char *text, qa_error *error)
{
    q3g_role *role = context;
    if (role->native_client)
        return application_native_q3_wire_client_command(role->native_client, text, error);
    if (role->common.client_command) return role->common.client_command(role->common.context, text, error);
    if (role->client_engine && role->client < 64)
        return application_q3_guest_client_command(role->client_engine->provider, role->client, text, error);
    return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 seat has no connected command recipient");
}
static bool configstring(void *context, uint32_t index, const char **out, qa_error *error)
{
    q3g_role *role = context;
    if (index >= QA_Q3_CONFIGSTRINGS) return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 configstring");
    *out = qa_q3_configstring(&role->engine->gamestate, index); return true;
}
static bool queue_room(const q3g_client *client, size_t count, qa_error *error)
{
    int64_t outstanding = (int64_t)client->reliable.sequence - client->reliable.acknowledged;
    return (outstanding >= 0 && (uint64_t)outstanding + count <= QA_Q3_RELIABLE &&
            count <= (size_t)(INT32_MAX - client->reliable.sequence)) ||
        application_fail(error, QA_ERROR_FORMAT, "Q3 reliable command capacity exhausted");
}
static bool command_recipient(const struct application_q3_guest *engine, size_t slot)
{
    return !engine->clients[slot].pending_retirement && (engine->clients[slot].connected ||
        (engine->round.phase != Q3G_ROUND_NONE && engine->round.phase != Q3G_ROUND_FAILED &&
         (engine->round.carried & (UINT64_C(1) << slot))));
}
static bool retained_command_recipient(const struct application_q3_guest *engine, size_t slot)
{
    if (!command_recipient(engine, slot)) return false;
    const q3g_client *client = &engine->clients[slot];
    if (client->bot) return true;
    const qa_application *app = engine->provider->application;
    for (const application_provider *provider = app->live_providers; provider; provider = provider->next_live) {
        const struct application_q3_guest *receiver = q3g_engine((application_provider *)provider);
        if (!receiver) continue;
        for (const q3g_role *role = receiver->roles; role; role = role->next)
            if (role->kind == QA_QVM_CGAME && role->local_client && role->client_engine == engine &&
                role->client == slot && role->host && role->ready && !role->retired) return true;
    }
    return false;
}
static bool send_command(void *context, int32_t client, const char *text, qa_error *error)
{
    q3g_role *role = context;
    if (client < -1 || client >= 64 || strlen(text) >= QA_Q3_COMMAND_CHARS)
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 server command recipient or size");
    for (size_t i = 0; i < 64; ++i)
        if ((client < 0 || i == (size_t)client) && retained_command_recipient(role->engine, i) &&
            !queue_room(&role->engine->clients[i], 1, error)) return false;
    /* A real transport owner receives the same reliable command. */
    if (role->server.send_command &&
        !role->server.send_command(role->server.context, client, text, error)) return false;
    for (size_t i = 0; i < 64; ++i)
        if ((client < 0 || i == (size_t)client) && retained_command_recipient(role->engine, i) &&
            !qa_q3_reliable_add(&role->engine->clients[i].reliable, QA_Q3_SERVER, text, error)) return false;
    return true;
}
bool q3g_set_configstring(q3g_role *role, uint32_t index, const char *text, qa_error *error)
{
    if (index >= QA_Q3_CONFIGSTRINGS || !text)
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 configstring update");
    const char *prior = qa_q3_configstring(&role->engine->gamestate, index);
    if (!strcmp(prior, text)) return true;
    size_t length = strlen(text), chunk = QA_Q3_COMMAND_CHARS - 24;
    if (length >= QA_Q3_GAMESTATE_CHARS)
        return application_fail(error, QA_ERROR_FORMAT, "Q3 configstring exceeds gamestate capacity");
    size_t count = length <= chunk ? 1 : (length + chunk - 1) / chunk;
    for (size_t i = 0; i < 64; ++i)
        if (retained_command_recipient(role->engine, i) && !queue_room(&role->engine->clients[i], count, error)) return false;
    if (!qa_q3_configstring_set(&role->engine->gamestate, index, text, error)) return false;
    char command[QA_Q3_COMMAND_CHARS];
    if (length <= chunk) {
        snprintf(command, sizeof(command), "cs %u \"%s\"", index, text);
        return send_command(role, -1, command, error);
    }
    for (size_t offset = 0; offset < length; offset += chunk) {
        size_t size = length - offset < chunk ? length - offset : chunk;
        const char *name = !offset ? "bcs0" : offset + size == length ? "bcs2" : "bcs1";
        snprintf(command, sizeof(command), "%s %u \"%.*s\"", name, index, (int)size, text + offset);
        if (!send_command(role, -1, command, error)) return false;
    }
    return true;
}

static bool set_configstring(void *context, uint32_t index, const char *text, qa_error *error)
{
    return q3g_set_configstring(context, index, text, error);
}
static bool userinfo(void *context, uint32_t client, const char **out, qa_error *error)
{
    q3g_role *role = context;
    if (client >= 64) return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 userinfo client");
    *out = role->engine->clients[client].userinfo ? role->engine->clients[client].userinfo : ""; return true;
}
static bool set_userinfo(void *context, uint32_t client, const char *text, qa_error *error)
{
    q3g_role *role = context;
    if (client >= 64 || !text) return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 userinfo update");
    char *copy = q3g_copy_text(text, error); if (!copy) return false;
    if (role->server.set_userinfo && !role->server.set_userinfo(role->server.context, client, text, error)) {
        free(copy); return false;
    }
    free(role->engine->clients[client].userinfo); role->engine->clients[client].userinfo = copy; return true;
}
static bool user_command(void *context, uint32_t client, qa_q3_usercmd *out, qa_error *error)
{
    q3g_role *role = context;
    if (client >= 64) return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 user command client");
    *out = role->engine->clients[client].command; return true;
}
static bool drop_client(void *context, uint32_t client, const char *reason, qa_error *error)
{
    q3g_role *role = context;
    if (client >= 64) return application_fail(error, QA_ERROR_ARGUMENT, "invalid dropped Q3 client");
    if (role->server.drop_client &&
        !role->server.drop_client(role->server.context, client, reason, error)) return false;
    return application_guest_client_drop(role->engine->provider, client, reason, error);
}
static bool allocate_bot(void *context, int32_t *out, qa_error *error)
{
    q3g_role *role = context;
    bool allocated = role->server.allocate_bot ?
        role->server.allocate_bot(role->server.context, out, error) :
        application_guest_bot_allocate(role->engine->provider, out, error);
    if (!allocated) return false;
    if (*out == -1) return true;
    if (*out < 0 || *out >= 64 || role->engine->clients[*out].allocated ||
        role->engine->seats[*out] != UINT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Q3 bot allocator returned an unavailable source slot");
    qa_actor_id actor;
    if (!qa_q3_host_actor(role->host, (uint32_t)*out, false, &actor, error) || !actor.registry ||
        !qa_actors_get(qa_session_actors(role->engine->provider->application->session), actor))
        return application_fail(error, QA_ERROR_FORMAT, "Q3 bot allocator returned no canonical source actor");
    role->engine->clients[*out].actor = actor;
    role->engine->clients[*out].pending_bot = true;
    role->engine->clients[*out].allocated = role->engine->clients[*out].bot = true;
    return true;
}
static bool free_bot(void *context, int32_t client, qa_error *error)
{
    q3g_role *role = context;
    if (client < 0 || client >= 64)
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid freed Q3 bot client");
    if (!role->engine->clients[client].bot) return true;
    if (role->server.free_bot && !role->server.free_bot(role->server.context, client, error)) return false;
    return application_guest_bot_free(role->engine->provider, client, error);
}
static bool bot_snapshot_entity(void *context, int32_t client, int32_t sequence, int32_t *out, qa_error *error)
{
    q3g_role *role = context;
    if (role->server.bot_snapshot_entity)
        return role->server.bot_snapshot_entity(role->server.context, client, sequence, out, error);
    if (client < 0 || client >= 64 || sequence < 0)
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 bot snapshot entity");
    q3g_client *source = &role->engine->clients[client];
    const q3g_snapshot *snapshot = &source->snapshots[(uint32_t)source->snapshot_sequence & (QA_Q3_PACKET_BACKUP - 1)];
    *out = source->has_snapshot && (size_t)sequence < snapshot->value.entity_count ?
        snapshot->value.entities[sequence].number : -1; return true;
}
static bool bot_console_message(void *context, int32_t client, const char **out, qa_error *error)
{
    q3g_role *role = context;
    if (role->server.bot_console_message)
        return role->server.bot_console_message(role->server.context, client, out, error);
    if (client < 0 || client >= 64) return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 bot console client");
    q3g_client *source = &role->engine->clients[client];
    *out = NULL;
    if (source->consumed_server_command < source->reliable.sequence) {
        *out = qa_q3_reliable_lookup(&source->reliable, ++source->consumed_server_command);
        bool clamped;
        return qa_q3_reliable_ack(&source->reliable, QA_Q3_SERVER,
                                  source->consumed_server_command, &clamped, error);
    }
    return true;
}
static bool bot_user_command(void *context, int32_t client, const qa_q3_usercmd *command, qa_error *error)
{
    q3g_role *role = context;
    if (role->server.bot_user_command)
        return role->server.bot_user_command(role->server.context, client, command, error);
    if (client < 0 || client >= 64 || !role->engine->clients[client].bot)
        return application_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 bot user command");
    if (!application_guest_bots_admit(role->engine->provider, error)) return false;
    bool prior = role->engine->round.source_entry;
    role->engine->round.source_entry = true;
    bool ok = application_q3_guest_client_think(role->engine->provider, (uint32_t)client, command, error);
    role->engine->round.source_entry = prior;
    return ok;
}
static bool admit_actor(void *context, qa_actor_id actor, qa_error *error)
{
    q3g_role *role = context;
    bool admitted = role->server.admit_actor ? role->server.admit_actor(role->server.context, actor, error) :
        application_guest_actor_admit(role->engine->provider, actor, error);
    return admitted && (!role->combat || application_q3_combat_admit(role->combat, actor, error));
}
static bool player_velocity(void *context, qa_actor_id actor, qa_vec3 velocity, qa_error *error)
{
    q3g_role *role = context;
    return role->server.player_velocity ? role->server.player_velocity(role->server.context, actor, velocity, error) :
        application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 canonical player velocity owner is unbound");
}
static qa_actor_id world_actor(void *context)
{
    q3g_role *role = context;
    return role->server.world_actor ? role->server.world_actor(role->server.context) : (qa_actor_id){0};
}
void q3g_server_bind(q3g_role *role, qa_q3_host_options *options)
{
    role->common = options->common; role->server = options->server;
    options->common = (qa_q3_host_common_services){role, print, milliseconds, calendar,
        q3g_arguments, client_command, installed_mods, clipboard};
    if (role->kind != QA_QVM_GAME) return;
    options->server = (qa_q3_host_server_services){role, 64, configstring, set_configstring,
        userinfo, set_userinfo, user_command, drop_client, send_command, allocate_bot, free_bot,
        bot_snapshot_entity, bot_console_message, bot_user_command, admit_actor, player_velocity, world_actor};
}
