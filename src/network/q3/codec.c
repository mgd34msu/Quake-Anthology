#include "huffman_internal.h"

bool qa_q3_slots_prepare(qa_arena *storage, qa_q3_snapshot_slot *slots, size_t count,
    size_t entity_limit, qa_q3_product product, qa_error *error)
{
    if (!count || !entity_limit || count > SIZE_MAX / entity_limit ||
        count * entity_limit > (SIZE_MAX - _Alignof(qa_q3_entity) + 1) / sizeof(qa_q3_entity)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Q3 snapshot reservation size overflow");
        return false;
    }
    size_t bytes = count * entity_limit * sizeof(qa_q3_entity);
    if (!qa_arena_reserve(storage, bytes + _Alignof(qa_q3_entity) - 1, error)) return false;
    qa_q3_entity *entities = qa_arena_alloc(storage, bytes, _Alignof(qa_q3_entity), error);
    if (!entities) return false;
    for (size_t i = 0; i < count; ++i) {
        slots[i] = (qa_q3_snapshot_slot){.entities = entities + i * entity_limit, .limit = entity_limit};
        qa_q3_slot_clear(slots + i, product);
    }
    qa_arena_seal(storage);
    return true;
}

bool qa_q3_slot_store(qa_q3_snapshot_slot *slot, const qa_q3_snapshot *value, qa_error *error)
{
    if (value->entity_count > slot->limit || (value->entity_count && !value->entities)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid retained Q3 snapshot entities");
        return false;
    }
    if (value->entity_count > slot->capacity) slot->capacity = value->entity_count;
    if (value->entity_count) memcpy(slot->entities, value->entities,
        value->entity_count * sizeof(*slot->entities));
    slot->value = *value; slot->value.entities = slot->entities;
    return true;
}

void qa_q3_slot_clear(qa_q3_snapshot_slot *slot, qa_q3_product product)
{
    slot->value = (qa_q3_snapshot){.entities = slot->entities, .player = {.product = product}};
    slot->capacity = 0; slot->ping = 0;
    slot->sent_time = 0; slot->ack_time = -1; slot->message_size = 0;
}

void qa_q3_reader_init(qa_q3_reader *reader, qa_bytes bytes, bool oob, qa_error *error)
{
    if (reader == NULL) return;
    qa_net_reader_init(&reader->raw, bytes, error);
    reader->oob = oob;
    reader->diagnostics = (qa_q3_diagnostics){0};
    if (bytes.size > QA_Q3_MESSAGE_BYTES)
        qa_net_reader_fail(&reader->raw, "Q3 message exceeds maximum length");
}

void qa_q3_writer_init(qa_q3_writer *writer, void *data, size_t capacity,
                       bool oob, qa_error *error)
{
    if (writer == NULL) return;
    *writer = (qa_q3_writer){ .oob = oob };
    if (capacity > QA_Q3_MESSAGE_BYTES) {
        writer->raw.error = error;
        qa_net_writer_fail(&writer->raw, "Q3 message exceeds maximum capacity");
        return;
    }
    qa_net_writer_init(&writer->raw, data, capacity, error);
}

static bool valid_width(int bits, bool oob, bool reading)
{
    if (bits == 0) return reading && !oob;
    if (bits < -31 || bits > 32) return false;
    unsigned width = (unsigned)(bits < 0 ? -bits : bits);
    return !oob || width == 8 || width == 16 || width == 32;
}

uint32_t qa_q3_read_bits(qa_q3_reader *reader, int bits)
{
    if (reader == NULL || reader->raw.failed) return 0;
    if (!valid_width(bits, reader->oob, true)) {
        qa_net_reader_fail(&reader->raw, "Invalid Q3 message bit width");
        return 0;
    }
    unsigned width = (unsigned)(bits < 0 ? -bits : bits);
    unsigned sign_width = width;
    uint32_t value;
    if (reader->oob) value = qa_net_read_bits(&reader->raw, width);
    else {
        unsigned low = width & 7u;
        value = qa_net_read_bits(&reader->raw, low);
        for (unsigned shift = low; shift < width && !reader->raw.failed; shift += 8)
            value |= qa_q3_huffman_symbol(&reader->raw) << shift;
        /* MSG_ReadBits subtracts the raw low bits before sign extension. */
        sign_width -= low;
    }
    if (bits < 0 && sign_width != 0 &&
        (value & (UINT32_C(1) << (sign_width - 1))) != 0)
        value |= UINT32_MAX << sign_width;
    return reader->raw.failed ? 0 : value;
}

bool qa_q3_write_bits(qa_q3_writer *writer, uint32_t value, int bits)
{
    if (writer == NULL || writer->raw.failed) return false;
    if (!valid_width(bits, writer->oob, false))
        return qa_net_writer_fail(&writer->raw, "Invalid Q3 message bit width");
    if (writer->size > writer->raw.capacity || writer->raw.capacity - writer->size < 4) {
        writer->overflowed = true;
        return qa_net_writer_fail(&writer->raw, "Q3 message exceeds output capacity");
    }
    unsigned width = (unsigned)(bits < 0 ? -bits : bits);
    if (writer->oob) {
        for (unsigned shift = 0; shift < width; shift += 8)
            writer->raw.data[writer->size++] = (uint8_t)(value >> shift);
        /* The source OOB long writer advances its bit cursor by eight. */
        writer->raw.bit += width == 32 ? 8 : width;
        return true;
    }
    uint32_t codes[4];
    unsigned lengths[4], count = 0, total = width & 7u;
    uint32_t remaining = value >> (width & 7u);
    for (unsigned shift = width & 7u; shift < width; shift += 8) {
        qa_q3_huffman_code((uint8_t)remaining, &codes[count], &lengths[count]);
        total += lengths[count++];
        remaining >>= 8;
    }
    size_t size = (writer->raw.bit + total) / 8 + 1;
    if (size > writer->raw.capacity) {
        writer->overflowed = true;
        return qa_net_writer_fail(&writer->raw, "Q3 Huffman message exceeds output capacity");
    }
    if (!qa_net_write_bits(&writer->raw, value, width & 7u)) return false;
    for (unsigned index = 0; index < count; ++index)
        if (!qa_net_write_bits(&writer->raw, codes[index], lengths[index])) return false;
    if (writer->raw.bit % 8 == 0) writer->raw.data[writer->raw.bit / 8] = 0;
    writer->size = size;
    return true;
}

static int read_byte(qa_q3_reader *reader)
{
    uint32_t value = qa_q3_read_bits(reader, 8);
    if (reader->raw.failed) return -1;
    size_t count = reader->raw.bit / 8 + (reader->oob ? 0u : 1u);
    return count > reader->raw.bytes.size ? -1 : (int)(value & 255u);
}

bool qa_q3_read_string(qa_q3_reader *reader, char *out, size_t capacity, bool big)
{
    if (reader == NULL) return false;
    if (out == NULL || capacity == 0)
        return qa_net_reader_fail(&reader->raw, "Invalid Q3 string output");
    size_t limit = big ? 8192u : 1024u;
    size_t used = 0;
    out[0] = '\0';
    while (used < limit - 1 && !reader->raw.failed) {
        int value = read_byte(reader);
        if (value <= 0) break;
        if (used + 1 >= capacity) {
            out[used] = '\0';
            return qa_net_reader_fail(&reader->raw, "Q3 string exceeds output capacity");
        }
        out[used++] = (char)(value == '%' || (!big && value > 127) ? '.' : value);
    }
    out[used] = '\0';
    return !reader->raw.failed;
}

bool qa_q3_write_string(qa_q3_writer *writer, const char *text, bool big)
{
    if (writer == NULL || writer->raw.failed) return false;
    size_t limit = big ? 8192u : 1024u;
    size_t length = 0;
    if (text != NULL)
        while (length < limit && text[length] != '\0') ++length;
    if (length < limit) {
        for (size_t index = 0; index < length; ++index) {
            unsigned char value = (unsigned char)text[index];
            if (!qa_q3_write_bits(writer, value > 127 ? '.' : value, 8)) return false;
        }
    }
    return qa_q3_write_bits(writer, 0, 8);
}

bool qa_q3_read_data(qa_q3_reader *reader, void *out, size_t size)
{
    if (reader == NULL || reader->raw.failed) return false;
    if ((size != 0 && out == NULL) || size > QA_Q3_MESSAGE_BYTES)
        return qa_net_reader_fail(&reader->raw, "Invalid Q3 data output");
    uint8_t *bytes = out;
    for (size_t index = 0; index < size; ++index) {
        bytes[index] = (uint8_t)read_byte(reader);
        if (reader->raw.failed) return false;
    }
    return true;
}

bool qa_q3_write_data(qa_q3_writer *writer, qa_bytes bytes)
{
    if (writer == NULL || writer->raw.failed) return false;
    if ((bytes.size != 0 && bytes.data == NULL) || bytes.size > QA_Q3_MESSAGE_BYTES)
        return qa_net_writer_fail(&writer->raw, "Invalid Q3 data input");
    for (size_t index = 0; index < bytes.size; ++index)
        if (!qa_q3_write_bits(writer, bytes.data[index], 8)) return false;
    return true;
}

size_t qa_q3_writer_size(const qa_q3_writer *writer)
{
    return writer == NULL ? 0 : writer->size;
}
