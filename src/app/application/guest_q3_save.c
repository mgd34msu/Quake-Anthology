#include "guest_q3_save.h"
#include "guest_checkpoint.h"
#include "guest_input_private.h"
#include "guest_projection_private.h"
#include "bots_private.h"
#include "map_players_private.h"
#include "portals.h"
#include "qa/qvm_save.h"
#include "qa/q3_host_save.h"
#include "qa/q3_abi.h"
#include "qa/source_save.h"

static bool state_fail(qa_source_save_io *io, qa_status status, const char *message)
{
    if (!io->failed) qa_error_set(io->error, status, io->offset, "%s", message);
    io->failed = true;
    return false;
}

static bool state_signature(qa_source_save_io *io)
{
    uint8_t magic[8] = {'Q','A','G','3','S','T',0,0};
    const uint8_t expected[8] = {'Q','A','G','3','S','T',0,0};
    uint32_t version = 1;
    return qa_source_save_bytes(io, magic, sizeof(magic)) &&
        qa_source_save_u32(io, &version) &&
        ((!memcmp(magic, expected, sizeof(magic)) && version == 1) ||
         state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 state signature"));
}

static bool owned_text(qa_source_save_io *io, char **text)
{
    bool present = *text != NULL;
    if (!qa_source_save_bool(io, &present) || !present) return !io->failed;
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? strlen(*text) + 1 : 0;
    if (!qa_source_save_count(io, &length, SIZE_MAX) || !length)
        return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 text extent");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (length > io->input.size - io->offset)
            return state_fail(io, QA_ERROR_FORMAT, "Truncated application Q3 text");
        *text = malloc(length);
        if (!*text) return state_fail(io, QA_ERROR_MEMORY, "Restoring application Q3 text");
    }
    return qa_source_save_bytes(io, *text, length) &&
        ((!(*text)[length - 1] && !memchr(*text, 0, length - 1)) ||
         state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 text terminator"));
}

static bool tokens(qa_source_save_io *io, qa_command_tokens *value)
{
    if (!qa_source_save_count(io, &value->count, 1024)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (value->count > (io->input.size - io->offset) / 9)
            return state_fail(io, QA_ERROR_FORMAT, "Truncated application Q3 token inventory");
        value->values = value->count ? calloc(value->count, sizeof(*value->values)) : NULL;
        value->storage = value->count ? malloc(9216) : NULL;
        if (value->count && (!value->values || !value->storage))
            return state_fail(io, QA_ERROR_MEMORY, "Restoring application Q3 arguments");
    } else if (value->count && (!value->values || !value->storage)) {
        return state_fail(io, QA_ERROR_FORMAT, "Application Q3 token storage is absent");
    }
    size_t used = 0;
    for (size_t i = 0; i < value->count; ++i) {
        char *text = io->direction == QA_SOURCE_SAVE_WRITE ? value->values[i] : NULL;
        if (!owned_text(io, &text)) {
            if (io->direction == QA_SOURCE_SAVE_READ) free(text);
            return false;
        }
        if (!text) return state_fail(io, QA_ERROR_FORMAT, "Application Q3 token is absent");
        size_t length = strlen(text) + 1;
        if (length > 9216 - used) {
            if (io->direction == QA_SOURCE_SAVE_READ) free(text);
            return state_fail(io, QA_ERROR_FORMAT, "Application Q3 tokens exceed the source extent");
        }
        if (io->direction == QA_SOURCE_SAVE_READ) {
            value->values[i] = value->storage + used;
            memcpy(value->values[i], text, length);
            free(text);
        }
        used += length;
    }
    if (!owned_text(io, &value->args_text)) return false;
    if (!value->args_text)
        return !value->count || state_fail(io, QA_ERROR_FORMAT, "Application Q3 token arguments are absent");
    size_t offset = 0;
    for (size_t i = 1; i < value->count; ++i) {
        if (i > 1 && value->args_text[offset++] != ' ')
            return state_fail(io, QA_ERROR_FORMAT, "Application Q3 arguments differ from their tokens");
        size_t length = strlen(value->values[i]);
        if (strncmp(value->args_text + offset, value->values[i], length))
            return state_fail(io, QA_ERROR_FORMAT, "Application Q3 arguments differ from their tokens");
        offset += length;
    }
    return !value->args_text[offset] ||
        state_fail(io, QA_ERROR_FORMAT, "Application Q3 arguments have an unowned suffix");
}

static bool record_write(void *context, size_t offset, qa_bytes bytes, qa_error *error)
{
    (void)error;
    memcpy((uint8_t *)context + offset, bytes.data, bytes.size);
    return true;
}

static bool entity(qa_source_save_io *io, qa_q3_entity *value)
{
    uint8_t bytes[208] = {0};
    qa_q3_abi_record record = {.abi = QA_QVM_Q3_MODERN,
        .bytes = {bytes, sizeof(bytes)}, .context = bytes, .write = record_write};
    return (io->direction != QA_SOURCE_SAVE_WRITE ||
            qa_q3_abi_write_entity(&record, 0, true, value, io->error)) &&
        qa_source_save_bytes(io, bytes, sizeof(bytes)) &&
        (io->direction != QA_SOURCE_SAVE_READ ||
         qa_q3_abi_read_entity(&record, 0, true, value, io->error));
}

static bool player(qa_source_save_io *io, qa_q3_player *value)
{
    uint8_t bytes[468] = {0};
    uint32_t product = value->product;
    qa_q3_abi_record record = {.abi = QA_QVM_Q3_MODERN,
        .bytes = {bytes, sizeof(bytes)}, .context = bytes, .write = record_write};
    if (!qa_source_save_u32(io, &product) || product > QA_Q3_TEAM_ARENA)
        return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 player product");
    bool ok = (io->direction != QA_SOURCE_SAVE_WRITE ||
               qa_q3_abi_write_player(&record, 0, true, false, value, io->error)) &&
        qa_source_save_bytes(io, bytes, sizeof(bytes)) &&
        (io->direction != QA_SOURCE_SAVE_READ ||
         qa_q3_abi_read_player(&record, 0, true, value, io->error));
    if (ok && io->direction == QA_SOURCE_SAVE_READ) value->product = (qa_q3_product)product;
    return ok;
}

static bool command(qa_source_save_io *io, qa_q3_usercmd *value)
{
    uint8_t bytes[24] = {0};
    qa_q3_abi_record record = {.abi = QA_QVM_Q3_MODERN,
        .bytes = {bytes, sizeof(bytes)}, .context = bytes, .write = record_write};
    return (io->direction != QA_SOURCE_SAVE_WRITE ||
            qa_q3_abi_write_usercmd(&record, 0, false, value, io->error)) &&
        qa_source_save_bytes(io, bytes, sizeof(bytes)) &&
        (io->direction != QA_SOURCE_SAVE_READ ||
         qa_q3_abi_read_usercmd(&record, 0, value, io->error));
}

static bool gamestate(qa_source_save_io *io, qa_q3_gamestate *value)
{
    bool ok = qa_source_save_i32(io, &value->command_sequence) &&
        qa_source_save_i32(io, &value->client_number) &&
        qa_source_save_i32(io, &value->checksum_feed) &&
        qa_source_save_count(io, &value->string_bytes, QA_Q3_GAMESTATE_CHARS) &&
        qa_source_save_bytes(io, value->strings, sizeof(value->strings));
    if (!ok) return false;
    if (!value->string_bytes || value->strings[0])
        return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 gamestate strings");
    for (size_t i = 0; i < QA_Q3_CONFIGSTRINGS; ++i)
        if (!qa_source_save_u16(io, &value->config_offsets[i]) ||
            !qa_q3_configstring(value, (unsigned)i))
            return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 configstring offset");
    for (size_t i = 0; i < QA_Q3_ENTITIES; ++i)
        if (!qa_source_save_bool(io, &value->baseline_present[i]) || !entity(io, &value->baselines[i]))
            return false;
    return true;
}

static bool snapshot(qa_source_save_io *io, q3g_snapshot *slot, size_t ordinal)
{
    qa_q3_snapshot *value = &slot->value;
    bool ok = qa_source_save_bool(io, &value->valid) &&
        qa_source_save_i32(io, &value->message_number) &&
        qa_source_save_i32(io, &value->server_time) &&
        qa_source_save_i32(io, &value->delta_number) &&
        qa_source_save_i32(io, &value->server_command_number) &&
        qa_source_save_u64(io, &value->parse_entities_number) &&
        qa_source_save_u8(io, &value->flags) && qa_source_save_u8(io, &value->area_bytes) &&
        qa_source_save_bytes(io, value->area_mask, sizeof(value->area_mask)) &&
        player(io, &value->player) && qa_source_save_count(io, &value->entity_count, 256) &&
        qa_source_save_count(io, &slot->capacity, 256) && qa_source_save_i32(io, &slot->ping);
    if (!ok) return false;
    if (value->area_bytes > sizeof(value->area_mask) || value->entity_count > slot->capacity ||
        (value->valid && (value->message_number < 0 ||
         ((uint32_t)value->message_number & (QA_Q3_PACKET_BACKUP - 1)) != ordinal)))
        return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 snapshot history identity");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (slot->capacity > (io->input.size - io->offset) / 208)
            return state_fail(io, QA_ERROR_FORMAT, "Truncated application Q3 snapshot entities");
        slot->entities = slot->capacity ? calloc(slot->capacity, sizeof(*slot->entities)) : NULL;
        if (slot->capacity && !slot->entities)
            return state_fail(io, QA_ERROR_MEMORY, "Restoring application Q3 snapshot entities");
        value->entities = slot->entities;
    } else if ((slot->capacity && !slot->entities) || value->entities != slot->entities) {
        return state_fail(io, QA_ERROR_FORMAT, "Application Q3 snapshot entity storage differs");
    }
    for (size_t i = 0; i < slot->capacity; ++i)
        if (!entity(io, &slot->entities[i])) return false;
    return true;
}

static bool client(qa_source_save_io *io, q3g_client *value)
{
    bool ok = qa_source_save_actor(io, &value->actor) && owned_text(io, &value->userinfo) &&
        owned_text(io, &value->retirement_reason) && command(io, &value->command);
    for (size_t i = 0; ok && i < QA_Q3_USERCMDS; ++i) ok = command(io, &value->commands[i]);
    ok = ok && qa_source_save_i32(io, &value->command_sequence) &&
        qa_source_save_i32(io, &value->snapshot_sequence) &&
        qa_source_save_i32(io, &value->consumed_server_command) &&
        qa_source_save_i32(io, &value->server_id) && qa_source_save_u64(io, &value->entered_ns) &&
        qa_source_save_i32(io, &value->reliable.sequence) &&
        qa_source_save_i32(io, &value->reliable.acknowledged) &&
        qa_source_save_bytes(io, value->reliable.text, sizeof(value->reliable.text));
    if (!ok) return false;
    int64_t outstanding = (int64_t)value->reliable.sequence - value->reliable.acknowledged;
    if (value->command_sequence < 0 || value->snapshot_sequence < 0 ||
        value->reliable.sequence < 0 || value->reliable.acknowledged < 0 ||
        outstanding < 0 || outstanding > QA_Q3_RELIABLE ||
        value->consumed_server_command < 0 || value->consumed_server_command > value->reliable.sequence)
        return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 client sequence");
    for (size_t i = 0; i < QA_Q3_RELIABLE; ++i)
        if (!memchr(value->reliable.text[i], 0, QA_Q3_COMMAND_CHARS))
            return state_fail(io, QA_ERROR_FORMAT, "Unterminated application Q3 reliable command");
    bool present = value->gamestate != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (present && io->direction == QA_SOURCE_SAVE_READ) {
        if (io->input.size - io->offset < 12 + 8 + QA_Q3_GAMESTATE_CHARS +
            QA_Q3_CONFIGSTRINGS * 2 + QA_Q3_ENTITIES * 209)
            return state_fail(io, QA_ERROR_FORMAT, "Truncated application Q3 client gamestate");
        value->gamestate = calloc(1, sizeof(*value->gamestate));
        if (!value->gamestate) return state_fail(io, QA_ERROR_MEMORY, "Restoring application Q3 client gamestate");
    }
    if (present && !gamestate(io, value->gamestate)) return false;
    for (size_t i = 0; i < QA_Q3_PACKET_BACKUP; ++i)
        if (!snapshot(io, &value->snapshots[i], i)) return false;
    if (!qa_source_save_f32(io, &value->sensitivity) || !qa_source_save_i32(io, &value->weapon) ||
        !qa_source_save_u32(io, &value->big_configstring_index) ||
        !qa_source_save_count(io, &value->big_configstring_length, QA_Q3_GAMESTATE_CHARS - 1) ||
        !qa_source_save_bool(io, &value->big_configstring_active)) return false;
    present = value->big_configstring != NULL;
    if (!qa_source_save_bool(io, &present)) return false;
    if (present && io->direction == QA_SOURCE_SAVE_READ) {
        if (value->big_configstring_length + 1 > io->input.size - io->offset)
            return state_fail(io, QA_ERROR_FORMAT, "Truncated application Q3 continued configstring");
        value->big_configstring = malloc(QA_Q3_GAMESTATE_CHARS);
        if (!value->big_configstring)
            return state_fail(io, QA_ERROR_MEMORY, "Restoring application Q3 continued configstring");
    }
    if (present && (!qa_source_save_bytes(io, value->big_configstring, value->big_configstring_length + 1) ||
        value->big_configstring[value->big_configstring_length] ||
        memchr(value->big_configstring, 0, value->big_configstring_length)))
        return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 continued configstring");
#define FLAG(name) if (!qa_source_save_bool(io, &value->name)) return false
    FLAG(allocated); FLAG(connected); FLAG(begun); FLAG(bot); FLAG(has_snapshot);
    FLAG(pending_system_info); FLAG(pending_bot); FLAG(pending_retirement);
    FLAG(disconnect_pending); FLAG(disconnect_started); FLAG(roster_attached);
#undef FLAG
    if (!isfinite(value->sensitivity) || (value->big_configstring_active &&
        (!value->big_configstring || value->big_configstring_index >= QA_Q3_CONFIGSTRINGS)) ||
        (!value->big_configstring && value->big_configstring_length) ||
        (value->connected && (!value->actor.registry || !value->allocated)) ||
        (value->has_snapshot && (!value->snapshots[(uint32_t)value->snapshot_sequence &
         (QA_Q3_PACKET_BACKUP - 1)].value.valid ||
         value->snapshots[(uint32_t)value->snapshot_sequence & (QA_Q3_PACKET_BACKUP - 1)].value.message_number !=
         value->snapshot_sequence)))
        return state_fail(io, QA_ERROR_FORMAT, "Invalid application Q3 client continuation");
    return true;
}

static bool fields(qa_source_save_io *io, struct application_q3_guest *engine)
{
    uint32_t product = engine->product;
    bool ok = state_signature(io) && qa_source_save_u32(io, &product) &&
        product == engine->product && qa_source_save_u64(io, &engine->role_sequence) &&
        qa_source_save_i32(io, &engine->milliseconds) && qa_source_save_bool(io, &engine->map_ready) &&
        owned_text(io, &engine->entity_text) && tokens(io, &engine->arguments) && gamestate(io, &engine->gamestate);
    if (!ok) return state_fail(io, QA_ERROR_FORMAT, "Application Q3 state product or envelope differs");
    for (size_t i = 0; i < 64; ++i)
        if (!qa_source_save_u32(io, &engine->seats[i]) || !client(io, &engine->clients[i])) return false;
    return true;
}

static bool owner(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    return (engine && !engine->calls && !engine->draining_clients &&
        qa_session_safe(provider->application->session) && application_q3_guest_idle(provider)) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Application Q3 state requires an idle source owner");
}

bool application_guest_q3_state_capture(application_provider *provider, qa_buffer *out, qa_error *error)
{
    if (!out || !owner(provider, error)) return false;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, provider->application->session, error) &&
        fields(&io, q3g_engine(provider)) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

bool application_guest_q3_state_restore(application_provider *provider, qa_bytes bytes, qa_error *error)
{
    if (!owner(provider, error)) return false;
    struct application_q3_guest *engine = q3g_engine(provider);
    if (provider->application->operation != APPLICATION_PERSISTING)
        return application_fail(error, QA_ERROR_ARGUMENT, "Application Q3 state requires an isolated persistence candidate");
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->initialized)
            return application_fail(error, QA_ERROR_ARGUMENT, "Application Q3 state requires unpublished source roles");
    for (size_t i = 0; i < 64; ++i) {
        const q3g_client *value = &engine->clients[i];
        if (value->actor.registry || value->userinfo || value->retirement_reason || value->gamestate ||
            value->big_configstring || value->allocated || value->connected)
            return application_fail(error, QA_ERROR_ARGUMENT, "Application Q3 state requires empty candidate clients");
        for (size_t j = 0; j < QA_Q3_PACKET_BACKUP; ++j)
            if (value->snapshots[j].entities)
                return application_fail(error, QA_ERROR_ARGUMENT, "Application Q3 state requires empty candidate history");
    }
    struct application_q3_guest *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) return application_fail(error, QA_ERROR_MEMORY, "Restoring application Q3 client state");
    candidate->product = engine->product;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, provider->application->session, bytes, error) &&
        fields(&io, candidate) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok && ((engine->entity_text != NULL) != (candidate->entity_text != NULL) ||
        (engine->entity_text && strcmp(engine->entity_text, candidate->entity_text))))
        ok = application_fail(error, QA_ERROR_FORMAT, "Application Q3 entity text differs from its prepared immutable map");
    if (ok) {
        q3g_clients_clear(engine);
        memcpy(engine->clients, candidate->clients, sizeof(engine->clients));
        memset(candidate->clients, 0, sizeof(candidate->clients));
        qa_command_tokens_free(&engine->arguments);
        engine->arguments = candidate->arguments; candidate->arguments = (qa_command_tokens){0};
        /* Hosts borrow this exact prepared allocation. Keep it stable while
         * their parser and executor continuations are imported separately. */
        engine->gamestate = candidate->gamestate;
        memcpy(engine->seats, candidate->seats, sizeof(engine->seats));
        engine->milliseconds = candidate->milliseconds;
        engine->map_ready = candidate->map_ready;
        engine->role_sequence = candidate->role_sequence;
    }
    q3g_clients_clear(candidate); qa_command_tokens_free(&candidate->arguments);
    free(candidate->entity_text); free(candidate);
    return ok;
}

enum { ROLE_INITIALIZED = 1, ROLE_RETIRED = 2, ROLE_READY = 4,
       ROLE_PRIMARY = 8, ROLE_LOCAL_CLIENT = 16, ROLE_COMMITTED = 32 };

typedef struct saved_artifact {
    char *path;
    uint32_t kind, abi;
    qa_sha256_digest digest, primary_digest;
    q3g_artifact *actual;
} saved_artifact;
typedef struct saved_projection {
    qa_actor_id actor;
    uint32_t slot;
    uint64_t serial;
    bool bound;
} saved_projection;
typedef struct saved_role {
    size_t artifact;
    uint32_t seat, client, flags;
    uint64_t sequence;
    qa_string_id owner;
    bool keys[256];
    qa_command_tokens arguments;
    saved_projection *projections;
    size_t projection_count;
    qa_bytes input, executor, services;
    qa_buffer input_storage, executor_storage, service_storage;
    q3g_role *actual;
} saved_role;
typedef struct q3g_restore {
    qa_buffer storage, state_storage;
    qa_bytes state;
    char *entity_text;
    uint32_t product;
    uint64_t sequence;
    size_t game, artifact_count, role_count;
    uint32_t seats[64];
    saved_artifact *artifacts;
    saved_role *roles;
    bool owns_text, imported;
} q3g_restore;

static void saved_free(q3g_restore *saved)
{
    if (!saved) return;
    for (size_t i = 0; saved->artifacts && i < saved->artifact_count; ++i)
        if (saved->owns_text) free(saved->artifacts[i].path);
    for (size_t i = 0; saved->roles && i < saved->role_count; ++i) {
        saved_role *role = &saved->roles[i];
        if (saved->owns_text) qa_command_tokens_free(&role->arguments);
        free(role->projections);
        qa_buffer_free(&role->input_storage);
        qa_buffer_free(&role->executor_storage);
        qa_buffer_free(&role->service_storage);
    }
    if (saved->owns_text) free(saved->entity_text);
    free(saved->artifacts); free(saved->roles);
    qa_buffer_free(&saved->state_storage); qa_buffer_free(&saved->storage);
    free(saved);
}

void application_guest_q3_save_clear(struct application_q3_guest *engine)
{
    if (!engine) return;
    saved_free(engine->restoration); engine->restoration = NULL;
}

static bool blob(qa_source_save_io *io, qa_bytes *bytes, size_t minimum)
{
    size_t length = bytes->size;
    if (!qa_source_save_count(io, &length, SIZE_MAX)) return false;
    if (length < minimum)
        return state_fail(io, QA_ERROR_FORMAT, "Q3 continuation nested owner is absent or truncated");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (length > io->input.size - io->offset)
            return state_fail(io, QA_ERROR_FORMAT, "Q3 continuation nested owner exceeds its enclosing record");
        *bytes = (qa_bytes){io->input.data + io->offset, length};
        io->offset += length;
        return true;
    }
    return qa_source_save_bytes(io, (void *)bytes->data, length);
}

static uint32_t role_flags(const q3g_role *role)
{
    uint32_t flags = (role->initialized ? ROLE_INITIALIZED : 0) |
        (role->retired ? ROLE_RETIRED : 0) | (role->ready ? ROLE_READY : 0) |
        (role->primary ? ROLE_PRIMARY : 0) | (role->local_client ? ROLE_LOCAL_CLIENT : 0) |
        (role->committed ? ROLE_COMMITTED : 0);
    const q3g_restore *pending = role->engine->restoration;
    if (pending && role->engine->restore_pending)
        for (size_t i = 0; i < pending->role_count; ++i)
            if (pending->roles[i].actual == role)
                flags |= pending->roles[i].flags & ROLE_INITIALIZED;
    return flags;
}

static bool portable_executor(qa_bytes bytes, qa_error *error)
{
    if (!bytes.data || bytes.size < 160 || memcmp(bytes.data, "QAVM", 4) ||
        qa_load_u32le(bytes.data + 4) != 2)
        return application_fail(error, QA_ERROR_FORMAT, "Q3 continuation has no complete original QVM executor");
    uint64_t memory = qa_load_u64le(bytes.data + 24), host = qa_load_u64le(bytes.data + 32);
    if (memory > bytes.size - 160 || host != bytes.size - 160 - (size_t)memory)
        return application_fail(error, QA_ERROR_FORMAT, "Q3 executor memory/host extents differ");
    return qa_q3_host_checkpoint_portable_state((qa_bytes){bytes.data + 160 + (size_t)memory, (size_t)host}, error);
}

static bool saved_fields(qa_source_save_io *io, q3g_restore *saved)
{
    uint8_t magic[8] = {'Q','A','G','3','P','V',0,0};
    const uint8_t expected[8] = {'Q','A','G','3','P','V',0,0};
    uint32_t version = 1;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || !qa_source_save_u32(io, &version) ||
        memcmp(magic, expected, sizeof(magic)) || version != 1 ||
        !qa_source_save_u32(io, &saved->product) || saved->product > QA_Q3_TEAM_ARENA ||
        !qa_source_save_u64(io, &saved->sequence) || !owned_text(io, &saved->entity_text) ||
        !blob(io, &saved->state, 12) ||
        !qa_source_save_count(io, &saved->artifact_count, SIZE_MAX / sizeof(*saved->artifacts)))
        return state_fail(io, QA_ERROR_FORMAT, "Invalid coupled Q3 provider envelope");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (!saved->artifact_count || saved->artifact_count > (io->input.size - io->offset) / 82)
            return state_fail(io, QA_ERROR_FORMAT, "Invalid Q3 artifact inventory extent");
        saved->artifacts = calloc(saved->artifact_count, sizeof(*saved->artifacts));
        if (!saved->artifacts) return state_fail(io, QA_ERROR_MEMORY, "Restoring Q3 artifact inventory");
    }
    for (size_t i = 0; i < saved->artifact_count; ++i) {
        saved_artifact *artifact = &saved->artifacts[i];
        if (!owned_text(io, &artifact->path) || !artifact->path || !*artifact->path ||
            !qa_source_save_u32(io, &artifact->kind) || artifact->kind > QA_QVM_UI ||
            !qa_source_save_u32(io, &artifact->abi) || artifact->abi > QA_QVM_Q3_116N ||
            !qa_source_save_bytes(io, artifact->digest.bytes, 32) ||
            !qa_source_save_bytes(io, artifact->primary_digest.bytes, 32))
            return state_fail(io, QA_ERROR_FORMAT, "Invalid Q3 source artifact declaration");
        for (size_t j = 0; j < i; ++j)
            if (artifact->kind == saved->artifacts[j].kind && !strcmp(artifact->path, saved->artifacts[j].path))
                return state_fail(io, QA_ERROR_FORMAT, "Duplicate Q3 immutable source artifact");
    }
    if (!qa_source_save_count(io, &saved->role_count, 129) || !saved->role_count ||
        !qa_source_save_count(io, &saved->game, saved->role_count))
        return state_fail(io, QA_ERROR_FORMAT, "Invalid Q3 source role inventory");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (saved->role_count > (io->input.size - io->offset) / 300)
            return state_fail(io, QA_ERROR_FORMAT, "Truncated Q3 source role inventory");
        saved->roles = calloc(saved->role_count, sizeof(*saved->roles));
        if (!saved->roles) return state_fail(io, QA_ERROR_MEMORY, "Restoring Q3 source role inventory");
    }
    size_t primaries = 0, games = 0;
    for (size_t i = 0; i < saved->role_count; ++i) {
        saved_role *role = &saved->roles[i];
        if (!qa_source_save_count(io, &role->artifact, saved->artifact_count - 1) ||
            !qa_source_save_u32(io, &role->seat) || !qa_source_save_u32(io, &role->client) ||
            !qa_source_save_u64(io, &role->sequence) || !role->sequence || role->sequence > saved->sequence ||
            !qa_source_save_u32(io, &role->owner) || !role->owner ||
            !qa_source_save_u32(io, &role->flags) || role->flags > 63 || !(role->flags & ROLE_READY) ||
            ((role->flags & ROLE_INITIALIZED) &&
             (!(role->flags & ROLE_COMMITTED) || (role->flags & ROLE_RETIRED))))
            return state_fail(io, QA_ERROR_FORMAT, "Invalid Q3 source role identity or lifecycle");
        const saved_artifact *artifact = &saved->artifacts[role->artifact];
        primaries += (role->flags & ROLE_PRIMARY) != 0;
        games += artifact->kind == QA_QVM_GAME;
        if ((artifact->kind == QA_QVM_GAME) != (saved->game == i + 1) ||
            (artifact->kind == QA_QVM_GAME && !(role->flags & ROLE_PRIMARY)) ||
            ((role->flags & ROLE_LOCAL_CLIENT) ? artifact->kind == QA_QVM_GAME || role->client >= 64 : role->client != UINT32_MAX))
            return state_fail(io, QA_ERROR_FORMAT, "Q3 source role changes its actual game/client authority");
        for (size_t j = 0; j < i; ++j) {
            const saved_role *prior = &saved->roles[j];
            if (role->sequence == prior->sequence || role->owner == prior->owner ||
                (artifact->kind == saved->artifacts[prior->artifact].kind && role->seat == prior->seat))
                return state_fail(io, QA_ERROR_FORMAT, "Duplicate Q3 source role owner, generation or seat");
        }
        for (size_t j = 0; j < 256; ++j)
            if (!qa_source_save_bool(io, &role->keys[j])) return false;
        if (!tokens(io, &role->arguments) || !blob(io, &role->input, 13) ||
            !blob(io, &role->services, 12) ||
            !qa_source_save_count(io, &role->projection_count, SIZE_MAX / sizeof(*role->projections))) return false;
        if (io->direction == QA_SOURCE_SAVE_READ && role->projection_count) {
            if (role->projection_count > (io->input.size - io->offset) / 26)
                return state_fail(io, QA_ERROR_FORMAT, "Truncated Q3 source projection inventory");
            role->projections = calloc(role->projection_count, sizeof(*role->projections));
            if (!role->projections) return state_fail(io, QA_ERROR_MEMORY, "Restoring Q3 source projection inventory");
        }
        for (size_t j = 0; j < role->projection_count; ++j) {
            saved_projection *projection = &role->projections[j];
            if (!qa_source_save_actor(io, &projection->actor) || !projection->actor.registry ||
                !qa_source_save_u32(io, &projection->slot) || projection->slot >= 64 ||
                !qa_source_save_bool(io, &projection->bound) ||
                !qa_source_save_u64(io, &projection->serial) || (projection->bound && !projection->serial))
                return state_fail(io, QA_ERROR_FORMAT, "Invalid Q3 source projection actor or lease");
            for (size_t k = 0; k < j; ++k)
                if (qa_actor_id_equal(role->projections[k].actor, projection->actor))
                    return state_fail(io, QA_ERROR_FORMAT, "Duplicate Q3 source projection actor");
        }
        if (!blob(io, &role->executor, 160) || !portable_executor(role->executor, io->error)) return false;
    }
    return (primaries == 1 && games <= 1) ||
        state_fail(io, QA_ERROR_FORMAT, "Q3 source inventory has no unique primary/game role");
}

static bool saved_collect(application_provider *provider, q3g_restore **out, qa_error *error)
{
    if (provider->kind != APPLICATION_PROVIDER_QVM)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Native Q3 module private data requires its actual relocation owner");
    struct application_q3_guest *engine = q3g_engine(provider);
    q3g_restore *saved = calloc(1, sizeof(*saved));
    if (!saved) return application_fail(error, QA_ERROR_MEMORY, "Capturing Q3 source inventory");
    saved->product = engine->product; saved->sequence = engine->role_sequence;
    saved->entity_text = engine->entity_text;
    for (q3g_artifact *a = engine->artifacts; a; a = a->next) ++saved->artifact_count;
    for (q3g_role *r = engine->roles; r; r = r->next) ++saved->role_count;
    if (!saved->artifact_count || !saved->role_count || saved->role_count > 129 ||
        saved->artifact_count > SIZE_MAX / sizeof(*saved->artifacts)) {
        saved_free(saved); return application_fail(error, QA_ERROR_FORMAT, "Invalid actual Q3 source inventory extent");
    }
    saved->artifacts = calloc(saved->artifact_count, sizeof(*saved->artifacts));
    saved->roles = calloc(saved->role_count, sizeof(*saved->roles));
    if (!saved->artifacts || !saved->roles) {
        /* Counts describe allocated cleanup inventories only. */
        if (!saved->artifacts) saved->artifact_count = 0;
        if (!saved->roles) saved->role_count = 0;
        saved_free(saved); return application_fail(error, QA_ERROR_MEMORY, "Capturing Q3 source inventory rows");
    }
    size_t i = 0;
    for (q3g_artifact *a = engine->artifacts; a; a = a->next, ++i) {
        if (!a->qvm || !a->image || a->module || a->declaration) {
            saved_free(saved); return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 native artifact cache requires its actual module owner");
        }
        saved->artifacts[i] = (saved_artifact){.path = a->path, .kind = a->kind,
            .abi = a->abi, .digest = *qa_qvm_image_digest(a->image), .actual = a};
        qa_sha256((qa_bytes){a->primary.data, a->primary.size}, &saved->artifacts[i].primary_digest);
    }
    i = 0;
    for (q3g_role *r = engine->roles; r; r = r->next, ++i) {
        saved_role *row = &saved->roles[i];
        if (!r->vm || !r->image || r->native || r->module || r->activation_failed || r->arguments_scoped) {
            saved_free(saved); return application_fail(error, QA_ERROR_UNSUPPORTED, "Q3 source role has an unqualified native executor or command scope");
        }
        size_t a = 0;
        while (a < saved->artifact_count && saved->artifacts[a].actual != r->artifact) ++a;
        if (a == saved->artifact_count) {
            saved_free(saved); return application_fail(error, QA_ERROR_FORMAT, "Q3 source role has no retained artifact owner");
        }
        *row = (saved_role){.artifact = a, .seat = r->seat, .client = r->client,
            .flags = role_flags(r), .sequence = r->service_sequence, .owner = r->service_owner,
            .arguments = r->arguments, .actual = r};
        memcpy(row->keys, r->input_keys, sizeof(row->keys));
        if (r == engine->game) saved->game = i + 1;
        for (guest_projection_actor *p = r->projection ? r->projection->actors : NULL; p; p = p->next)
            ++row->projection_count;
        if (row->projection_count > SIZE_MAX / sizeof(*row->projections)) {
            saved_free(saved); return application_fail(error, QA_ERROR_MEMORY, "Q3 source projection inventory is too large");
        }
        row->projections = row->projection_count ? calloc(row->projection_count, sizeof(*row->projections)) : NULL;
        if (row->projection_count && !row->projections) {
            saved_free(saved); return application_fail(error, QA_ERROR_MEMORY, "Capturing Q3 source projection leases");
        }
        size_t j = 0;
        for (guest_projection_actor *p = r->projection ? r->projection->actors : NULL; p; p = p->next, ++j) {
            if (p->projection != r->projection || p->inventory_prepared ||
                (p->inventory_bound && !qa_inventory_primary_current(provider->application->inventory,
                    p->inventory_lease, p)) ||
                (p->inventory_lease.serial ? !qa_actor_id_equal(p->actor, p->inventory_lease.actor) : p->inventory_lease.actor.registry != 0)) {
                saved_free(saved); return application_fail(error, QA_ERROR_FORMAT, "Q3 source projection has no complete private lease");
            }
            row->projections[j] = (saved_projection){p->actor, p->slot, p->inventory_lease.serial, p->inventory_bound};
        }
        if (!qa_q3_host_checkpoint_services(r->host, &row->service_storage, error) ||
            !application_guest_input_checkpoint(r, &row->input_storage, error) ||
            !qa_qvm_checkpoint(r->vm, &row->executor_storage, error)) { saved_free(saved); return false; }
        row->services = (qa_bytes){row->service_storage.data, row->service_storage.size};
        row->input = (qa_bytes){row->input_storage.data, row->input_storage.size};
        row->executor = (qa_bytes){row->executor_storage.data, row->executor_storage.size};
    }
    if (!application_guest_q3_state_capture(provider, &saved->state_storage, error)) { saved_free(saved); return false; }
    saved->state = (qa_bytes){saved->state_storage.data, saved->state_storage.size};
    *out = saved; return true;
}

static bool clients_agree(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    const struct application_player_roster *roster = provider->application->players;
    for (uint32_t i = 0; i < 64; ++i) {
        const q3g_client *client = engine->clients + i;
        if (!client->actor.registry) continue;
        const qa_actor_record *actor = qa_actors_get(qa_session_actors(provider->application->session), client->actor);
        if (!actor) {
            if (!client->pending_retirement)
                return application_fail(error, QA_ERROR_FORMAT, "Q3 source client has unqualified retired actor provenance");
            continue;
        }
        uint32_t slot;
        if (!engine->game || !qa_q3_host_actor_slot(engine->game->host, client->actor, &slot, error) || slot != i)
            return application_fail(error, QA_ERROR_FORMAT, "Q3 source client disagrees with its actual host actor slot");
        bool retired;
        if (!qa_q3_host_checkpoint_input_retired(engine->game->host, i, &retired, error) ||
            retired != client->pending_retirement)
            return application_fail(error, QA_ERROR_FORMAT, "Q3 pending source retirement changes its original input admission");
        if (client->connected && !client->bot) {
            uint32_t seat;
            if (!qa_application_player_seat(provider->application, client->actor, &seat) || engine->seats[i] != seat)
                return application_fail(error, QA_ERROR_FORMAT, "Q3 source human client changes its canonical roster seat");
        }
        if (client->roster_attached) {
            bool admitted = false;
            for (size_t j = 0; roster && j < roster->count; ++j) {
                const application_player_record *player = roster->records + j;
                if (!qa_actor_id_equal(player->actor, client->actor)) continue;
                for (size_t k = 0; k < player->guest_count; ++k)
                    if (player->guests[k].owner == provider->owner && player->guests[k].source_slot == i &&
                        qa_sha256_equal(&player->guests[k].identity, &provider->launch->identity)) admitted = true;
            }
            if (!admitted)
                return application_fail(error, QA_ERROR_FORMAT, "Q3 source bot attachment is absent from its exact saved roster");
        }
    }
    return true;
}

static bool capture_body(application_provider *provider, qa_buffer *out, qa_error *error)
{
    if (!out || !owner(provider, error)) return false;
    q3g_restore *saved = NULL;
    qa_source_save_io io = {0};
    bool ok = clients_agree(provider, error) && saved_collect(provider, &saved, error) &&
        qa_source_save_writer(&io, provider->application->session, error) &&
        saved_fields(&io, saved) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); saved_free(saved);
    return ok;
}

bool application_guest_q3_save_capture(application_provider *provider, qa_buffer *out, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine || engine->restore_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source continuation is not published");
    return capture_body(provider, out, error);
}

static bool equal_bytes(qa_bytes a, qa_bytes b)
{
    return a.size == b.size && (!a.size || !memcmp(a.data, b.data, a.size));
}

static bool prepare_artifact(application_provider *provider, struct application_q3_guest *engine,
                              saved_artifact *saved, bool primary, qa_error *error)
{
    q3g_artifact *artifact = calloc(1, sizeof(*artifact));
    if (!artifact) return application_fail(error, QA_ERROR_MEMORY, "Restoring immutable Q3 artifact owner");
    artifact->path = q3g_copy_text(saved->path, error);
    if (!artifact->path) { free(artifact); return false; }
    artifact->kind = (qa_qvm_role)saved->kind; artifact->qvm = true;
    /* Attach before source qualification so every partial retained image has
     * the same lifetime as the isolated provider. */
    q3g_artifact **tail = &engine->artifacts;
    while (*tail) tail = &(*tail)->next;
    *tail = artifact; saved->actual = artifact;
    qa_qvm_compatibility compatibility = {0};
    bool ok;
    if (primary) {
        artifact->image = provider->state.qvm.image;
        qa_qvm_image_retain(artifact->image);
        ok = provider->launch->declaration ?
            qa_qvm_compatibility_parse(qa_resource_bytes(provider->launch->declaration), saved->path,
                qa_qvm_image_digest(artifact->image), artifact->kind, &compatibility, error) :
            qa_qvm_compatibility_read(provider->launch->content, saved->path,
                qa_qvm_image_digest(artifact->image), artifact->kind, &compatibility, error);
    } else {
        ok = qa_qvm_image_open(provider->launch->content, saved->path, artifact->kind,
            &artifact->image, &compatibility, error);
    }
    if (ok) {
        qa_sha256_digest primary_digest;
        qa_sha256((qa_bytes){compatibility.primary.data, compatibility.primary.size}, &primary_digest);
        ok = compatibility.abi == (qa_qvm_abi)saved->abi &&
            qa_sha256_equal(qa_qvm_image_digest(artifact->image), &saved->digest) &&
            qa_sha256_equal(&primary_digest, &saved->primary_digest);
        if (!ok) application_fail(error, QA_ERROR_FORMAT, "Q3 artifact or source declaration differs from saved content");
    }
    if (ok) {
        artifact->abi = compatibility.abi;
        artifact->primary = compatibility.primary; compatibility.primary = (qa_buffer){0};
    }
    qa_qvm_compatibility_free(&compatibility);
    return ok;
}

static bool qualify_base(application_provider *provider, qa_world *world, q3g_restore *saved,
                          const qa_product *product, const qa_launch_choices *choices, qa_error *error)
{
    struct application_q3_guest *base = calloc(1, sizeof(*base));
    if (!base) return application_fail(error, QA_ERROR_MEMORY, "Qualifying Q3 application source state");
    base->product = !strcmp(product->campaign, "missionpack") ? QA_Q3_TEAM_ARENA : QA_Q3_ARENA;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, provider->application->session, saved->state, error) &&
        fields(&io, base) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (ok && (saved->product != (uint32_t)base->product || saved->sequence != base->role_sequence ||
        ((saved->entity_text != NULL) != (base->entity_text != NULL)) ||
        (saved->entity_text && strcmp(saved->entity_text, base->entity_text))))
        ok = application_fail(error, QA_ERROR_FORMAT, "Q3 provider envelope changes its nested source identity");
    for (size_t i = 0; ok && i < choices->seat_count; ++i) {
        if (base->seats[i] != choices->seats[i].id)
            ok = application_fail(error, QA_ERROR_FORMAT, "Q3 source seat inventory differs from restored configuration");
    }
    if (ok && base->entity_text) {
        const qa_bsp_view *map = qa_collision_bsp(qa_world_geometry(world));
        qa_bytes source = map ? map->lumps[QA_BSP_ENTITIES].bytes : (qa_bytes){0};
        const uint8_t *end = source.size ? memchr(source.data, 0, source.size) : NULL;
        size_t length = end ? (size_t)(end - source.data) : source.size;
        if (!map || strlen(base->entity_text) != length ||
            (length && memcmp(base->entity_text, source.data, length)))
            ok = application_fail(error, QA_ERROR_FORMAT, "Q3 entity parser belongs to different immutable map source text");
    }
    if (ok) memcpy(saved->seats, base->seats, sizeof(saved->seats));
    q3g_clients_clear(base); qa_command_tokens_free(&base->arguments);
    free(base->entity_text); free(base);
    return ok;
}

bool application_guest_q3_save_prepare(application_provider *provider, qa_world *world,
    const qa_product *product, const qa_launch_choices *choices, const qa_save_record *record, qa_error *error)
{
    if (!provider || !provider->application || !provider->launch || !product || !choices || !record ||
        !world || product->family != QA_GAME_Q3 || choices->seat_count > 64 ||
        provider->kind != APPLICATION_PROVIDER_QVM || !provider->state.qvm.image || q3g_engine(provider) ||
        provider->application->operation != APPLICATION_PERSISTING ||
        record->owner.kind != QA_SAVE_PROVIDER || !record->owner.instance ||
        strcmp(record->owner.instance, provider->launch->selection.instance) ||
        !qa_sha256_equal(&record->owner.content, &provider->launch->identity) ||
        !record->owner.schema || strcmp(record->owner.schema, "qa.q3.qvm") ||
        record->owner.schema_version != 1 || !record->owner.backend || strcmp(record->owner.backend, "qvm"))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 restored construction requires its qualified provider record");
    qa_bytes payload = record->payload, body;
    if (!payload.data || payload.size < 32 || memcmp(payload.data, "QAPV", 4) ||
        qa_load_u32le(payload.data + 4) != 1 || qa_load_u32le(payload.data + 8) != APPLICATION_PROVIDER_QVM ||
        qa_load_u32le(payload.data + 12) > 1 || qa_load_u64le(payload.data + 24) != payload.size - 32 ||
        !application_guest_checkpoint_body(provider, (qa_bytes){payload.data + 32, payload.size - 32}, &body, error))
        return application_fail(error, QA_ERROR_FORMAT, "Q3 restored provider wrapper differs from its actual source");
    q3g_restore *saved = calloc(1, sizeof(*saved));
    if (!saved) return application_fail(error, QA_ERROR_MEMORY, "Retaining Q3 provider constructor state");
    saved->owns_text = true; saved->storage.data = malloc(body.size); saved->storage.size = body.size;
    if (!saved->storage.data) { saved_free(saved); return application_fail(error, QA_ERROR_MEMORY, "Retaining Q3 provider bytes"); }
    memcpy(saved->storage.data, body.data, body.size);
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, provider->application->session,
        (qa_bytes){saved->storage.data, saved->storage.size}, error) &&
        saved_fields(&io, saved) && qa_source_save_finish(&io, NULL) &&
        qualify_base(provider, world, saved, product, choices, error);
    qa_source_save_dispose(&io);
    size_t primary = SIZE_MAX;
    for (size_t i = 0; ok && i < saved->role_count; ++i) {
        saved_role *role = saved->roles + i;
        saved_artifact *artifact = saved->artifacts + role->artifact;
        if (role->flags & ROLE_PRIMARY) {
            primary = role->artifact;
            if (strcmp(artifact->path, provider->launch->selection.artifact) ||
                artifact->kind != (uint32_t)q3g_primary_role(provider->launch->selection.artifact) ||
                role->seat != (choices->seat_count ? choices->seats[0].id : UINT32_MAX))
                ok = application_fail(error, QA_ERROR_FORMAT, "Q3 primary role differs from its selected source artifact");
        }
        char identity[65], name[160]; qa_sha256_hex(&provider->launch->identity, identity);
        snprintf(name, sizeof(name), "q3-service:%u:%s:%llu", provider->owner, identity,
            (unsigned long long)role->sequence);
        if (qa_strings_find(qa_session_strings(provider->application->session),
            (qa_bytes){(const uint8_t *)name, strlen(name)}) != role->owner)
            ok = application_fail(error, QA_ERROR_FORMAT, "Q3 role lifetime owner leaves its actual restored string registry");
    }
    if (!ok) { saved_free(saved); return false; }
    if (!application_guest_q3_create_empty(provider->application, provider, world, product, choices, true, error)) {
        saved_free(saved); return false;
    }
    struct application_q3_guest *engine = q3g_engine(provider);
    engine->restoration = saved; engine->role_sequence = saved->sequence;
    memcpy(engine->seats, saved->seats, sizeof(engine->seats));
    if (saved->entity_text && !(engine->entity_text = q3g_copy_text(saved->entity_text, error))) return false;
    for (size_t i = 0; i < saved->artifact_count; ++i)
        if (!prepare_artifact(provider, engine, saved->artifacts + i, i == primary, error)) return false;
    /* Service heaps were originally admitted in registration order, even
     * though the role list itself is prepended by ordinary role creation. */
    for (size_t n = 0; n < saved->role_count; ++n) {
        saved_role *next = NULL;
        for (size_t i = 0; i < saved->role_count; ++i)
            if (!saved->roles[i].actual && (!next || saved->roles[i].sequence < next->sequence)) next = saved->roles + i;
        saved_artifact *artifact = saved->artifacts + next->artifact;
        q3g_role *role = NULL;
        if (!q3g_role_create_restored(engine, (qa_qvm_role)artifact->kind, next->seat, artifact->path,
            (next->flags & ROLE_PRIMARY) != 0, next->sequence, next->owner, &role, error)) return false;
        next->actual = role; role->next = engine->roles; engine->roles = role;
        if (role->client != next->client || role->local_client != ((next->flags & ROLE_LOCAL_CLIENT) != 0))
            return application_fail(error, QA_ERROR_FORMAT, "Q3 role changes its actual admitted local or remote client services");
        if (artifact->kind == QA_QVM_GAME) {
            engine->game = role; q3g_game_aliases(engine, role);
            if ((next->flags & ROLE_COMMITTED) && application_bots_runtime(provider->application) &&
                !application_bots_guest_bind(provider->application, provider, role->host, error)) return false;
        }
    }
    engine->roles = saved->roles[0].actual;
    for (size_t i = 0; i < saved->role_count; ++i)
        saved->roles[i].actual->next = i + 1 < saved->role_count ? saved->roles[i + 1].actual : NULL;
    return true;
}

bool application_guest_q3_save_restore(application_provider *provider, qa_bytes bytes, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    q3g_restore *saved = engine ? engine->restoration : NULL;
    if (!saved || saved->imported || !engine->restore_pending || !owner(provider, error) ||
        provider->application->operation != APPLICATION_PERSISTING ||
        !equal_bytes(bytes, (qa_bytes){saved->storage.data, saved->storage.size}))
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source import requires its isolated prepared record");
    if (!application_guest_q3_state_restore(provider, saved->state, error)) return false;
    for (size_t i = 0; i < saved->role_count; ++i) {
        saved_role *row = saved->roles + i; q3g_role *role = row->actual;
        if (!portable_executor(row->executor, error) ||
            !application_guest_input_restore(role, row->input, error) ||
            !qa_qvm_restore_candidate(role->vm, row->executor, error)) return false;
        qa_command_tokens_free(&role->arguments);
        role->arguments = row->arguments; row->arguments = (qa_command_tokens){0};
        memcpy(role->input_keys, row->keys, sizeof(role->input_keys));
        role->client = row->client; role->local_client = (row->flags & ROLE_LOCAL_CLIENT) != 0;
        role->retired = (row->flags & ROLE_RETIRED) != 0;
        role->committed = (row->flags & ROLE_COMMITTED) != 0;
        if (row->projection_count && (!role->projection || !role->projection->has_inventory))
            return application_fail(error, QA_ERROR_FORMAT, "Q3 source projections lack their actual immutable declaration");
        guest_projection_actor **tail = role->projection ? &role->projection->actors : NULL;
        for (size_t j = 0; j < row->projection_count; ++j) {
            saved_projection *record = row->projections + j;
            if (record->bound) {
                uint32_t slot;
                if (!qa_q3_host_actor_slot(role->host, record->actor, &slot, error) || slot != record->slot)
                    return application_fail(error, QA_ERROR_FORMAT, "Q3 source projection changes its selected actor or client slot");
            }
            guest_projection_actor *context = calloc(1, sizeof(*context));
            if (!context) return application_fail(error, QA_ERROR_MEMORY, "Restoring Q3 private inventory lease contexts");
            *context = (guest_projection_actor){.projection = role->projection, .actor = record->actor,
                .slot = record->slot, .inventory_bound = record->bound, .inventory_prepared = record->bound,
                .inventory_lease = {record->serial ? record->actor : (qa_actor_id){0}, record->serial}};
            *tail = context; tail = &context->next;
        }
    }
    saved->imported = true;
    return true;
}

bool application_guest_q3_save_finish(application_provider *provider, qa_error *error)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    q3g_restore *saved = engine ? engine->restoration : NULL;
    if (!saved || !saved->imported || !engine->restore_pending || !owner(provider, error) ||
        !qa_world_idle(engine->world) || provider->application->operation != APPLICATION_PERSISTING)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 source finish requires its imported candidate continuation");
    if (!clients_agree(provider, error) || !application_portals_validate(provider->application, error)) return false;
    /* The application coordinator must have validated the complete WORLD,
     * collective portal contributors, bots, roster and frontend owners first.
     * Engine entry stays blocked throughout this final byte qualification. */
    for (size_t i = 0; i < saved->role_count; ++i) {
        saved_role *row = saved->roles + i; q3g_role *role = row->actual;
        if (role->projection)
            for (guest_projection_actor *context = role->projection->actors; context; context = context->next)
                if (context->inventory_prepared || (context->inventory_bound &&
                    (!qa_inventory_primary_current(provider->application->inventory, context->inventory_lease, context) ||
                     application_provider_for(provider->application, context->actor, QA_ROLE_INVENTORY, NULL) != provider)))
                    return application_fail(error, QA_ERROR_FORMAT, "Q3 source primary lease was not restored exactly");
        if (role->local_client && ((row->flags & ROLE_INITIALIZED) &&
            (!engine->clients[role->client].connected || engine->seats[role->client] != role->seat)))
            return application_fail(error, QA_ERROR_FORMAT, "Q3 local client role disagrees with its restored source seat");
        qa_buffer services = {0};
        bool ok = qa_q3_host_checkpoint_services(role->host, &services, error) &&
            equal_bytes(row->services, (qa_bytes){services.data, services.size});
        qa_buffer_free(&services);
        if (!ok) return application_fail(error, QA_ERROR_FORMAT, "Q3 host service inventory differs from its original source owner");
    }
    for (size_t i = 0; i < saved->role_count; ++i)
        if (!qa_q3_host_finish_restore(saved->roles[i].actual->host, error)) return false;
    qa_buffer actual = {0};
    bool ok = capture_body(provider, &actual, error) &&
        equal_bytes((qa_bytes){actual.data, actual.size}, (qa_bytes){saved->storage.data, saved->storage.size});
    qa_buffer_free(&actual);
    if (!ok) return application_fail(error, QA_ERROR_FORMAT, "Q3 candidate source continuation differs after owner reconnection");
    for (size_t i = 0; i < saved->role_count; ++i)
        saved->roles[i].actual->initialized = (saved->roles[i].flags & ROLE_INITIALIZED) != 0;
    engine->restore_pending = false;
    application_guest_q3_save_clear(engine);
    return true;
}
