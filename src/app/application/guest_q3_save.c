#include "guest_q3_save.h"
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
