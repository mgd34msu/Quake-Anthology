/* Protocol 68 operations ported from quake-typescript/src/network/q3. */
#include "qa/network_q3.h"
#include <stdlib.h>
#include <string.h>

static bool read_fail(qa_q3_reader *r, const char *s) { return qa_net_reader_fail(&r->raw, s); }
static bool write_fail(qa_q3_writer *w, const char *s) { return qa_net_writer_fail(&w->raw, s); }
static bool write8(qa_q3_writer *w, uint32_t x) { return qa_q3_write_bits(w, x, 8); }
static bool write32(qa_q3_writer *w, int32_t x) { return qa_q3_write_bits(w, (uint32_t)x, 32); }

void qa_q3_gamestate_init(qa_q3_gamestate *g) {
    memset(g, 0, sizeof(*g));
    g->string_bytes = 1;
}
const char *qa_q3_configstring(const qa_q3_gamestate *g, unsigned index) {
    if (!g || !g->string_bytes || g->string_bytes > sizeof(g->strings) || index >= QA_Q3_CONFIGSTRINGS
        || g->config_offsets[index] >= g->string_bytes) return NULL;
    const char *text = g->strings + g->config_offsets[index];
    return memchr(text, 0, g->string_bytes - g->config_offsets[index]) ? text : NULL;
}
bool qa_q3_configstring_set(qa_q3_gamestate *g, unsigned index, const char *value, qa_error *error) {
    if (!g || !value || index >= QA_Q3_CONFIGSTRINGS || strlen(value) >= 8192) {
        qa_error_set(error, QA_ERROR_ARGUMENT, index, "Invalid Q3 configstring"); return false;
    }
    char strings[QA_Q3_GAMESTATE_CHARS] = {0};
    uint16_t offsets[QA_Q3_CONFIGSTRINGS] = {0};
    size_t used = 1;
    for (unsigned i = 0; i < QA_Q3_CONFIGSTRINGS; ++i) {
        const char *text = i == index ? value : qa_q3_configstring(g, i);
        if (!text || !*text) continue;
        size_t count = strlen(text) + 1;
        if (count > sizeof(strings) - used) {
            qa_error_set(error, QA_ERROR_FORMAT, used, "Q3 gamestate strings exceed 16000 bytes"); return false;
        }
        offsets[i] = (uint16_t)used;
        memcpy(strings + used, text, count);
        used += count;
    }
    memcpy(g->config_offsets, offsets, sizeof(offsets));
    memcpy(g->strings, strings, used);
    g->string_bytes = used;
    return true;
}

bool qa_q3_server_begin(qa_q3_writer *w, int32_t ack) { return write32(w, ack); }
bool qa_q3_server_command(qa_q3_writer *w, int32_t sequence, const char *text) {
    return write8(w, 5) && write32(w, sequence) && qa_q3_write_string(w, text, false);
}
bool qa_q3_server_gamestate(qa_q3_writer *w, const qa_q3_gamestate *g) {
    if (!g || !g->string_bytes || g->string_bytes > QA_Q3_GAMESTATE_CHARS)
        return write_fail(w, "Invalid Q3 gamestate string storage");
    if (!write8(w, 2) || !write32(w, g->command_sequence)) return false;
    for (unsigned i = 0; i < QA_Q3_CONFIGSTRINGS; ++i) {
        const char *s = qa_q3_configstring(g, i);
        if (!s) return write_fail(w, "Invalid Q3 configstring offset");
        if (strlen(s) >= 8192) return write_fail(w, "Q3 configstring exceeds wire string limit");
        if (*s && (!write8(w, 3) || !qa_q3_write_bits(w, i, 16) || !qa_q3_write_string(w, s, true))) return false;
    }
    for (unsigned i = 0; i < QA_Q3_ENTITIES; ++i) {
        if (!g->baseline_present[i]) continue;
        if (!write8(w, 4)) return false;
        if (g->baselines[i].number == QA_Q3_ENTITY_NONE && i != QA_Q3_ENTITY_NONE) {
            qa_q3_entity removed = {.number = (int32_t)i};
            if (!qa_q3_write_entity(w, &removed, NULL, true)) return false;
        } else {
            if (g->baselines[i].number != (int32_t)i) return write_fail(w, "Q3 baseline number does not match its slot");
            if (!qa_q3_write_entity(w, NULL, &g->baselines[i], true)) return false;
        }
    }
    return write8(w, 8) && write32(w, g->client_number) && write32(w, g->checksum_feed);
}
static bool entities_valid(const qa_q3_snapshot *s) {
    if (!s) return true;
    if (s->entity_count > QA_Q3_ENTITY_NONE || (s->entity_count && !s->entities)) return false;
    int32_t previous = -1;
    for (size_t i = 0; i < s->entity_count; ++i) {
        if (s->entities[i].number <= previous || s->entities[i].number >= QA_Q3_ENTITY_NONE) return false;
        previous = s->entities[i].number;
    }
    return true;
}
bool qa_q3_server_snapshot(qa_q3_writer *w, const qa_q3_snapshot *from,
                           const qa_q3_snapshot *to, const qa_q3_gamestate *g) {
    if (!to || !g || !entities_valid(from) || !entities_valid(to) || to->area_bytes > 32)
        return write_fail(w, "Invalid Q3 snapshot");
    int64_t distance = from ? (int64_t)to->message_number - from->message_number : 0;
    if (from && (!from->valid || distance < 1 || distance > 255)) return write_fail(w, "Invalid Q3 snapshot delta distance");
    if (!write8(w, 7) || !write32(w, to->server_time) || !write8(w, (uint32_t)distance)
        || !write8(w, to->flags) || !write8(w, to->area_bytes)
        || !qa_q3_write_data(w, (qa_bytes){to->area_mask, to->area_bytes})
        || !qa_q3_write_player(w, from ? &from->player : NULL, &to->player)) return false;
    size_t old_index = 0, next_index = 0, old_count = from ? from->entity_count : 0;
    while (old_index < old_count || next_index < to->entity_count) {
        const qa_q3_entity *old = old_index < old_count ? &from->entities[old_index] : NULL;
        const qa_q3_entity *next = next_index < to->entity_count ? &to->entities[next_index] : NULL;
        if (old && next && old->number == next->number) {
            if (!qa_q3_write_entity(w, old, next, false)) return false;
            ++old_index; ++next_index;
        } else if (next && (!old || next->number < old->number)) {
            const qa_q3_entity *baseline = g->baseline_present[next->number] ? &g->baselines[next->number] : NULL;
            if (!qa_q3_write_entity(w, baseline, next, true)) return false;
            ++next_index;
        } else {
            if (!qa_q3_write_entity(w, old, NULL, true)) return false;
            ++old_index;
        }
    }
    return qa_q3_write_bits(w, QA_Q3_ENTITY_NONE, 10);
}
bool qa_q3_server_download_block(qa_q3_writer *w, int32_t source_block, int32_t file_size, qa_bytes bytes)
{
    if (source_block < 0 || file_size < 0 || bytes.size > QA_Q3_DOWNLOAD_BYTES || (bytes.size && !bytes.data))
        return write_fail(w, "Invalid original Q3 download block");
    return write8(w, 6) && qa_q3_write_bits(w, (uint32_t)source_block & UINT32_C(65535), 16) &&
        (source_block != 0 || write32(w, file_size)) && qa_q3_write_bits(w, (uint32_t)bytes.size, 16) && qa_q3_write_data(w, bytes);
}
bool qa_q3_server_download(qa_q3_writer *w, const qa_q3_download *d) {
    if (!d || d->size > QA_Q3_DOWNLOAD_BYTES) return write_fail(w, "Invalid Q3 download block size");
    if (!d->block && d->file_size < 0)
        return write8(w, 6) && qa_q3_write_bits(w, 0, 16) && write32(w, d->file_size) && qa_q3_write_string(w, d->error, false);
    return qa_q3_server_download_block(w, (uint16_t)d->block, d->block ? 0 : d->file_size, (qa_bytes){d->data, d->size});
}
bool qa_q3_server_end(qa_q3_writer *w) { return write8(w, 8); }

static bool read_gamestate(qa_q3_reader *r, qa_q3_gamestate *g) {
    qa_q3_gamestate_init(g);
    g->command_sequence = (int32_t)qa_q3_read_bits(r, 32);
    for (;;) {
        unsigned opcode = qa_q3_read_bits(r, 8);
        if (r->raw.failed) return false;
        if (opcode == 8) break;
        if (opcode == 3) {
            unsigned index = qa_q3_read_bits(r, 16);
            char value[8192];
            if (index >= QA_Q3_CONFIGSTRINGS) return read_fail(r, "Invalid Q3 configstring index");
            if (!qa_q3_read_string(r, value, sizeof(value), true)) return false;
            size_t size = strlen(value) + 1;
            if (size > sizeof(g->strings) - g->string_bytes) return read_fail(r, "Q3 gamestate string storage exhausted");
            g->config_offsets[index] = (uint16_t)g->string_bytes;
            memcpy(g->strings + g->string_bytes, value, size);
            g->string_bytes += size;
        } else if (opcode == 4) {
            unsigned number = qa_q3_read_bits(r, 10);
            if (number >= QA_Q3_ENTITIES) return read_fail(r, "Invalid Q3 baseline number");
            if (!qa_q3_read_entity(r, NULL, (int32_t)number, &g->baselines[number])) return false;
            g->baseline_present[number] = true;
        } else return read_fail(r, "Invalid Q3 gamestate opcode");
    }
    g->client_number = (int32_t)qa_q3_read_bits(r, 32);
    g->checksum_feed = (int32_t)qa_q3_read_bits(r, 32);
    return !r->raw.failed;
}
static bool append_entity(qa_q3_reader *r, qa_q3_snapshot *s, qa_q3_entity **storage,
    size_t *capacity, bool *owned, const qa_q3_entity *entity) {
    if (entity->number == QA_Q3_ENTITY_NONE) return true;
    if (s->entity_count == *capacity) {
        size_t maximum = SIZE_MAX / sizeof(**storage);
        if (*capacity >= maximum) return read_fail(r, "Q3 snapshot entity storage exhausted");
        size_t next = *capacity > maximum / 2 ? maximum : *capacity ? *capacity * 2 : QA_Q3_ENTITIES;
        qa_q3_entity *fresh = *owned ? realloc(*storage, next * sizeof(*fresh)) : malloc(next * sizeof(*fresh));
        if (!fresh) {
            qa_error_set(r->raw.error, QA_ERROR_MEMORY, 0, "Growing original Q3 snapshot entity storage");
            r->raw.failed = true; return false;
        }
        if (!*owned && s->entity_count) memcpy(fresh, *storage, s->entity_count * sizeof(*fresh));
        *storage = fresh; *capacity = next; *owned = true;
    }
    (*storage)[s->entity_count++] = *entity; s->entities = *storage;
    return true;
}
static bool read_snapshot(qa_q3_reader *r, qa_q3_server_decode *context, qa_q3_snapshot *s,
    qa_q3_entity **storage, size_t *capacity, bool *owned, qa_q3_snapshot_validity *validity) {
    memset(s, 0, sizeof(*s));
    s->entities = *storage;
    s->message_number = context->message_number;
    s->server_time = (int32_t)qa_q3_read_bits(r, 32);
    uint32_t distance = qa_q3_read_bits(r, 8);
    s->delta_number = distance ? (int32_t)((uint32_t)s->message_number - distance) : -1;
    s->flags = (uint8_t)qa_q3_read_bits(r, 8);
    s->server_command_number = context->server_command_sequence;
    s->parse_entities_number = context->parse_entities_number;
    const qa_q3_snapshot *old = s->delta_number > 0 && context->history
        ? context->history(context->history_context, s->delta_number) : NULL;
    *validity = QA_Q3_SNAPSHOT_VALID;
    if (s->delta_number > 0) {
        if (!old) *validity = QA_Q3_SNAPSHOT_MISSING_DELTA;
        else if (!old->valid) *validity = QA_Q3_SNAPSHOT_INVALID_DELTA;
        else if (old->message_number != s->delta_number) *validity = QA_Q3_SNAPSHOT_STALE_DELTA;
        else if (context->parse_entities_number < old->parse_entities_number
            || context->parse_entities_number - old->parse_entities_number > QA_Q3_PARSE_ENTITIES - 128)
            *validity = QA_Q3_SNAPSHOT_STALE_ENTITIES;
    }
    s->valid = *validity == QA_Q3_SNAPSHOT_VALID;
    s->area_bytes = (uint8_t)qa_q3_read_bits(r, 8);
    if (s->area_bytes > sizeof(s->area_mask)) return read_fail(r, "Q3 snapshot area mask exceeds 32 bytes");
    if (!qa_q3_read_data(r, s->area_mask, s->area_bytes)
        || !qa_q3_read_player(r, old ? &old->player : NULL, context->product, &s->player)) return false;
    size_t index = 0;
    for (;;) {
        int32_t number = (int32_t)qa_q3_read_bits(r, 10);
        if (r->raw.failed) return false;
        if (number == QA_Q3_ENTITY_NONE) break;
        if (number < 0 || number >= QA_Q3_ENTITIES) return read_fail(r, "Invalid Q3 packet entity number");
        while (old && index < old->entity_count && old->entities[index].number < number) {
            if (!append_entity(r, s, storage, capacity, owned, &old->entities[index++])) return false;
        }
        const qa_q3_entity *baseline;
        if (old && index < old->entity_count && old->entities[index].number == number) baseline = &old->entities[index++];
        else baseline = context->gamestate->baseline_present[number] ? &context->gamestate->baselines[number] : NULL;
        qa_q3_entity entity;
        if (!qa_q3_read_entity(r, baseline, number, &entity) ||
            !append_entity(r, s, storage, capacity, owned, &entity)) return false;
    }
    while (old && index < old->entity_count)
        if (!append_entity(r, s, storage, capacity, owned, &old->entities[index++])) return false;
    if (s->entity_count > UINT64_MAX - context->parse_entities_number)
        return read_fail(r, "Q3 parse entity sequence exhausted");
    context->parse_entities_number += s->entity_count;
    return !r->raw.failed;
}
bool qa_q3_server_cursor_init(qa_q3_server_cursor *cursor, qa_bytes bytes, qa_error *error) {
    if (!cursor || !bytes.data || !bytes.size || bytes.size > QA_Q3_MESSAGE_BYTES) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 server cursor packet"); return false;
    }
    memset(cursor, 0, sizeof(*cursor));
    qa_q3_reader_init(&cursor->reader, bytes, false, error);
    cursor->phase = QA_Q3_SERVER_CURSOR_ACK;
    return true;
}
bool qa_q3_server_cursor_continue(qa_q3_server_cursor *cursor, qa_q3_server_decode *context,
    qa_q3_server_event_fn consume, void *user, bool source_callbacks, bool *pending, qa_error *error) {
    if (!cursor || !context || !context->gamestate || !consume || !pending ||
        cursor->phase == QA_Q3_SERVER_CURSOR_FAILED || cursor->reader.raw.failed ||
        cursor->phase < QA_Q3_SERVER_CURSOR_ACK || cursor->phase > QA_Q3_SERVER_CURSOR_FAILED) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 server decoder context"); return false;
    }
    *pending = false;
    cursor->reader.raw.error = error;
    qa_q3_reader *reader = &cursor->reader;
    qa_q3_snapshot snapshot;
    qa_q3_entity *storage = context->entity_scratch;
    size_t capacity = storage ? context->entity_scratch_capacity : 0;
    bool owned_storage = false;
    qa_q3_command command;
    qa_q3_download download;
    qa_q3_server_event event = {.kind = QA_Q3_EVENT_ACK};
    bool ok = true;
    if (cursor->phase == QA_Q3_SERVER_CURSOR_ACK) {
        int32_t ack = (int32_t)qa_q3_read_bits(reader, 32);
        if ((int64_t)ack < (int64_t)context->reliable_sequence - QA_Q3_RELIABLE) ack = context->reliable_sequence;
        event.value.acknowledge = ack;
        cursor->phase = QA_Q3_SERVER_CURSOR_OPCODE;
        ok = !reader->raw.failed && consume(user, &event, error);
    }
    while (ok && cursor->phase != QA_Q3_SERVER_CURSOR_DONE) {
        unsigned opcode;
        if (cursor->phase == QA_Q3_SERVER_CURSOR_GAMESTATE) opcode = 2;
        else if (cursor->phase == QA_Q3_SERVER_CURSOR_DOWNLOAD) opcode = 6;
        else {
            opcode = qa_q3_read_bits(reader, 8);
            if (reader->raw.failed) { ok = false; break; }
            if (opcode == 8) { cursor->phase = QA_Q3_SERVER_CURSOR_DONE; break; }
            if (opcode == 1) continue;
            if (opcode == 2) cursor->phase = QA_Q3_SERVER_CURSOR_GAMESTATE;
            if (opcode == 6) cursor->phase = QA_Q3_SERVER_CURSOR_DOWNLOAD;
        }
        if (!source_callbacks && (opcode == 2 || opcode == 6)) { *pending = true; break; }
        cursor->phase = QA_Q3_SERVER_CURSOR_OPCODE;
        switch (opcode) {
        case 2:
            event.kind = QA_Q3_EVENT_GAMESTATE_START;
            ok = consume(user, &event, error);
            if (!ok) break;
            ok = read_gamestate(reader, context->gamestate);
            if (!ok) break;
            context->server_command_sequence = context->gamestate->command_sequence;
            context->parse_entities_number = 0;
            event.kind = QA_Q3_EVENT_GAMESTATE;
            event.value.gamestate = context->gamestate;
            ok = consume(user, &event, error);
            break;
        case 5:
            command.sequence = (int32_t)qa_q3_read_bits(reader, 32);
            ok = qa_q3_read_string(reader, command.text, sizeof(command.text), false);
            if (ok && command.sequence > context->server_command_sequence) {
                context->server_command_sequence = command.sequence;
                event.kind = QA_Q3_EVENT_COMMAND; event.value.command = &command;
                ok = consume(user, &event, error);
            }
            break;
        case 6:
            memset(&download, 0, sizeof(download));
            download.block = (int16_t)(uint16_t)qa_q3_read_bits(reader, 16);
            if (!download.block) {
                download.file_size = (int32_t)qa_q3_read_bits(reader, 32);
                if (reader->raw.failed) { ok = false; break; }
                if (context->download_size && !context->download_size(context->download_context,
                    download.file_size, &download.file_size, error)) { ok = false; break; }
            }
            if (!download.block && download.file_size < 0) {
                ok = qa_q3_read_string(reader, download.error, sizeof(download.error), false);
            } else {
                download.size = qa_q3_read_bits(reader, 16);
                if (download.size > sizeof(download.data)) ok = read_fail(reader, "Q3 download block exceeds message limit");
                else ok = qa_q3_read_data(reader, download.data, download.size);
            }
            if (ok) { event.kind = QA_Q3_EVENT_DOWNLOAD; event.value.download = &download; ok = consume(user, &event, error); }
            if (download.file_size < 0) cursor->phase = QA_Q3_SERVER_CURSOR_DONE;
            break;
        case 7:
            event.kind = QA_Q3_EVENT_SNAPSHOT; event.value.snapshot.value = &snapshot;
            ok = read_snapshot(reader, context, &snapshot, &storage, &capacity, &owned_storage, &event.value.snapshot.validity);
            if (ok) ok = consume(user, &event, error);
            break;
        default: ok = read_fail(reader, "Invalid Q3 server opcode"); break;
        }
    }
    if (owned_storage) free(storage);
    if (!ok) cursor->phase = QA_Q3_SERVER_CURSOR_FAILED;
    cursor->reader.raw.error = NULL;
    return ok;
}

bool qa_q3_decode_server(qa_bytes bytes, qa_q3_server_decode *context,
    qa_q3_server_event_fn consume, void *user, qa_error *error) {
    qa_q3_server_cursor cursor; bool pending;
    return qa_q3_server_cursor_init(&cursor, bytes, error) &&
        qa_q3_server_cursor_continue(&cursor, context, consume, user, true, &pending, error);
}

bool qa_q3_server_cursor_pending_valid(const qa_q3_server_cursor *cursor, qa_q3_product product,
    qa_error *error) {
    if (!cursor || (product != QA_Q3_ARENA && product != QA_Q3_TEAM_ARENA) ||
        cursor->reader.raw.failed || cursor->reader.oob ||
        (cursor->phase != QA_Q3_SERVER_CURSOR_GAMESTATE && cursor->phase != QA_Q3_SERVER_CURSOR_DOWNLOAD) ||
        !cursor->reader.raw.bytes.data || !cursor->reader.raw.bytes.size ||
        cursor->reader.raw.bytes.size > QA_Q3_MESSAGE_BYTES) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid held Q3 source boundary"); return false;
    }
    qa_q3_gamestate *gamestate = calloc(1, sizeof(*gamestate));
    qa_q3_entity *storage = NULL; size_t capacity = 0; bool owned_storage = false;
    if (!gamestate) {
        free(gamestate);
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Qualifying held Q3 packet cursor"); return false;
    }
    qa_q3_reader reader;
    qa_q3_reader_init(&reader, cursor->reader.raw.bytes, false, error);
    (void)qa_q3_read_bits(&reader, 32);
    qa_q3_server_decode context = {.product = product, .gamestate = gamestate};
    bool valid = false;
    while (!reader.raw.failed) {
        unsigned opcode = qa_q3_read_bits(&reader, 8);
        if (reader.raw.failed) break;
        if (opcode == 2 || opcode == 6) {
            valid = reader.raw.bit == cursor->reader.raw.bit &&
                cursor->phase == (opcode == 2 ? QA_Q3_SERVER_CURSOR_GAMESTATE : QA_Q3_SERVER_CURSOR_DOWNLOAD);
            break;
        }
        if (opcode == 1) continue;
        if (opcode == 5) {
            char text[QA_Q3_COMMAND_CHARS];
            (void)qa_q3_read_bits(&reader, 32);
            if (!qa_q3_read_string(&reader, text, sizeof(text), false)) break;
        } else if (opcode == 7) {
            qa_q3_snapshot snapshot; qa_q3_snapshot_validity validity;
            if (!read_snapshot(&reader, &context, &snapshot, &storage, &capacity, &owned_storage, &validity)) break;
        } else break;
    }
    free(storage); free(gamestate);
    if (!valid && !reader.raw.failed)
        qa_error_set(error, QA_ERROR_FORMAT, cursor->reader.raw.bit, "Held Q3 cursor is not its first source boundary");
    return valid;
}

bool qa_q3_client_cursor_init(qa_q3_client_cursor *c, qa_bytes data, qa_error *error) {
    if (!c) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing Q3 client cursor"); return false; }
    memset(c, 0, sizeof(*c));
    qa_q3_reader_init(&c->reader, data, false, error);
    c->header.server_id = (int32_t)qa_q3_read_bits(&c->reader, 32);
    c->header.message_acknowledge = (int32_t)qa_q3_read_bits(&c->reader, 32);
    c->header.reliable_acknowledge = (int32_t)qa_q3_read_bits(&c->reader, 32);
    return !c->reader.raw.failed;
}
bool qa_q3_client_cursor_next(qa_q3_client_cursor *c, qa_q3_client_part *part, qa_q3_command *command) {
    if (!part || !command || c->phase) return read_fail(&c->reader, "Q3 client cursor is not reading commands");
    unsigned opcode = qa_q3_read_bits(&c->reader, 8);
    if (c->reader.raw.failed) return false;
    switch (opcode) {
    case 4:
        *part = QA_Q3_CLIENT_COMMAND;
        command->sequence = (int32_t)qa_q3_read_bits(&c->reader, 32);
        return qa_q3_read_string(&c->reader, command->text, sizeof(command->text), false);
    case 2: *part = QA_Q3_CLIENT_MOVE; c->phase = 1; return true;
    case 3: *part = QA_Q3_CLIENT_MOVE_NO_DELTA; c->phase = 1; return true;
    case 5: *part = QA_Q3_CLIENT_END; c->phase = 3; return true;
    default: return read_fail(&c->reader, "Invalid Q3 client opcode");
    }
}
bool qa_q3_client_cursor_movement(qa_q3_client_cursor *c, int32_t feed, const char *command,
                                  qa_q3_usercmd commands[QA_Q3_USERCMDS], size_t *count) {
    if (c->phase != 1 || !commands || !count || !command) return read_fail(&c->reader, "Invalid Q3 movement cursor state");
    unsigned n = qa_q3_read_bits(&c->reader, 8);
    if (!n || n > QA_Q3_USERCMDS) return read_fail(&c->reader, "Invalid Q3 backup usercmd count");
    uint32_t key = (uint32_t)feed ^ (uint32_t)c->header.message_acknowledge ^ qa_q3_command_hash(command, 32);
    qa_q3_usercmd previous = {0};
    for (unsigned i = 0; i < n; ++i) {
        if (!qa_q3_read_usercmd(&c->reader, &previous, &key, &commands[i])) return false;
        previous = commands[i];
    }
    *count = n; c->phase = 2;
    return true;
}
bool qa_q3_client_cursor_end(qa_q3_client_cursor *c) {
    if (c->phase == 3) return true;
    if (c->phase != 2) return read_fail(&c->reader, "Invalid Q3 client terminal state");
    if (qa_q3_read_bits(&c->reader, 8) != 5 || c->reader.raw.failed)
        return read_fail(&c->reader, "Q3 client movement lacks terminal EOF");
    c->phase = 3; return true;
}
bool qa_q3_encode_client(qa_q3_writer *w, const qa_q3_client_message *m, int32_t feed, const char *server_command) {
    if (!m || !server_command || m->command_count > QA_Q3_RELIABLE + 1
        || m->header.message_acknowledge < 0 || m->header.reliable_acknowledge < 0)
        return write_fail(w, "Invalid Q3 client message header");
    if (!write32(w, m->header.server_id) || !write32(w, m->header.message_acknowledge)
        || !write32(w, m->header.reliable_acknowledge)) return false;
    for (size_t i = 0; i < m->command_count; ++i) {
        const qa_q3_command *c = &m->commands[i];
        if (i && c->sequence <= m->commands[i - 1].sequence) return write_fail(w, "Q3 reliable commands are unordered");
        if (!write8(w, 4) || !write32(w, c->sequence) || !qa_q3_write_string(w, c->text, false)) return false;
    }
    if (m->movement) {
        if (!m->usercmd_count || m->usercmd_count > QA_Q3_USERCMDS) return write_fail(w, "Invalid Q3 movement count");
        if (!write8(w, m->no_delta ? 3 : 2) || !write8(w, (uint32_t)m->usercmd_count)) return false;
        uint32_t key = (uint32_t)feed ^ (uint32_t)m->header.message_acknowledge ^ qa_q3_command_hash(server_command, 32);
        qa_q3_usercmd previous = {0};
        for (size_t i = 0; i < m->usercmd_count; ++i) {
            if (!qa_q3_write_usercmd(w, &previous, &m->usercmds[i], &key)) return false;
            previous = m->usercmds[i];
        }
    }
    return write8(w, 5);
}
