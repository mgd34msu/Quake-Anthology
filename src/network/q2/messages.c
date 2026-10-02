#include "messages_internal.h"
#include "q2pro_internal.h"
#include <stdlib.h>
#include <zlib.h>


static bool is_kex(const qa_q2_codec *c) {
    return c->protocol.kind == QA_NET_Q2KEX_2023 || c->protocol.kind == QA_NET_Q2KEX_DEMO_2022;
}
static bool is_rerelease(const qa_q2_codec *c) {
    return c->protocol.kind == QA_NET_Q2REPRO_1038 || c->protocol.kind == QA_NET_Q2PRIVATE_4038;
}
static bool extended(const qa_q2_codec *c) {
    return c->protocol.kind == QA_NET_Q2PRO_36 && qa_q2pro_extensions(c);
}

static const char *read_text(qa_net_reader *r) {
    if (r->failed) return NULL;
    if (r->bit % 8) { qa_net_reader_fail(r, "Unaligned Q2 string"); return NULL; }
    size_t remaining = qa_net_reader_remaining(r);
    const uint8_t *start = remaining ? r->bytes.data + r->bit / 8 : NULL;
    const uint8_t *end = remaining ? memchr(start, 0, remaining) : NULL;
    if (!end) { qa_net_reader_fail(r, "Unterminated Q2 string"); return NULL; }
    r->bit += ((size_t)(end - start) + 1) * 8;
    return (const char *)start;
}

static qa_bytes raw_span(qa_net_reader *r, size_t start) {
    return (qa_bytes){r->bytes.data + start, r->bit / 8 - start};
}

static bool callback_failed(qa_net_reader *reader, const char *message)
{
    if (!reader->error || reader->error->code == QA_OK) return qa_net_reader_fail(reader, message);
    reader->failed = true; return false;
}

static void download_reset(qa_q2_messages *m) {
    if (m->download_open) inflateEnd(&m->download);
    memset(&m->download, 0, sizeof(m->download));
    m->download_open = false;
    while (m->download_first) {
        qa_q2_inflate_segment *next = m->download_first->next;
        qa_buffer_free(&m->download_first->compressed); free(m->download_first);
        m->download_first = next;
    }
    m->download_last = NULL;
}

void qa_q2_messages_reset(qa_q2_messages *m) {
    if (!m) return;
    for (size_t i = 0; i < m->options.config_strings; ++i) { free(m->configs[i]); m->configs[i] = NULL; }
    for (size_t i = 0; i < QA_Q2_MAX_SEATS; ++i) qa_q2_frame_history_clear(m->histories[i]);
    m->baseline_count = 0;
    m->seat = 0;
    m->stream = STREAM_NONE;
    m->codec.demo26 = false;
    m->codec.frame_player_pending = false;
    download_reset(m);
}

void qa_q2_messages_destroy(qa_q2_messages *m) {
    if (!m) return;
    qa_q2_messages_reset(m);
    for (size_t i = 0; i < QA_Q2_MAX_SEATS; ++i) qa_q2_frame_history_destroy(m->histories[i]);
    free(m->configs);
    free(m->baselines);
    free(m);
}

bool qa_q2_messages_create(qa_net_protocol_id protocol, const qa_q2_message_options *options,
                            qa_q2_messages **out, qa_error *error) {
    if (!out || !options || !options->config_strings || options->config_strings > UINT16_MAX ||
        !options->inventory_slots || options->inventory_slots > 32768 ||
        (options->native_api2023 && protocol.kind != QA_NET_Q2KEX_2023)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 message layout limits"); return false;
    }
    qa_q2_messages *m = calloc(1, sizeof(*m));
    if (!m) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate Q2 decoder"); return false; }
    m->options = *options;
    if (!m->options.history_capacity) m->options.history_capacity = 16;
    if (!m->options.max_inflated_bytes) m->options.max_inflated_bytes = 64u * 1024u * 1024u;
    m->configs = calloc(options->config_strings, sizeof(*m->configs));
    if (!m->configs || !qa_q2_codec_init(&m->codec, protocol, error)) {
        if (!m->configs) qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate Q2 config strings");
        free(m->configs); free(m); return false;
    }
    *out = m;
    return true;
}

qa_q2_codec *qa_q2_messages_codec(qa_q2_messages *m) { return m ? &m->codec : NULL; }
const char *qa_q2_messages_config(const qa_q2_messages *m, uint16_t index) {
    return m && index < m->options.config_strings ? m->configs[index] : NULL;
}
static size_t baseline_slot(const qa_q2_messages *m, uint32_t number) {
    size_t lo = 0, hi = m->baseline_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (m->baselines[mid].number < number) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}
const qa_q2_entity *qa_q2_messages_baseline(const qa_q2_messages *m, uint32_t number) {
    if (!m) return NULL;
    size_t slot = baseline_slot(m, number);
    return slot < m->baseline_count && m->baselines[slot].number == number ? &m->baselines[slot] : NULL;
}
const qa_q2_wire_frame *qa_q2_messages_latest(const qa_q2_messages *m, uint8_t seat) {
    return m && seat < QA_Q2_MAX_SEATS ? qa_q2_frame_history_latest(m->histories[seat]) : NULL;
}

static bool set_config(qa_q2_messages *m, uint16_t index, const char *value, qa_error *error) {
    if (index >= m->options.config_strings || !value) {
        qa_error_set(error, QA_ERROR_FORMAT, index, "Invalid Q2 config string"); return false;
    }
    size_t length = strlen(value);
    char *copy = malloc(length + 1);
    if (!copy) { qa_error_set(error, QA_ERROR_MEMORY, index, "Cannot retain Q2 config string"); return false; }
    memcpy(copy, value, length + 1);
    free(m->configs[index]); m->configs[index] = copy;
    return true;
}

static bool set_baseline(qa_q2_messages *m, const qa_q2_entity *entity, qa_error *error) {
    if (!entity || !entity->number || entity->number > UINT16_MAX) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid Q2 spawn baseline"); return false;
    }
    size_t slot = baseline_slot(m, entity->number);
    if (slot < m->baseline_count && m->baselines[slot].number == entity->number) {
        m->baselines[slot] = *entity; return true;
    }
    if (m->baseline_count == m->baseline_capacity) {
        size_t capacity = m->baseline_capacity ? m->baseline_capacity * 2 : 64;
        if (capacity > UINT16_MAX) capacity = UINT16_MAX;
        qa_q2_entity *entries = realloc(m->baselines, capacity * sizeof(*entries));
        if (!entries) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot retain Q2 baselines"); return false; }
        m->baselines = entries; m->baseline_capacity = capacity;
    }
    memmove(m->baselines + slot + 1, m->baselines + slot, (m->baseline_count - slot) * sizeof(*m->baselines));
    m->baselines[slot] = *entity; ++m->baseline_count;
    return true;
}

static qa_q2_frame_history *history(qa_q2_messages *m, uint8_t seat, qa_error *error) {
    if (seat >= QA_Q2_MAX_SEATS) { qa_error_set(error, QA_ERROR_FORMAT, seat, "Invalid Q2 seat"); return NULL; }
    if (!m->histories[seat] && !qa_q2_frame_history_create(m->options.history_capacity, &m->histories[seat], error))
        return NULL;
    return m->histories[seat];
}

bool qa_q2_messages_accept(qa_q2_messages *m, const qa_q2_server_record *record, qa_error *error) {
    if (!m || !record || record->seat > QA_Q2_MAX_SEATS ||
        (record->seat == QA_Q2_MAX_SEATS && !is_kex(&m->codec)) || m->reading) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid decoded Q2 record"); return false;
    }
    const qa_q2_server_event *e = &record->event;
    if (e->kind == QA_Q2_SVC_SERVERDATA) qa_q2_messages_reset(m);
    m->seat = record->seat;
    if (e->kind == QA_Q2_SVC_CONFIGSTRING) return set_config(m, e->data.config.index, e->data.config.value, error);
    if (e->kind == QA_Q2_SVC_BASELINE) return set_baseline(m, &e->data.baseline, error);
    if (e->kind == QA_Q2_SVC_FRAME) {
        qa_q2_frame_history *h = history(m, is_kex(&m->codec) ? 0 : record->seat, error);
        return h && qa_q2_frame_history_accept(h, e->data.frame, error);
    }
    return true;
}

static bool emit_record(qa_q2_messages *m, qa_net_reader *r, size_t start, uint8_t opcode,
                         qa_q2_server_event *event, qa_q2_server_emit_fn emit, void *user) {
    if (r->failed) return false;
    if (r->bit % 8) return qa_net_reader_fail(r, "Q2 server record ended inside a byte");
    qa_q2_server_record record = {m->seat, opcode, raw_span(r, start), *event};
    if (event->kind == QA_Q2_SVC_FRAME && is_kex(&m->codec)) record.seat = 0;
    if (emit && !emit(user, &record, r->error)) return callback_failed(r, "Q2 server record callback failed");
    return true;
}

static bool read_config(qa_q2_messages *m, qa_net_reader *r, qa_q2_server_event *event, bool *end) {
    uint16_t index = qa_net_read_u16(r);
    *end = index == m->options.config_strings;
    if (r->failed || *end) return !r->failed;
    const char *value = read_text(r);
    if (!value || !set_config(m, index, value, r->error)) return qa_net_reader_fail(r, "Invalid Q2 config stream");
    event->kind = QA_Q2_SVC_CONFIGSTRING;
    event->data.config.index = index;
    event->data.config.value = value;
    return true;
}

static bool read_baseline(qa_q2_messages *m, qa_net_reader *r, qa_q2_server_event *event, bool *end) {
    uint32_t number; uint64_t bits;
    if (!qa_q2_read_entity_header(&m->codec, r, &number, &bits)) return false;
    *end = number == 0;
    if (*end) return true;
    static const qa_q2_entity zero;
    event->kind = QA_Q2_SVC_BASELINE;
    if (!qa_q2_read_entity(&m->codec, r, &zero, number, bits, &event->data.baseline)) return false;
    return set_baseline(m, &event->data.baseline, r->error) || qa_net_reader_fail(r, "Cannot retain Q2 spawn baseline");
}

static bool read_sound(qa_q2_codec *c, qa_net_reader *r, qa_q2_kex_sound *out) {
    if (is_kex(c)) return qa_q2_kex_read_sound(c, r, out);
    qa_q2_kex_sound s = {0};
    s.flags = qa_net_read_u8(r);
    s.index = (s.flags & 32u) ? qa_net_read_u16(r) : qa_net_read_u8(r);
    s.volume = (s.flags & 1u) ? (float)qa_net_read_u8(r) / 255.0f : 1.0f;
    s.attenuation = (s.flags & 2u) ? (float)qa_net_read_u8(r) / 64.0f : 1.0f;
    s.time_offset = (s.flags & 16u) ? (float)qa_net_read_u8(r) / 1000.0f : 0.0f;
    uint16_t channel = (s.flags & 8u) ? qa_net_read_u16(r) : 0;
    s.entity = channel >> 3; s.channel = (uint8_t)(channel & 7u);
    s.has_position = (s.flags & 4u) != 0;
    if (s.has_position && !qa_q2_game_position_read(c, r, s.position)) return false;
    if (r->failed) return false;
    *out = s; return true;
}

static bool inflate_download(qa_q2_messages *m, qa_net_reader *r, qa_bytes bytes,
                              bool stream, bool finish, size_t expected, qa_buffer *out) {
    z_stream local = {0};
    z_stream *z = stream ? &m->download : &local;
    if (!stream || !m->download_open) {
        if (inflateInit2(z, -MAX_WBITS) != Z_OK) return qa_net_reader_fail(r, "Cannot start Q2 download inflater");
        if (stream) m->download_open = true;
    }
    z->next_in = (Bytef *)bytes.data;
    z->avail_in = (uInt)bytes.size;
    size_t limit = stream ? m->options.max_inflated_bytes : expected;
    size_t used = 0, capacity = 0;
    uint8_t *data = NULL;
    int result = Z_OK;
    bool ok = true;
    for (;;) {
        if (used == capacity) {
            size_t next = capacity ? capacity * 2 : 4096;
            if (next < capacity || next > limit) next = limit;
            if (next <= capacity) {
                uint8_t probe;
                z->next_out = &probe; z->avail_out = 1;
                result = inflate(z, Z_NO_FLUSH);
                if (z->avail_out != 1) { ok = false; break; }
                if (result == Z_STREAM_END || (!finish && !z->avail_in && (result == Z_OK || result == Z_BUF_ERROR))) break;
                ok = false; break;
            }
            uint8_t *grown = realloc(data, next);
            if (!grown) { ok = false; break; }
            data = grown; capacity = next;
        }
        size_t available = capacity - used;
        if (available > UINT_MAX) available = UINT_MAX;
        z->next_out = data + used; z->avail_out = (uInt)available;
        uInt old_in = z->avail_in;
        result = inflate(z, Z_NO_FLUSH);
        used += available - z->avail_out;
        if (result == Z_STREAM_END) break;
        if (result != Z_OK && result != Z_BUF_ERROR) { ok = false; break; }
        if (!z->avail_in && z->avail_out) break;
        if (old_in == z->avail_in && z->avail_out == available) { ok = false; break; }
    }
    if (z->avail_in || (finish && result != Z_STREAM_END) || (!stream && used != expected)) ok = false;
    z->next_in = NULL; z->avail_in = 0; z->next_out = NULL; z->avail_out = 0;
    if (!stream) inflateEnd(z);
    if (!ok) { free(data); if (stream) download_reset(m); return qa_net_reader_fail(r, "Invalid or oversized Q2 compressed download"); }
    if (stream && !finish) {
        qa_q2_inflate_segment *segment = calloc(1, sizeof(*segment));
        if (segment && bytes.size) segment->compressed.data = malloc(bytes.size);
        if (!segment || (bytes.size && !segment->compressed.data)) {
            free(segment); free(data); download_reset(m);
            return qa_net_reader_fail(r, "Cannot retain Q2 deflate continuation receipt");
        }
        if (bytes.size) memcpy(segment->compressed.data, bytes.data, bytes.size);
        segment->compressed.size = bytes.size; segment->output_size = used;
        if (m->download_last) m->download_last->next = segment; else m->download_first = segment;
        m->download_last = segment;
    }
    if (stream && finish) download_reset(m);
    *out = (qa_buffer){data, used};
    return true;
}

bool qa_q2_messages_restore_download_segment(qa_q2_messages *m, qa_bytes bytes,
    size_t output_size, qa_error *error) {
    if (!m || m->reading || (bytes.size && !bytes.data) || bytes.size > UINT_MAX ||
        output_size > m->options.max_inflated_bytes) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid retained Q2 deflate receipt"); return false;
    }
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    qa_buffer discarded = {0};
    if (!inflate_download(m, &reader, bytes, true, false, 0, &discarded)) return false;
    bool ok = discarded.size == output_size;
    qa_buffer_free(&discarded);
    if (!ok) { download_reset(m); return qa_net_reader_fail(&reader, "Q2 deflate receipt changes its accepted output extent"); }
    return true;
}

static bool read_download(qa_q2_messages *m, qa_net_reader *r, unsigned compression,
                           qa_q2_server_event *event, qa_buffer *owned) {
    int16_t length = qa_net_read_i16(r);
    event->kind = QA_Q2_SVC_DOWNLOAD;
    event->data.download.percent = qa_net_read_u8(r);
    if (r->failed) return false;
    if (length < 0) { event->data.download.missing = true; download_reset(m); return true; }
    size_t expected = compression == 1 ? qa_net_read_u16(r) : 0;
    qa_bytes bytes;
    if (!qa_net_read_bytes(r, (size_t)length, &bytes)) return false;
    if (!compression) { event->data.download.bytes = bytes; return true; }
    if (!inflate_download(m, r, bytes, compression == 2, compression == 1 || event->data.download.percent == 100,
                          expected, owned)) return false;
    event->data.download.bytes = (qa_bytes){owned->data, owned->size};
    return true;
}

typedef struct blast_context {
    qa_q2_messages *messages;
    qa_net_reader *reader;
    size_t start;
    uint8_t opcode;
    qa_q2_server_emit_fn emit;
    void *user;
} blast_context;
static bool blast_config(void *user, uint16_t index, const char *value, qa_error *error) {
    blast_context *b = user;
    if (!set_config(b->messages, index, value, error)) return false;
    qa_q2_server_event e = {.kind = QA_Q2_SVC_CONFIGSTRING};
    e.data.config.index = index; e.data.config.value = value;
    return emit_record(b->messages, b->reader, b->start, b->opcode, &e, b->emit, b->user);
}
static bool blast_baseline(void *user, const qa_q2_entity *entity, qa_error *error) {
    blast_context *b = user;
    if (!set_baseline(b->messages, entity, error)) return false;
    qa_q2_server_event e = {.kind = QA_Q2_SVC_BASELINE}; e.data.baseline = *entity;
    return emit_record(b->messages, b->reader, b->start, b->opcode, &e, b->emit, b->user);
}

static bool parse_server(qa_q2_messages *m, qa_net_reader *r, qa_q2_server_emit_fn emit, void *user, unsigned depth) {
    if (depth > 32) return qa_net_reader_fail(r, "Excessively nested Q2 zpacket");
    bool kex = is_kex(&m->codec), rerelease = is_rerelease(&m->codec);
    while (qa_net_reader_remaining(r)) {
        size_t start = r->bit / 8;
        qa_q2_server_event event = {0};
        if (m->stream != STREAM_NONE) {
            bool end = false;
            bool baseline = m->stream == STREAM_BASELINE;
            if (!(baseline ? read_baseline(m, r, &event, &end) : read_config(m, r, &event, &end))) return false;
            if (end) m->stream = m->stream == STREAM_GAMESTATE ? STREAM_BASELINE : STREAM_NONE;
            else if (!emit_record(m, r, start, baseline ? 39 : 38, &event, emit, user)) return false;
            continue;
        }
        uint8_t opcode = qa_q2_service_opcode(&m->codec, qa_net_read_u8(r));
        if (m->options.private_opcodes[opcode / 8] & (1u << (opcode % 8))) {
            if (!m->options.private_read || !m->options.private_read(m->options.private_user, opcode, &m->codec, r, &event) ||
                event.kind != QA_Q2_SVC_PRIVATE) return qa_net_reader_fail(r, "Unbound Q2 private service");
            if (!emit_record(m, r, start, opcode, &event, emit, user)) return false;
            continue;
        }
        if ((opcode == 21 && (m->codec.protocol.kind == QA_NET_R1Q2_35 || m->codec.protocol.kind == QA_NET_Q2PRO_36)) ||
            (opcode == 34 && rerelease)) {
            qa_buffer inflated = {0};
            if (!qa_q2_zpacket_read(r, &inflated)) return false;
            if (inflated.size > m->options.max_inflated_bytes - m->inflated_this_read) {
                qa_buffer_free(&inflated); return qa_net_reader_fail(r, "Q2 inflated message budget exceeded");
            }
            m->inflated_this_read += inflated.size;
            qa_net_reader nested;
            qa_net_reader_init(&nested, (qa_bytes){inflated.data, inflated.size}, r->error);
            bool ok = parse_server(m, &nested, emit, user, depth + 1) && qa_net_reader_finish(&nested);
            qa_buffer_free(&inflated);
            if (!ok) return qa_net_reader_fail(r, "Invalid nested Q2 zpacket");
            continue;
        }
        qa_buffer owned = {0};
        int16_t *inventory = NULL;
        bool ok = true, end = false;
        switch (opcode) {
        case 6: event.kind = QA_Q2_SVC_NOP; break;
        case 7: event.kind = QA_Q2_SVC_DISCONNECT; break;
        case 8: event.kind = QA_Q2_SVC_RECONNECT; break;
        case 10: event.kind = QA_Q2_SVC_PRINT; event.data.print.level = qa_net_read_u8(r); event.data.print.text = read_text(r); break;
        case 11: event.kind = QA_Q2_SVC_COMMAND; event.data.print.text = read_text(r); break;
        case 15: event.kind = QA_Q2_SVC_CENTERPRINT; event.data.print.text = read_text(r); break;
        case 4: event.kind = QA_Q2_SVC_LAYOUT; event.data.print.text = read_text(r); break;
        case 12: {
            uint32_t version = qa_net_read_u32(r);
            bool demo26 = version == 26 && m->options.demo && m->codec.protocol.kind == QA_NET_Q2_34;
            if (!demo26 && version != qa_q2_protocol_version(m->codec.protocol)) { ok = qa_net_reader_fail(r, "Q2 server data protocol differs from negotiation"); break; }
            event.kind = QA_Q2_SVC_SERVERDATA;
            if (!qa_q2_read_serverdata(&m->codec, r, &event.data.serverdata)) { ok = false; break; }
            qa_q2_messages_reset(m); m->codec.demo26 = demo26;
            break;
        }
        case 13: ok = read_config(m, r, &event, &end) && !end; break;
        case 14: ok = read_baseline(m, r, &event, &end) && !end; break;
        case 20: {
            event.kind = QA_Q2_SVC_FRAME;
            qa_q2_frame_history *h = history(m, kex ? 0 : m->seat, r->error);
            ok = h && qa_q2_frame_history_read(h, &m->codec, r,
                  (qa_q2_entity_span){m->baselines, m->baseline_count}, &event.data.frame);
            break;
        }
        case 9: {
            event.kind = QA_Q2_SVC_SOUND;
            qa_q2_codec sound_codec = m->codec;
            if (m->options.native_api2023) sound_codec.protocol.kind = QA_NET_Q2REPRO_1038;
            ok = read_sound(&sound_codec, r, &event.data.sound); break;
        }
        case 3:
            event.kind = QA_Q2_SVC_TEMP_ENTITY;
            ok = qa_q2_temp_entity_read(&m->codec, r, m->options.override_extended_temps ? m->options.extended_temps : extended(&m->codec), &event.data.temporary);
            break;
        case 1: case 2: {
            event.kind = QA_Q2_SVC_MUZZLEFLASH;
            uint32_t entity = qa_net_read_u16(r), flash = qa_net_read_u8(r);
            event.data.muzzle.monster = opcode == 2;
            event.data.muzzle.silenced = opcode == 1 && (flash & 128u);
            if (opcode == 1) flash &= 127u;
            if (opcode == 2 && (rerelease || extended(&m->codec))) { flash |= (entity & 0xe000u) >> 5; entity &= 0x1fffu; }
            event.data.muzzle.entity = entity; event.data.muzzle.flash = flash;
            break;
        }
        case 5:
            event.kind = QA_Q2_SVC_INVENTORY;
            inventory = malloc(m->options.inventory_slots * sizeof(*inventory));
            if (!inventory) { ok = qa_net_reader_fail(r, "Cannot read Q2 inventory"); break; }
            for (size_t i = 0; i < m->options.inventory_slots; ++i) inventory[i] = qa_net_read_i16(r);
            event.data.inventory.counts = inventory; event.data.inventory.count = m->options.inventory_slots;
            break;
        case 16: ok = read_download(m, r, 0, &event, &owned); break;
        case 21:
            event.kind = QA_Q2_SVC_SEAT;
            if (!kex) { ok = false; break; }
            ok = qa_q2_kex_read_splitclient(r, &event.data.seat);
            if (event.data.seat > QA_Q2_MAX_SEATS) { ok = false; break; }
            m->seat = event.data.seat;
            break;
        case 22:
            if (!kex) {
                ok = read_download(m, r, m->codec.protocol.kind == QA_NET_Q2PRO_36 && m->codec.protocol.revision >= 1021 ? 2 : 1, &event, &owned);
                break;
            } else {
                blast_context b = {m, r, start, opcode, emit, user};
                if (!qa_q2_kex_read_configblast(r, m->options.max_inflated_bytes, blast_config, &b)) return false;
                continue;
            }
        case 23:
            if (!kex) { m->stream = STREAM_GAMESTATE; continue; }
            else {
                blast_context b = {m, r, start, opcode, emit, user};
                if (!qa_q2_kex_read_baselineblast(&m->codec, r, m->options.max_inflated_bytes, blast_baseline, &b)) return false;
                continue;
            }
        case 24:
            if (kex) event.kind = QA_Q2_SVC_LEVEL_RESTART;
            else { event.kind = QA_Q2_SVC_SETTING; event.data.setting.index = qa_net_read_i32(r); event.data.setting.value = qa_net_read_i32(r); }
            break;
        case 25:
            if (!kex && !rerelease) { m->stream = STREAM_CONFIG; continue; }
            event.kind = QA_Q2_SVC_DAMAGE;
            ok = qa_q2_kex_read_damage(r, event.data.damage.indicators, &event.data.damage.count); break;
        case 26:
            if (!kex && !rerelease) { m->stream = STREAM_BASELINE; continue; }
            event.kind = QA_Q2_SVC_LOCALIZED_PRINT; ok = qa_q2_kex_read_locprint(r, &event.data.localized); break;
        case 27: event.kind = QA_Q2_SVC_FOG; ok = qa_q2_fog_read(r, &event.data.fog); break;
        case 30: event.kind = QA_Q2_SVC_POI; ok = qa_q2_kex_read_poi(r, &event.data.poi); break;
        case 31: event.kind = QA_Q2_SVC_HELP_PATH; ok = qa_q2_kex_read_help_path(r, &event.data.help_path); break;
        case 32: {
            qa_q2_kex_muzzleflash flash;
            ok = qa_q2_kex_read_muzzleflash(r, &flash);
            if (ok) {
                event.kind = QA_Q2_SVC_MUZZLEFLASH;
                event.data.muzzle.entity = (uint32_t)(uint16_t)flash.entity;
                event.data.muzzle.flash = flash.weapon; event.data.muzzle.monster = true;
            }
            break;
        }
        case 33: event.kind = QA_Q2_SVC_ACHIEVEMENT; event.data.print.text = read_text(r); break;
        case 35: if (!rerelease) { ok = false; break; } ok = read_download(m, r, 2, &event, &owned); break;
        case 36: if (!rerelease) { ok = false; break; } m->stream = STREAM_GAMESTATE; continue;
        case 37:
            if (!rerelease) { ok = false; break; }
            event.kind = QA_Q2_SVC_SETTING; event.data.setting.index = qa_net_read_i32(r); event.data.setting.value = qa_net_read_i32(r); break;
        case 38: if (!rerelease) { ok = false; break; } m->stream = STREAM_CONFIG; continue;
        case 39: if (!rerelease) { ok = false; break; } m->stream = STREAM_BASELINE; continue;
        default:
            ok = m->options.private_read && m->options.private_read(m->options.private_user, opcode, &m->codec, r, &event) && event.kind == QA_Q2_SVC_PRIVATE;
            break;
        }
        if (ok && !r->failed) ok = emit_record(m, r, start, opcode, &event, emit, user);
        qa_buffer_free(&owned); free(inventory);
        if (!ok) return qa_net_reader_fail(r, "Invalid or unbound Q2 server service");
        if (r->failed) return false;
    }
    return !r->failed;
}

bool qa_q2_messages_read(qa_q2_messages *m, qa_bytes bytes, qa_q2_server_emit_fn emit, void *user, qa_error *error) {
    if (!m || m->reading) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid or reentrant Q2 decoder read"); return false; }
    qa_net_reader r;
    qa_net_reader_init(&r, bytes, error);
    m->reading = true; m->inflated_this_read = 0;
    bool ok = parse_server(m, &r, emit, user, 0) && qa_net_reader_finish(&r);
    m->reading = false;
    return ok;
}

static bool write_sound(qa_q2_codec *c, qa_net_writer *w, const qa_q2_kex_sound *from) {
    qa_q2_kex_sound s = *from;
    bool kex = is_kex(c);
    if (s.channel > 7 || s.entity > (UINT32_MAX >> 3)) return qa_net_writer_fail(w, "Invalid Q2 sound channel");
    if (!kex && s.index > UINT8_MAX) {
        if (!is_rerelease(c) && !extended(c)) return qa_net_writer_fail(w, "Q2 sound needs extended index");
        s.flags |= 32u;
    }
    if (s.has_position) s.flags |= 4u;
    if (s.entity || s.channel) s.flags |= 8u;
    if (s.volume != 1) s.flags |= 1u;
    if (s.attenuation != 1) s.flags |= 2u;
    if (s.time_offset != 0) s.flags |= 16u;
    uint32_t channel = (s.entity << 3) | s.channel;
    if (kex && channel > UINT16_MAX) s.flags |= 64u;
    if (!s.has_position && (s.flags & 4u)) return qa_net_writer_fail(w, "Positioned Q2 sound has no position");
    if (!qa_net_write_u8(w, 9)) return false;
    if (kex) return qa_q2_kex_write_sound(c, w, &s);
    if (channel > UINT16_MAX) return qa_net_writer_fail(w, "Q2 sound channel exceeds wire range");
    qa_net_write_u8(w, s.flags);
    if (s.flags & 32u) qa_net_write_u16(w, s.index); else qa_net_write_u8(w, (uint8_t)s.index);
    if (s.flags & 1u) qa_q2_write_scaled(w, s.volume, 255, 8, false);
    if (s.flags & 2u) qa_q2_write_scaled(w, s.attenuation, 64, 8, false);
    if (s.flags & 16u) qa_q2_write_scaled(w, s.time_offset, 1000, 8, false);
    if (s.flags & 8u) qa_net_write_u16(w, (uint16_t)channel);
    if (s.flags & 4u) qa_q2_game_position_write(c, w, s.position);
    return !w->failed;
}

bool qa_q2_server_event_write(qa_q2_codec *c, qa_net_writer *w, const qa_q2_server_event *e) {
    if (!c || !e) return qa_net_writer_fail(w, "Missing Q2 server event");
    bool kex = is_kex(c), rerelease = is_rerelease(c);
    if (!kex && !rerelease && (e->kind == QA_Q2_SVC_ACHIEVEMENT || e->kind == QA_Q2_SVC_FOG ||
        e->kind == QA_Q2_SVC_DAMAGE || e->kind == QA_Q2_SVC_POI || e->kind == QA_Q2_SVC_HELP_PATH ||
        e->kind == QA_Q2_SVC_LOCALIZED_PRINT))
        return qa_net_writer_fail(w, "Q2 rerelease service needs its game protocol");
    switch (e->kind) {
    case QA_Q2_SVC_NOP: return qa_net_write_u8(w, 6);
    case QA_Q2_SVC_DISCONNECT: return qa_net_write_u8(w, 7);
    case QA_Q2_SVC_RECONNECT: return qa_net_write_u8(w, 8);
    case QA_Q2_SVC_LEVEL_RESTART:
        if (!kex) return qa_net_writer_fail(w, "Q2 level restart needs KEX wire");
        return qa_net_write_u8(w, 24);
    case QA_Q2_SVC_SERVERDATA: return qa_q2_write_serverdata(c, w, &e->data.serverdata);
    case QA_Q2_SVC_PRINT:
        return qa_net_write_u8(w, 10) && qa_net_write_u8(w, e->data.print.level) && qa_net_write_string(w, e->data.print.text);
    case QA_Q2_SVC_CENTERPRINT: case QA_Q2_SVC_COMMAND: case QA_Q2_SVC_LAYOUT: case QA_Q2_SVC_ACHIEVEMENT: {
        uint8_t op = e->kind == QA_Q2_SVC_CENTERPRINT ? 15 : e->kind == QA_Q2_SVC_COMMAND ? 11 : e->kind == QA_Q2_SVC_LAYOUT ? 4 : 33;
        return qa_net_write_u8(w, op) && qa_net_write_string(w, e->data.print.text);
    }
    case QA_Q2_SVC_CONFIGSTRING:
        return qa_net_write_u8(w, 13) && qa_net_write_u16(w, e->data.config.index) && qa_net_write_string(w, e->data.config.value);
    case QA_Q2_SVC_BASELINE: return qa_q2_write_baseline(c, w, &e->data.baseline);
    case QA_Q2_SVC_TEMP_ENTITY:
        return qa_net_write_u8(w, 3) && qa_q2_temp_entity_write(c, w, extended(c), &e->data.temporary);
    case QA_Q2_SVC_INVENTORY:
        if (e->data.inventory.count > 32768 || (e->data.inventory.count && !e->data.inventory.counts))
            return qa_net_writer_fail(w, "Invalid Q2 inventory");
        qa_net_write_u8(w, 5);
        for (size_t i = 0; i < e->data.inventory.count; ++i) qa_net_write_i16(w, e->data.inventory.counts[i]);
        return !w->failed;
    case QA_Q2_SVC_DOWNLOAD:
        if (e->data.download.bytes.size > INT16_MAX ||
            (!e->data.download.missing && e->data.download.bytes.size && !e->data.download.bytes.data))
            return qa_net_writer_fail(w, "Q2 download block exceeds wire range");
        return qa_net_write_u8(w, 16) &&
               qa_net_write_i16(w, e->data.download.missing ? -1 : (int16_t)e->data.download.bytes.size) &&
               qa_net_write_u8(w, e->data.download.percent) && (e->data.download.missing ||
               qa_net_write_data(w, e->data.download.bytes.data, e->data.download.bytes.size));
    case QA_Q2_SVC_SETTING:
        if (!rerelease && c->protocol.kind != QA_NET_R1Q2_35 && c->protocol.kind != QA_NET_Q2PRO_36)
            return qa_net_writer_fail(w, "Selected Q2 wire has no server settings");
        return qa_net_write_u8(w, rerelease ? 37 : 24) && qa_net_write_i32(w, e->data.setting.index) && qa_net_write_i32(w, e->data.setting.value);
    case QA_Q2_SVC_SEAT:
        if (!kex) return qa_net_writer_fail(w, "Q2 seat marker needs KEX wire");
        return qa_net_write_u8(w, 21) && qa_q2_kex_write_splitclient(w, e->data.seat);
    case QA_Q2_SVC_MUZZLEFLASH: {
        uint32_t entity = e->data.muzzle.entity, flash = e->data.muzzle.flash;
        if (entity > UINT16_MAX) return qa_net_writer_fail(w, "Invalid Q2 muzzleflash entity");
        if (e->data.muzzle.monster && flash > UINT8_MAX) {
            if (kex || rerelease) {
                if (flash > UINT16_MAX) return qa_net_writer_fail(w, "Invalid Q2 muzzleflash index");
                return qa_net_write_u8(w, 32) && qa_net_write_u16(w, (uint16_t)entity) && qa_net_write_u16(w, (uint16_t)flash);
            }
            if (!extended(c) || flash > 2047 || entity > 8191)
                return qa_net_writer_fail(w, "Q2 monster muzzleflash needs extended game layout");
            entity |= (flash & 0x700u) << 5; flash &= 255u;
        }
        if ((!e->data.muzzle.monster && flash > 127) || flash > UINT8_MAX)
            return qa_net_writer_fail(w, "Q2 muzzleflash index exceeds wire range");
        return qa_net_write_u8(w, e->data.muzzle.monster ? 2 : 1) && qa_net_write_u16(w, (uint16_t)entity) &&
               qa_net_write_u8(w, (uint8_t)(flash | (e->data.muzzle.silenced ? 128u : 0)));
    }
    case QA_Q2_SVC_SOUND: return write_sound(c, w, &e->data.sound);
    case QA_Q2_SVC_FOG: return qa_net_write_u8(w, 27) && qa_q2_fog_write(w, &e->data.fog);
    case QA_Q2_SVC_DAMAGE:
        if (e->data.damage.count > 4) return qa_net_writer_fail(w, "Too many retained Q2 damage indicators");
        return qa_net_write_u8(w, 25) && qa_q2_kex_write_damage(w, e->data.damage.indicators, e->data.damage.count);
    case QA_Q2_SVC_LOCALIZED_PRINT: return qa_net_write_u8(w, 26) && qa_q2_kex_write_locprint(w, &e->data.localized);
    case QA_Q2_SVC_POI: return qa_net_write_u8(w, 30) && qa_q2_kex_write_poi(w, &e->data.poi);
    case QA_Q2_SVC_HELP_PATH: return qa_net_write_u8(w, 31) && qa_q2_kex_write_help_path(w, &e->data.help_path);
    case QA_Q2_SVC_FRAME: return qa_net_writer_fail(w, "Q2 frame encoding requires baseline context");
    case QA_Q2_SVC_PRIVATE: return qa_net_writer_fail(w, "Private Q2 services require their bound writer");
    }
    return qa_net_writer_fail(w, "Unknown Q2 server event");
}

static bool valid_utf8(const char *text) {
    const uint8_t *p = (const uint8_t *)text;
    while (*p) {
        uint32_t code = *p++;
        unsigned continuation;
        uint32_t minimum;
        if (code < 128) continue;
        if (code >= 0xc2 && code <= 0xdf) { code &= 31; continuation = 1; minimum = 0x80; }
        else if (code >= 0xe0 && code <= 0xef) { code &= 15; continuation = 2; minimum = 0x800; }
        else if (code >= 0xf0 && code <= 0xf4) { code &= 7; continuation = 3; minimum = 0x10000; }
        else return false;
        for (unsigned i = 0; i < continuation; ++i) {
            if ((*p & 0xc0u) != 0x80u) return false;
            code = (code << 6) | (*p++ & 63u);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}

bool qa_q2_client_messages_read(qa_q2_codec *c, qa_bytes bytes, uint32_t sequence, size_t seats,
                                qa_q2_client_emit_fn emit, void *user, qa_error *error) {
    if (!c || !seats || seats > QA_Q2_MAX_SEATS) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 client message context"); return false;
    }
    qa_net_reader r;
    qa_net_reader_init(&r, bytes, error);
    bool moved = false, kex = c->protocol.kind == QA_NET_Q2KEX_2023;
    while (qa_net_reader_remaining(&r)) {
        size_t start = r.bit / 8;
        uint8_t raw = qa_net_read_u8(&r), opcode = c->protocol.kind == QA_NET_Q2PRO_36 ? raw & 31u : raw;
        qa_q2_client_record record = {0};
        qa_q2_client_event *e = &record.event;
        switch (opcode) {
        case 1: e->kind = QA_Q2_CLC_NOP; break;
        case 3: case 4:
            e->kind = opcode == 3 ? QA_Q2_CLC_USERINFO : QA_Q2_CLC_COMMAND;
            if (opcode == 4 && kex) {
                uint8_t seat = qa_net_read_u8(&r);
                if (!seat || seat > seats) return qa_net_reader_fail(&r, "Q2 connection does not own selected split player");
                record.seat = (uint8_t)(seat - 1);
            }
            e->data.text = read_text(&r);
            if (!e->data.text || (kex && !valid_utf8(e->data.text))) return qa_net_reader_fail(&r, "Invalid Q2 client control string");
            break;
        case 5:
            if (c->protocol.kind != QA_NET_R1Q2_35 && c->protocol.kind != QA_NET_Q2PRO_36 && !is_rerelease(c))
                return qa_net_reader_fail(&r, "Q2 client setting is unsupported by selected wire");
            e->kind = QA_Q2_CLC_SETTING;
            e->data.setting.index = qa_net_read_i16(&r); e->data.setting.value = qa_net_read_i16(&r);
            break;
        case 12:
            if (c->protocol.kind != QA_NET_Q2PRO_36 && !is_rerelease(c))
                return qa_net_reader_fail(&r, "Q2 userinfo delta is unsupported by selected wire");
            e->kind = QA_Q2_CLC_USERINFO_DELTA;
            e->data.userinfo_delta.name = read_text(&r); e->data.userinfo_delta.value = read_text(&r);
            break;
        case 2: {
            if (moved) return qa_net_reader_fail(&r, "Multiple Q2 move commands in one packet");
            moved = true;
            bool checksum = c->protocol.kind == QA_NET_Q2_34;
            uint8_t expected = checksum ? qa_net_read_u8(&r) : 0;
            size_t checksum_start = r.bit / 8;
            int32_t frame = qa_net_read_i32(&r);
            static const qa_q2_usercmd zero;
            for (size_t seat = 0; seat < (kex ? seats : 1); ++seat) {
                e->kind = QA_Q2_CLC_MOVE; e->data.move.last_frame = frame;
                uint8_t light = kex ? qa_net_read_u8(&r) : 0;
                for (unsigned i = 0; i < 3; ++i) {
                    const qa_q2_usercmd *from = i ? &e->data.move.commands[i - 1] : &zero;
                    if (!qa_q2_read_usercmd(c, &r, from, &e->data.move.commands[i])) return false;
                    if (kex) e->data.move.commands[i].lightlevel = light;
                }
                if (checksum && qa_q2_sequence_checksum(raw_span(&r, checksum_start), sequence) != expected)
                    return qa_net_reader_fail(&r, "Q2 command sequence checksum mismatch");
                record.seat = (uint8_t)seat; record.raw = raw_span(&r, start);
                if (emit && !emit(user, &record, error)) return callback_failed(&r, "Q2 client command callback failed");
            }
            continue;
        }
        case 10: case 11:
            if (moved) return qa_net_reader_fail(&r, "Multiple Q2 move commands in one packet");
            moved = true; e->kind = QA_Q2_CLC_BATCH;
            if (c->protocol.kind == QA_NET_Q2PRO_36) {
                qa_q2pro_batch batch;
                if (!qa_q2pro_read_batch(c, &r, opcode == 10, raw >> 5, &batch)) return false;
                e->data.batch.last_frame = batch.lastframe; e->data.batch.frame_count = batch.frame_count;
                for (size_t i = 0; i < batch.frame_count; ++i) {
                    e->data.batch.frames[i].count = batch.frames[i].count;
                    for (size_t j = 0; j < batch.frames[i].count; ++j) {
                        e->data.batch.frames[i].commands[j] = batch.frames[i].commands[j];
                        e->data.batch.frames[i].commands[j].lightlevel = batch.lightlevel;
                    }
                }
            } else if (is_rerelease(c)) {
                if (!qa_q2_repro_read_batch(c, &r, opcode == 10, &e->data.batch)) return false;
            } else return qa_net_reader_fail(&r, "Batched Q2 movement is unsupported by selected wire");
            break;
        default: return qa_net_reader_fail(&r, "Unknown Q2 client opcode");
        }
        if (r.failed) return false;
        record.raw = raw_span(&r, start);
        if (emit && !emit(user, &record, error)) return callback_failed(&r, "Q2 client command callback failed");
    }
    return qa_net_reader_finish(&r);
}

bool qa_q2_client_move_write(qa_q2_codec *c, qa_net_writer *w, uint32_t sequence, int32_t last_frame,
                              const qa_q2_usercmd (*commands)[3], size_t seats) {
    bool kex = c && c->protocol.kind == QA_NET_Q2KEX_2023;
    if (!c || !commands || !seats || seats > QA_Q2_MAX_SEATS || (!kex && seats != 1) || w->bit % 8)
        return qa_net_writer_fail(w, "Invalid Q2 client move");
    qa_net_write_u8(w, 2);
    bool checksum = c->protocol.kind == QA_NET_Q2_34;
    size_t checksum_byte = w->bit / 8;
    if (checksum) qa_net_write_u8(w, 0);
    size_t start = w->bit / 8;
    qa_net_write_i32(w, last_frame);
    static const qa_q2_usercmd zero;
    for (size_t seat = 0; seat < seats; ++seat) {
        if (kex) qa_net_write_u8(w, commands[seat][2].lightlevel);
        for (unsigned i = 0; i < 3; ++i)
            if (!qa_q2_write_usercmd(c, w, i ? &commands[seat][i - 1] : &zero, &commands[seat][i])) return false;
    }
    if (w->failed) return false;
    if (checksum) w->data[checksum_byte] = qa_q2_sequence_checksum((qa_bytes){w->data + start, w->bit / 8 - start}, sequence);
    return true;
}

bool qa_q2_client_event_write(qa_q2_codec *c, qa_net_writer *w, const qa_q2_client_event *e, uint8_t seat) {
    if (!c || !e) return qa_net_writer_fail(w, "Missing Q2 client event");
    bool kex = c->protocol.kind == QA_NET_Q2KEX_2023;
    switch (e->kind) {
    case QA_Q2_CLC_NOP: return qa_net_write_u8(w, 1);
    case QA_Q2_CLC_USERINFO: case QA_Q2_CLC_COMMAND:
        if (!e->data.text || (kex && !valid_utf8(e->data.text))) return qa_net_writer_fail(w, "Invalid Q2 control string");
        if (e->kind == QA_Q2_CLC_COMMAND && ((kex && seat >= QA_Q2_MAX_SEATS) || (!kex && seat)))
            return qa_net_writer_fail(w, "Invalid Q2 command seat");
        qa_net_write_u8(w, e->kind == QA_Q2_CLC_USERINFO ? 3 : 4);
        if (kex && e->kind == QA_Q2_CLC_COMMAND) qa_net_write_u8(w, (uint8_t)(seat + 1));
        return qa_net_write_string(w, e->data.text);
    case QA_Q2_CLC_SETTING:
        if (c->protocol.kind != QA_NET_R1Q2_35 && c->protocol.kind != QA_NET_Q2PRO_36 && !is_rerelease(c))
            return qa_net_writer_fail(w, "Q2 wire does not support client settings");
        return qa_net_write_u8(w, 5) && qa_net_write_i16(w, e->data.setting.index) && qa_net_write_i16(w, e->data.setting.value);
    case QA_Q2_CLC_USERINFO_DELTA:
        if (c->protocol.kind != QA_NET_Q2PRO_36 && !is_rerelease(c)) return qa_net_writer_fail(w, "Q2 wire does not support userinfo deltas");
        return qa_net_write_u8(w, 12) && qa_net_write_string(w, e->data.userinfo_delta.name) && qa_net_write_string(w, e->data.userinfo_delta.value);
    case QA_Q2_CLC_BATCH:
        if (!e->data.batch.frame_count || e->data.batch.frame_count > QA_Q2_REPRO_BATCH_FRAMES)
            return qa_net_writer_fail(w, "Invalid Q2 command batch count");
        if (c->protocol.kind == QA_NET_Q2PRO_36) {
            qa_q2pro_batch batch = {0};
            batch.nodelta = e->data.batch.last_frame == -1; batch.lastframe = e->data.batch.last_frame;
            batch.frame_count = e->data.batch.frame_count;
            for (size_t i = 0; i < batch.frame_count; ++i) {
                size_t count = e->data.batch.frames[i].count;
                if (count > QA_Q2PRO_BATCH_COMMANDS) return qa_net_writer_fail(w, "Invalid Q2 command batch size");
                batch.frames[i].count = count;
                for (size_t j = 0; j < count; ++j) {
                    batch.frames[i].commands[j] = e->data.batch.frames[i].commands[j];
                    batch.lightlevel = batch.frames[i].commands[j].lightlevel;
                }
            }
            return qa_q2pro_write_batch_message(c, w, &batch);
        }
        if (!is_rerelease(c)) return qa_net_writer_fail(w, "Q2 wire does not support command batches");
        return qa_net_write_u8(w, e->data.batch.last_frame == -1 ? 10 : 11) &&
               qa_q2_repro_write_batch(c, w, e->data.batch.last_frame == -1, &e->data.batch);
    case QA_Q2_CLC_MOVE: return qa_net_writer_fail(w, "Q2 move encoding requires its sequence checksum context");
    }
    return qa_net_writer_fail(w, "Unknown Q2 client event");
}

void qa_q2_command_replay_init(qa_q2_command_replay *replay) {
    if (replay) { memset(replay, 0, sizeof(*replay)); replay->last_frame = -1; }
}

bool qa_q2_command_replay_run(qa_q2_command_replay *r, const qa_q2_client_event *e, uint32_t dropped,
                              qa_q2_think_fn think, void *user, qa_error *error) {
    if (!r || !e || !think || (e->kind != QA_Q2_CLC_MOVE && e->kind != QA_Q2_CLC_BATCH)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 command replay"); return false;
    }
    if (e->kind == QA_Q2_CLC_MOVE) {
        r->last_frame = e->data.move.last_frame;
        if (dropped < 20) {
            while (dropped > 2) { if (!think(user, &r->previous, error)) return false; --dropped; }
            if (dropped > 1 && !think(user, &e->data.move.commands[0], error)) return false;
            if (dropped > 0 && !think(user, &e->data.move.commands[1], error)) return false;
        }
        if (!think(user, &e->data.move.commands[2], error)) return false;
        r->previous = e->data.move.commands[2];
        return true;
    }
    const qa_q2_repro_batch *batch = &e->data.batch;
    if (!batch->frame_count || batch->frame_count > QA_Q2_REPRO_BATCH_FRAMES) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 replay batch"); return false;
    }
    const qa_q2_usercmd *last = NULL;
    for (size_t i = 0; i < batch->frame_count; ++i) {
        if (batch->frames[i].count > QA_Q2_BATCH_COMMANDS) { qa_error_set(error, QA_ERROR_ARGUMENT, i, "Invalid Q2 replay command count"); return false; }
        if (batch->frames[i].count) last = &batch->frames[i].commands[batch->frames[i].count - 1];
    }
    r->last_frame = batch->last_frame;
    if (!last) return true;
    size_t backups = batch->frame_count - 1;
    if (dropped < 20) {
        while (dropped > backups) { if (!think(user, &r->previous, error)) return false; --dropped; }
        while (dropped) {
            const qa_q2_batch_frame *frame = &batch->frames[backups - dropped];
            for (size_t i = 0; i < frame->count; ++i) if (!think(user, &frame->commands[i], error)) return false;
            --dropped;
        }
    }
    const qa_q2_batch_frame *newest = &batch->frames[backups];
    for (size_t i = 0; i < newest->count; ++i) if (!think(user, &newest->commands[i], error)) return false;
    r->previous = *last;
    return true;
}

bool qa_q2_rate_drop(qa_q2_rate_window *window, uint32_t frame, uint32_t rate, bool loopback) {
    if (!window || loopback) return false;
    uint64_t total = 0;
    for (unsigned i = 0; i < 10; ++i) total += window->sizes[i];
    if (total <= rate) return false;
    if (window->suppressed != UINT32_MAX) ++window->suppressed;
    window->sizes[frame % 10] = 0;
    return true;
}
void qa_q2_rate_sent(qa_q2_rate_window *window, uint32_t frame, uint32_t bytes) {
    if (window) window->sizes[frame % 10] = bytes;
}
uint32_t qa_q2_rate_take_suppressed(qa_q2_rate_window *window) {
    if (!window) return 0;
    uint32_t value = window->suppressed; window->suppressed = 0; return value;
}

bool qa_q2_download_sender_init(qa_q2_download_sender *s, size_t size, size_t offset, size_t block_bytes,
                                qa_q2_download_read_fn read, qa_q2_download_close_fn close, void *user, qa_error *error) {
    if (!s || !read || !close || offset > size || !block_bytes || block_bytes > INT16_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 download source"); return false;
    }
    *s = (qa_q2_download_sender){user, read, close, size, offset, block_bytes, false};
    return true;
}
void qa_q2_download_sender_close(qa_q2_download_sender *s) {
    if (s && !s->ended) { s->ended = true; s->close(s->user); }
}
bool qa_q2_download_sender_next(qa_q2_download_sender *s, qa_buffer *out, uint8_t *percent, bool *present, qa_error *error) {
    if (!s || !out || !percent || !present || !s->read || !s->close) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 download request"); return false;
    }
    *present = false;
    if (s->ended) return true;
    size_t length = s->size - s->offset;
    if (length > s->block_bytes) length = s->block_bytes;
    qa_buffer bytes = {0};
    if (!s->read(s->user, s->offset, length, &bytes, error)) { qa_buffer_free(&bytes); return false; }
    if (bytes.size != length || (length && !bytes.data)) {
        qa_buffer_free(&bytes); qa_error_set(error, QA_ERROR_IO, s->offset, "Q2 download source changed or read was short"); return false;
    }
    s->offset += length;
    /* Division before multiplication bounds both operations for SIZE_MAX files. */
    size_t quotient = s->size / 100, remainder = s->size % 100;
    uint8_t progress = 0;
    if (s->size) {
        for (unsigned p = 1; p <= 100; ++p) {
            size_t threshold = quotient * p + (remainder * p + 99) / 100;
            if (s->offset < threshold) break;
            progress = (uint8_t)p;
        }
    }
    if (s->offset == s->size) qa_q2_download_sender_close(s);
    *out = bytes; *percent = progress; *present = true;
    return true;
}
