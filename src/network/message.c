#include "qa/network.h"

#include <float.h>
#include <limits.h>
#include <string.h>

_Static_assert(CHAR_BIT == 8, "network codecs require 8-bit bytes");
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24,
               "network codecs require IEEE binary32");
_Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53,
               "unified protocol requires IEEE binary64");

bool qa_net_reader_fail(qa_net_reader *reader, const char *message)
{
    if (reader != NULL && !reader->failed) {
        reader->failed = true;
        qa_error_set(reader->error, QA_ERROR_FORMAT, reader->bit / 8, "%s", message);
    }
    return false;
}

bool qa_net_writer_fail(qa_net_writer *writer, const char *message)
{
    if (writer != NULL && !writer->failed) {
        writer->failed = true;
        qa_error_set(writer->error, QA_ERROR_FORMAT, writer->bit / 8, "%s", message);
    }
    return false;
}

void qa_net_reader_init(qa_net_reader *reader, qa_bytes bytes, qa_error *error)
{
    if (reader == NULL) return;
    *reader = (qa_net_reader){ .bytes = bytes, .error = error };
    if ((bytes.size != 0 && bytes.data == NULL) || bytes.size > SIZE_MAX / 8)
        qa_net_reader_fail(reader, "Invalid network input span");
}

void qa_net_writer_init(qa_net_writer *writer, void *data, size_t capacity, qa_error *error)
{
    if (writer == NULL) return;
    *writer = (qa_net_writer){ .data = data, .capacity = capacity, .error = error };
    if ((capacity != 0 && data == NULL) || capacity > SIZE_MAX / 8) {
        qa_net_writer_fail(writer, "Invalid network output span");
        return;
    }
    if (capacity != 0) memset(data, 0, capacity);
}

size_t qa_net_reader_remaining(const qa_net_reader *reader)
{
    if (reader == NULL || reader->failed || reader->bit > reader->bytes.size * 8) return 0;
    return (reader->bytes.size * 8 - reader->bit) / 8;
}

size_t qa_net_writer_size(const qa_net_writer *writer)
{
    return writer == NULL ? 0 : writer->bit / 8 + (writer->bit % 8 != 0);
}

bool qa_net_reader_finish(qa_net_reader *reader)
{
    if (reader == NULL || reader->failed) return false;
    if (reader->bit != reader->bytes.size * 8)
        return qa_net_reader_fail(reader, "Trailing network message data");
    return true;
}

static bool reader_room(qa_net_reader *reader, size_t bits)
{
    if (reader == NULL || reader->failed) return false;
    if (reader->bit > reader->bytes.size * 8 || bits > reader->bytes.size * 8 - reader->bit)
        return qa_net_reader_fail(reader, "Truncated network message");
    return true;
}

static bool writer_room(qa_net_writer *writer, size_t bits)
{
    if (writer == NULL || writer->failed) return false;
    if (writer->bit > writer->capacity * 8 || bits > writer->capacity * 8 - writer->bit)
        return qa_net_writer_fail(writer, "Network message exceeds output capacity");
    return true;
}

uint32_t qa_net_read_bits(qa_net_reader *reader, unsigned count)
{
    if (count > 32) { qa_net_reader_fail(reader, "Invalid network bit width"); return 0; }
    if (!reader_room(reader, count)) return 0;
    if (reader->bit % 8 == 0 && (count == 8 || count == 16 || count == 32)) {
        const uint8_t *bytes = reader->bytes.data + reader->bit / 8;
        uint32_t value = count == 8 ? bytes[0] : count == 16 ? qa_load_u16le(bytes) : qa_load_u32le(bytes);
        reader->bit += count;
        return value;
    }
    uint32_t value = 0;
    for (unsigned bit = 0; bit < count; ++bit, ++reader->bit)
        value |= (uint32_t)((reader->bytes.data[reader->bit / 8] >> (reader->bit % 8)) & 1u) << bit;
    return value;
}

int32_t qa_net_read_sbits(qa_net_reader *reader, unsigned count)
{
    if (count == 0 || count > 32) { qa_net_reader_fail(reader, "Invalid signed bit width"); return 0; }
    uint32_t value = qa_net_read_bits(reader, count);
    if (count < 32 && (value & (UINT32_C(1) << (count - 1))) != 0)
        value |= UINT32_MAX << count;
    return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}

bool qa_net_write_bits(qa_net_writer *writer, uint32_t value, unsigned count)
{
    if (count > 32) return qa_net_writer_fail(writer, "Invalid network bit width");
    if (!writer_room(writer, count)) return false;
    if (writer->bit % 8 == 0 && (count == 8 || count == 16 || count == 32)) {
        uint8_t *bytes = writer->data + writer->bit / 8;
        if (count == 8) bytes[0] = (uint8_t)value;
        else if (count == 16) qa_store_u16le(bytes, (uint16_t)value);
        else qa_store_u32le(bytes, value);
        writer->bit += count;
        return true;
    }
    for (unsigned bit = 0; bit < count; ++bit, ++writer->bit) {
        uint8_t mask = (uint8_t)(1u << (writer->bit % 8));
        if ((value & (UINT32_C(1) << bit)) != 0) writer->data[writer->bit / 8] |= mask;
        else writer->data[writer->bit / 8] &= (uint8_t)~mask;
    }
    return true;
}

uint8_t qa_net_read_u8(qa_net_reader *reader) { return (uint8_t)qa_net_read_bits(reader, 8); }
int8_t qa_net_read_i8(qa_net_reader *reader) { return (int8_t)qa_net_read_sbits(reader, 8); }
uint16_t qa_net_read_u16(qa_net_reader *reader) { return (uint16_t)qa_net_read_bits(reader, 16); }
int16_t qa_net_read_i16(qa_net_reader *reader) { return (int16_t)qa_net_read_sbits(reader, 16); }
uint32_t qa_net_read_u32(qa_net_reader *reader) { return qa_net_read_bits(reader, 32); }
int32_t qa_net_read_i32(qa_net_reader *reader) { return qa_net_read_sbits(reader, 32); }
uint64_t qa_net_read_u64(qa_net_reader *reader)
{
    if (!reader_room(reader, 64)) return 0;
    uint64_t low = qa_net_read_u32(reader);
    return low | (uint64_t)qa_net_read_u32(reader) << 32;
}
float qa_net_read_f32(qa_net_reader *reader)
{
    uint32_t bits = qa_net_read_u32(reader);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
double qa_net_read_f64(qa_net_reader *reader)
{
    uint64_t bits = qa_net_read_u64(reader);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
bool qa_net_write_u8(qa_net_writer *writer, uint8_t value) { return qa_net_write_bits(writer, value, 8); }
bool qa_net_write_i8(qa_net_writer *writer, int8_t value) { return qa_net_write_bits(writer, (uint8_t)value, 8); }
bool qa_net_write_u16(qa_net_writer *writer, uint16_t value) { return qa_net_write_bits(writer, value, 16); }
bool qa_net_write_i16(qa_net_writer *writer, int16_t value) { return qa_net_write_bits(writer, (uint16_t)value, 16); }
bool qa_net_write_u32(qa_net_writer *writer, uint32_t value) { return qa_net_write_bits(writer, value, 32); }
bool qa_net_write_i32(qa_net_writer *writer, int32_t value) { return qa_net_write_bits(writer, (uint32_t)value, 32); }
bool qa_net_write_u64(qa_net_writer *writer, uint64_t value)
{
    if (!writer_room(writer, 64)) return false;
    return qa_net_write_u32(writer, (uint32_t)value) && qa_net_write_u32(writer, (uint32_t)(value >> 32));
}
bool qa_net_write_f32(qa_net_writer *writer, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return qa_net_write_u32(writer, bits);
}
bool qa_net_write_f64(qa_net_writer *writer, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return qa_net_write_u64(writer, bits);
}

bool qa_net_read_data(qa_net_reader *reader, void *output, size_t size)
{
    if ((size != 0 && output == NULL) || size > SIZE_MAX / 8)
        return qa_net_reader_fail(reader, "Invalid network output span");
    if (!reader_room(reader, size * 8)) return false;
    if (size == 0) return true;
    if (reader->bit % 8 == 0) {
        memcpy(output, reader->bytes.data + reader->bit / 8, size);
        reader->bit += size * 8;
    } else {
        uint8_t *bytes = output;
        for (size_t index = 0; index < size; ++index) bytes[index] = qa_net_read_u8(reader);
    }
    return true;
}

bool qa_net_read_bytes(qa_net_reader *reader, size_t size, qa_bytes *out)
{
    if (reader == NULL || out == NULL || reader->bit % 8 != 0 || size > SIZE_MAX / 8)
        return qa_net_reader_fail(reader, "Invalid borrowed network span");
    if (!reader_room(reader, size * 8)) return false;
    *out = (qa_bytes){ .data = size == 0 ? NULL : reader->bytes.data + reader->bit / 8, .size = size };
    reader->bit += size * 8;
    return true;
}

bool qa_net_write_data(qa_net_writer *writer, const void *data, size_t size)
{
    if ((size != 0 && data == NULL) || size > SIZE_MAX / 8)
        return qa_net_writer_fail(writer, "Invalid network input span");
    if (!writer_room(writer, size * 8)) return false;
    if (size == 0) return true;
    if (writer->bit % 8 == 0) {
        memmove(writer->data + writer->bit / 8, data, size);
        writer->bit += size * 8;
    } else {
        const uint8_t *bytes = data;
        for (size_t index = 0; index < size; ++index) qa_net_write_u8(writer, bytes[index]);
    }
    return true;
}

bool qa_net_read_string(qa_net_reader *reader, char *out, size_t capacity)
{
    if (out == NULL || capacity == 0) return qa_net_reader_fail(reader, "Invalid network string output");
    size_t used = 0;
    out[0] = '\0';
    while (reader != NULL && !reader->failed) {
        uint8_t byte = qa_net_read_u8(reader);
        if (reader->failed) break;
        if (byte == 0) { out[used] = '\0'; return true; }
        if (used + 1 >= capacity) { out[used] = '\0'; return qa_net_reader_fail(reader, "Network string exceeds capacity"); }
        out[used++] = (char)byte;
    }
    out[used] = '\0';
    return false;
}

bool qa_net_write_string(qa_net_writer *writer, const char *text)
{
    if (text == NULL) return qa_net_writer_fail(writer, "Missing network string");
    size_t size = strlen(text);
    if (size == SIZE_MAX) return qa_net_writer_fail(writer, "Network string is too long");
    return qa_net_write_data(writer, text, size + 1);
}

bool qa_net_protocol_valid(qa_net_protocol_id protocol, qa_error *error)
{
    switch (protocol.kind) {
        case QA_NET_R1Q2_35:
            if (protocol.revision >= 1903 && protocol.revision <= 1905 && protocol.flags == 0) return true;
            break;
        case QA_NET_Q2PRO_36:
            if (protocol.revision >= 1015 && protocol.revision <= 1026 && protocol.flags == 0) return true;
            break;
        case QA_NET_RMQ999:
        case QA_NET_QW29:
            if (protocol.revision == 0 && (protocol.flags & ~UINT32_C(190)) == 0) return true;
            break;
        case QA_NET_NQ15: case QA_NET_FITZ666: case QA_NET_QW28:
        case QA_NET_Q2_34: case QA_NET_Q2REPRO_1038: case QA_NET_Q2KEX_2023:
        case QA_NET_Q2KEX_DEMO_2022: case QA_NET_Q2PRIVATE_4038:
        case QA_NET_Q3_68: case QA_NET_UNIFIED_1:
            if (protocol.revision == 0 && protocol.flags == 0) return true;
            break;
    }
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid network protocol identity");
    return false;
}
