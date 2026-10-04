#include "internal.h"
#include "qa/binary.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool qa_source_save_writer(qa_source_save_io *io, qa_session *session, qa_error *error)
{
    if (!io) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "source save writer requires its state"); return false; }
    *io = (qa_source_save_io){.session = session, .direction = QA_SOURCE_SAVE_WRITE, .error = error};
    return true;
}
bool qa_source_save_writer_reserve(qa_source_save_io *io, size_t total_capacity)
{
    if (!io || io->failed) return false;
    if (io->direction != QA_SOURCE_SAVE_WRITE)
        return persistence_io_fail(io, QA_ERROR_ARGUMENT, "source save reserve requires owned output");
    if (total_capacity <= io->capacity) return true;
    uint8_t *bytes = realloc(io->output.data, total_capacity);
    if (!bytes) return persistence_io_fail(io, QA_ERROR_MEMORY, "allocating explicit source continuation bytes");
    io->output.data = bytes; io->capacity = total_capacity;
    return true;
}
bool qa_source_save_reader(qa_source_save_io *io, qa_session *session, qa_bytes bytes, qa_error *error)
{
    if (!io) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "source save reader requires its state and bytes"); return false;
    }
    *io = (qa_source_save_io){.session = session, .direction = QA_SOURCE_SAVE_READ, .input = bytes, .error = error};
    if (bytes.size && !bytes.data)
        return persistence_io_fail(io, QA_ERROR_ARGUMENT, "source save reader requires its state and bytes");
    return true;
}
void qa_source_save_dispose(qa_source_save_io *io)
{ if (io) { qa_buffer_free(&io->output); *io = (qa_source_save_io){0}; } }
bool qa_source_save_finish(qa_source_save_io *io, qa_buffer *out)
{
    if (!io || io->failed) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (io->offset != io->input.size) return persistence_io_fail(io, QA_ERROR_FORMAT, "trailing source continuation bytes");
        io->direction = QA_SOURCE_SAVE_FINISHED;
        return true;
    }
    if (io->direction != QA_SOURCE_SAVE_WRITE || !out)
        return persistence_io_fail(io, QA_ERROR_ARGUMENT, "source save writer needs an owned output");
    *out = io->output; io->output = (qa_buffer){0}; io->capacity = 0;
    io->direction = QA_SOURCE_SAVE_FINISHED;
    return true;
}
bool qa_source_save_span(qa_source_save_io *io, size_t size, qa_bytes *span)
{
    if (!io || io->failed) return false;
    if (io->direction != QA_SOURCE_SAVE_READ || !span)
        return persistence_io_fail(io, QA_ERROR_ARGUMENT, "source save span requires borrowed input");
    if (io->offset > SIZE_MAX - size)
        return persistence_io_fail(io, QA_ERROR_FORMAT, "source continuation field extent overflow");
    if (io->offset > io->input.size || size > io->input.size - io->offset)
        return persistence_io_fail(io, QA_ERROR_FORMAT, "truncated source continuation field");
    *span = (qa_bytes){io->input.data ? io->input.data + io->offset : NULL, size};
    io->offset += size;
    return true;
}
bool qa_source_save_bytes(qa_source_save_io *io, void *data, size_t size)
{
    if (!io || io->failed) return false;
    if (size && !data)
        return persistence_io_fail(io, QA_ERROR_FORMAT, "source continuation field extent overflow");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        qa_bytes span;
        if (!qa_source_save_span(io, size, &span)) return false;
        if (size) memcpy(data, span.data, size);
        return true;
    } else if (io->direction == QA_SOURCE_SAVE_WRITE) {
        if (io->offset > SIZE_MAX - size)
            return persistence_io_fail(io, QA_ERROR_FORMAT, "source continuation field extent overflow");
        size_t wanted = io->offset + size;
        if (wanted > io->capacity) {
            size_t capacity = io->capacity ? io->capacity : 256;
            while (capacity < wanted) {
                if (capacity > SIZE_MAX / 2) { capacity = wanted; break; }
                capacity *= 2;
            }
            if (!qa_source_save_writer_reserve(io, capacity)) return false;
        }
        if (size) memcpy(io->output.data + io->offset, data, size);
        io->output.size = wanted;
    } else return persistence_io_fail(io, QA_ERROR_ARGUMENT, "invalid source continuation direction");
    io->offset += size;
    return true;
}
bool qa_source_save_u8(qa_source_save_io *io, uint8_t *value)
{ return value ? qa_source_save_bytes(io, value, 1) : persistence_io_fail(io, QA_ERROR_ARGUMENT, "missing source u8 field"); }
#define UNSIGNED_IO(bits, width) \
bool qa_source_save_u##bits(qa_source_save_io *io, uint##bits##_t *value) { \
    if (!io || !value || io->failed) return false; \
    uint8_t bytes[width]; \
    if (io->direction == QA_SOURCE_SAVE_WRITE) qa_store_u##bits##le(bytes, *value); \
    if (!qa_source_save_bytes(io, bytes, width)) return false; \
    if (io->direction == QA_SOURCE_SAVE_READ) *value = qa_load_u##bits##le(bytes); \
    return true; }
UNSIGNED_IO(16, 2)
UNSIGNED_IO(32, 4)
UNSIGNED_IO(64, 8)
#undef UNSIGNED_IO
bool qa_source_save_bool(qa_source_save_io *io, bool *value)
{
    if (!io || !value) return persistence_io_fail(io, QA_ERROR_ARGUMENT, "missing source boolean field");
    uint8_t word = io->direction == QA_SOURCE_SAVE_WRITE && *value ? 1 : 0;
    if (!qa_source_save_u8(io, &word)) return false;
    if (word > 1) return persistence_io_fail(io, QA_ERROR_FORMAT, "invalid source boolean field");
    if (io->direction == QA_SOURCE_SAVE_READ) *value = word != 0;
    return true;
}
bool qa_source_save_i32(qa_source_save_io *io, int32_t *value)
{
    if (!io || !value) return persistence_io_fail(io, QA_ERROR_ARGUMENT, "missing source signed field");
    uint32_t word = io->direction == QA_SOURCE_SAVE_WRITE ? (uint32_t)*value : 0;
    if (!qa_source_save_u32(io, &word)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ)
        *value = word <= INT32_MAX ? (int32_t)word : -(int32_t)(UINT32_MAX - word) - 1;
    return true;
}
bool qa_source_save_i64(qa_source_save_io *io, int64_t *value)
{
    if (!io || !value) return persistence_io_fail(io, QA_ERROR_ARGUMENT, "missing source signed field");
    uint64_t word = io->direction == QA_SOURCE_SAVE_WRITE ? (uint64_t)*value : 0;
    if (!qa_source_save_u64(io, &word)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ)
        *value = word <= INT64_MAX ? (int64_t)word : -(int64_t)(UINT64_MAX - word) - 1;
    return true;
}
bool qa_source_save_f32(qa_source_save_io *io, float *value)
{
    if (!io || !value) return persistence_io_fail(io, QA_ERROR_ARGUMENT, "missing source float field");
    uint32_t word = 0;
    if (io->direction == QA_SOURCE_SAVE_WRITE) memcpy(&word, value, 4);
    if (!qa_source_save_u32(io, &word)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) memcpy(value, &word, 4);
    return true;
}
bool qa_source_save_f64(qa_source_save_io *io, double *value)
{
    if (!io || !value) return persistence_io_fail(io, QA_ERROR_ARGUMENT, "missing source double field");
    uint64_t word = 0;
    if (io->direction == QA_SOURCE_SAVE_WRITE) memcpy(&word, value, 8);
    if (!qa_source_save_u64(io, &word)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) memcpy(value, &word, 8);
    return true;
}
bool qa_source_save_vec3(qa_source_save_io *io, qa_vec3 *value)
{ return value && qa_source_save_f32(io, &value->x) && qa_source_save_f32(io, &value->y) && qa_source_save_f32(io, &value->z); }
bool qa_source_save_count(qa_source_save_io *io, size_t *value, size_t maximum)
{
    if (!io || !value) return persistence_io_fail(io, QA_ERROR_ARGUMENT, "missing source count field");
    uint64_t word = io->direction == QA_SOURCE_SAVE_WRITE ? *value : 0;
    if (!qa_source_save_u64(io, &word)) return false;
    if (word > maximum || word > SIZE_MAX) return persistence_io_fail(io, QA_ERROR_FORMAT, "source count exceeds its declared owner bound");
    if (io->direction == QA_SOURCE_SAVE_READ) *value = (size_t)word;
    return true;
}
