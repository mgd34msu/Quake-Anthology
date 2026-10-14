#include "qa/network_q2_demo.h"
#include "classic_internal.h"

bool qa_q2_demo_read_packet(qa_net_reader *r, qa_bytes *message,
                           bool *present, bool *terminated) {
    if (!r || !message || !present || !terminated) return false;
    *present = *terminated = false;
    if (r->failed) return false;
    if (!qa_net_reader_remaining(r)) return true;
    int32_t length = qa_net_read_i32(r);
    if (r->failed) return false;
    if (length == -1) { *terminated = true; return true; }
    if (length < 0) return qa_net_reader_fail(r, "Invalid Q2 demo message length");
    qa_bytes packet;
    if (!qa_net_read_bytes(r, (size_t)length, &packet)) return false;
    *message = packet;
    *present = true;
    return true;
}

static bool recorded_serverdata(qa_net_reader *r, uint32_t version,
                                qa_q2_recording_header *out) {
    uint32_t revision = 0;
    uint8_t attract = 0;
    if (version == 26 || version == 34 || version == 35 || version == 36) {
        qa_net_reader prefix = *r;
        qa_q2_serverdata data;
        if (qa_net_reader_remaining(&prefix) < 5)
            return qa_net_reader_fail(r, "Truncated Q2 recorded serverdata");
        attract = prefix.bytes.data[prefix.bit / 8 + 4];
        if (!qa_q2_classic_read_serverdata_body(&prefix, &data)) { r->failed = true; return false; }
        if (version == 35) {
            (void)qa_net_read_u8(&prefix);
            revision = qa_net_read_u16(&prefix);
            if (!prefix.failed && (revision < 1903 || revision > 1905))
                return qa_net_reader_fail(r, "Unsupported recorded R1Q2 revision");
        } else if (version == 36) {
            revision = qa_net_read_u16(&prefix);
            if (!prefix.failed && (revision < 1015 || revision > 1026 || revision == 1016))
                return qa_net_reader_fail(r, "Unsupported recorded Q2PRO revision");
        }
        if (prefix.failed) { r->failed = true; return false; }
    }
    qa_q2_recording_header header = {.kind = QA_Q2_RECORDING_CLIENT, .recorded_version = version};
    qa_q2_codec codec;
    if (!qa_q2_protocol_from_version(version == 26 ? 34 : version, revision,
            &header.protocol, r->error) || !qa_q2_codec_init(&codec, header.protocol, r->error)) {
        r->failed = true; return false;
    }
    codec.demo26 = version == 26;
    if (!qa_q2_read_serverdata(&codec, r, &header.data)) return false;
    header.protocol = codec.protocol;
    if (version == 34 && attract == 2) {
        if (header.data.clientnum != -1)
            return qa_net_reader_fail(r, "Q2 server demo contains a recorded player view");
        header.kind = QA_Q2_RECORDING_SERVER;
        header.data.client_count = 0;
    }
    *out = header;
    return true;
}

static bool recorded_mvd(qa_bytes bytes, qa_arena *storage,
                         qa_q2_recording_header *out, qa_error *error) {
    qa_q2_mvd_framer *framer = NULL;
    if (!qa_q2_mvd_framer_create(true, QA_Q2_MVD_MESSAGE_BYTES, &framer, error)) return false;
    qa_bytes message = {0};
    size_t consumed = 0;
    bool present = false;
    bool okay = qa_q2_mvd_framer_push(framer, bytes, &consumed, &present, &message, error);
    qa_net_reader wire;
    qa_net_reader_init(&wire, message, error);
    qa_q2_mvd_command command;
    qa_q2_mvd_header source;
    if (okay && !present) okay = qa_net_reader_fail(&wire, "MVD recording has no gamestate");
    if (okay) okay = qa_q2_mvd_read_command(&wire, &command) &&
        qa_q2_mvd_read_header_arena(&wire, command, storage, &source);
    if (okay) {
        qa_q2_recording_header header = {.kind = QA_Q2_RECORDING_MVD,
            .recorded_version = 37, .protocol = source.profile.protocol,
            .data = {.servercount = source.servercount, .attractloop = true,
                .clientnum = source.dummy, .server_fps = 10}};
        memcpy(header.data.gamedir, source.gamedir, sizeof(header.data.gamedir));
        for (size_t i = 0; i < source.config_count; ++i) {
            if (source.configstrings[i].index) continue;
            size_t size = strlen(source.configstrings[i].value);
            if (size >= sizeof(header.data.levelname)) {
                okay = qa_net_reader_fail(&wire, "MVD level title exceeds Q2 serverdata");
                break;
            }
            memcpy(header.data.levelname, source.configstrings[i].value, size + 1);
        }
        if (okay) *out = header;
    }
    qa_q2_mvd_framer_destroy(framer);
    return okay;
}

bool qa_q2_recording_probe(qa_bytes bytes, qa_q2_recording_header *out, qa_error *error) {
    if (!out || (bytes.size && !bytes.data) || bytes.size > SIZE_MAX / 8) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 recording span"); return false;
    }
    qa_arena storage = {0};
    bool okay = false;
    if (bytes.size >= 4 && qa_load_u32le(bytes.data) == QA_Q2_MVD_MAGIC) {
        okay = recorded_mvd(bytes, &storage, out, error);
        goto done;
    }
    qa_net_reader file;
    qa_net_reader_init(&file, bytes, error);
    char *text = NULL;
    size_t text_capacity = 0;
    for (;;) {
        qa_bytes message = {0};
        bool present, terminated;
        if (!qa_q2_demo_read_packet(&file, &message, &present, &terminated)) goto done;
        if (!present) { qa_net_reader_fail(&file, "Q2 demo has no serverdata"); goto done; }
        qa_net_reader wire;
        qa_net_reader_init(&wire, message, error);
        while (qa_net_reader_remaining(&wire)) {
            uint8_t opcode = qa_net_read_u8(&wire);
            if (opcode == 12) {
                uint32_t version = qa_net_read_u32(&wire);
                okay = !wire.failed && recorded_serverdata(&wire, version, out);
                goto done;
            }
            if (opcode == 6) continue;
            if (opcode != 10 && opcode != 11 && opcode != 15) {
                qa_net_reader_fail(&wire, "Q2 demo preamble requires serverdata first");
                goto done;
            }
            if (opcode == 10) (void)qa_net_read_u8(&wire);
            if (text_capacity < message.size + 1) {
                text = qa_arena_grow(&storage, text, text_capacity, message.size + 1, 1, error);
                if (!text) goto done;
                text_capacity = message.size + 1;
            }
            if (!qa_net_read_string(&wire, text, text_capacity)) goto done;
        }
    }
done:
    qa_arena_destroy(&storage);
    return okay;
}

bool qa_q2_server_demo_read(qa_bytes message, qa_arena *storage, qa_q2_wire_frame *out,
                            qa_bytes *multicasts, bool *is_frame, qa_error *error) {
    if (!storage || !out || !multicasts || !is_frame ||
        (message.size && !message.data) || message.size > SIZE_MAX / 8) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 server demo decoder"); return false;
    }
    *is_frame = false;
    if (!message.size || message.data[0] != 20) { *multicasts = message; return true; }
    qa_net_reader wire;
    qa_net_reader_init(&wire, message, error);
    (void)qa_net_read_u8(&wire);
    qa_q2_wire_frame frame = {.valid = true, .delta_frame = -1};
    frame.server_frame = qa_net_read_i32(&wire);
    if (frame.server_frame < 0 || qa_net_read_u8(&wire) != 18)
        return qa_net_reader_fail(&wire, "Invalid Q2 server demo frame prefix");
    if (wire.failed) return false;
    size_t capacity = qa_net_reader_remaining(&wire) / 2;
    if (capacity > Q2C_MAX_EDICTS - 1u) capacity = Q2C_MAX_EDICTS - 1u;
    if (capacity) {
        frame.entities = qa_arena_alloc(storage, capacity * sizeof(*frame.entities),
                                        _Alignof(qa_q2_entity), error);
        if (!frame.entities) return false;
    }
    qa_q2_codec codec;
    if (!qa_q2_codec_init(&codec, (qa_net_protocol_id){.kind = QA_NET_Q2_34}, error)) return false;
    uint32_t previous = 0;
    for (;;) {
        uint32_t number;
        uint64_t bits;
        if (!qa_q2_read_entity_header(&codec, &wire, &number, &bits)) return false;
        if (!number) break;
        if (number <= previous || (bits & Q2C_U_REMOVE))
            return qa_net_reader_fail(&wire, "Invalid full Q2 server demo entity");
        if (!qa_q2_read_entity(&codec, &wire, NULL, number, bits,
                              frame.entities + frame.entity_count)) return false;
        ++frame.entity_count;
        previous = number;
    }
    qa_bytes trailing;
    if (!qa_net_read_bytes(&wire, qa_net_reader_remaining(&wire), &trailing)) return false;
    *out = frame;
    *multicasts = trailing;
    *is_frame = true;
    return true;
}

bool qa_q2_server_demo_signon_write(qa_net_writer *w, const qa_q2_serverdata *data,
                                   const qa_q2_mvd_config *configs, size_t count) {
    if (!data || (count && !configs) || count > QA_Q2_CLASSIC_CONFIGSTRINGS ||
        !memchr(data->gamedir, 0, sizeof(data->gamedir)) ||
        !memchr(data->levelname, 0, sizeof(data->levelname)))
        return qa_net_writer_fail(w, "Invalid Q2 server demo signon");
    if (!qa_net_write_u8(w, 12) || !qa_net_write_u32(w, 34) ||
        !qa_net_write_i32(w, data->servercount) || !qa_net_write_u8(w, 2) ||
        !qa_net_write_string(w, data->gamedir) || !qa_net_write_i16(w, -1) ||
        !qa_net_write_string(w, data->levelname)) return false;
    for (size_t i = 0; i < count; ++i) {
        const qa_q2_mvd_config *config = configs + i;
        if (config->index >= QA_Q2_CLASSIC_CONFIGSTRINGS || !config->value ||
            (i && config->index <= configs[i - 1].index))
            return qa_net_writer_fail(w, "Invalid Q2 server demo configstring order");
        if (*config->value && (!qa_net_write_u8(w, 13) || !qa_net_write_u16(w, config->index) ||
                              !qa_net_write_string(w, config->value))) return false;
    }
    return !w->failed;
}

bool qa_q2_server_demo_frame_write(qa_net_writer *w, int32_t server_frame,
                                  qa_q2_entity_span entities, qa_bytes multicasts) {
    if (server_frame < 0 || (entities.count && !entities.data) ||
        (multicasts.size && !multicasts.data))
        return qa_net_writer_fail(w, "Invalid Q2 server demo frame");
    qa_q2_codec codec;
    if (!qa_q2_codec_init(&codec, (qa_net_protocol_id){.kind = QA_NET_Q2_34}, w->error)) {
        w->failed = true; return false;
    }
    if (!qa_net_write_u8(w, 20) || !qa_net_write_i32(w, server_frame) ||
        !qa_net_write_u8(w, 18)) return false;
    uint32_t previous = 0;
    for (size_t i = 0; i < entities.count; ++i) {
        const qa_q2_entity *entity = entities.data + i;
        if (entity->number <= previous || entity->number >= Q2C_MAX_EDICTS)
            return qa_net_writer_fail(w, "Invalid Q2 server demo entity order");
        previous = entity->number;
        if ((entity->modelindex || entity->effects || entity->sound || entity->event) &&
            !qa_q2_write_entity(&codec, w, NULL, entity, false, true)) return false;
    }
    return qa_q2_write_entity_end(&codec, w) &&
        qa_net_write_data(w, multicasts.data, multicasts.size);
}
