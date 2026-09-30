#include "qa/source_save.h"
#include "qa/binary.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool io_fail(qa_source_save_io *io, qa_status code, const char *text)
{
    if (io && !io->failed) qa_error_set(io->error, code, io->offset, "%s", text);
    if (io) io->failed = true;
    return false;
}

bool qa_source_save_writer(qa_source_save_io *io, qa_session *session, qa_error *error)
{
    if (!io) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "source save writer requires its state"); return false; }
    *io = (qa_source_save_io){.session = session, .direction = QA_SOURCE_SAVE_WRITE, .error = error};
    return true;
}
bool qa_source_save_reader(qa_source_save_io *io, qa_session *session, qa_bytes bytes, qa_error *error)
{
    if (!io || (bytes.size && !bytes.data)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "source save reader requires its state and bytes"); return false;
    }
    *io = (qa_source_save_io){.session = session, .direction = QA_SOURCE_SAVE_READ, .input = bytes, .error = error};
    return true;
}
void qa_source_save_dispose(qa_source_save_io *io)
{ if (io) { qa_buffer_free(&io->output); *io = (qa_source_save_io){0}; } }
bool qa_source_save_finish(qa_source_save_io *io, qa_buffer *out)
{
    if (!io || io->failed) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (io->offset != io->input.size) return io_fail(io, QA_ERROR_FORMAT, "trailing source continuation bytes");
        io->direction = QA_SOURCE_SAVE_FINISHED;
        return true;
    }
    if (io->direction != QA_SOURCE_SAVE_WRITE || !out)
        return io_fail(io, QA_ERROR_ARGUMENT, "source save writer needs an owned output");
    *out = io->output; io->output = (qa_buffer){0}; io->capacity = 0;
    io->direction = QA_SOURCE_SAVE_FINISHED;
    return true;
}
bool qa_source_save_bytes(qa_source_save_io *io, void *data, size_t size)
{
    if (!io || io->failed) return false;
    if ((size && !data) || io->offset > SIZE_MAX - size)
        return io_fail(io, QA_ERROR_FORMAT, "source continuation field extent overflow");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (io->offset > io->input.size || size > io->input.size - io->offset)
            return io_fail(io, QA_ERROR_FORMAT, "truncated source continuation field");
        if (size) memcpy(data, io->input.data + io->offset, size);
    } else if (io->direction == QA_SOURCE_SAVE_WRITE) {
        size_t wanted = io->offset + size;
        if (wanted > io->capacity) {
            size_t capacity = io->capacity ? io->capacity : 256;
            while (capacity < wanted) {
                if (capacity > SIZE_MAX / 2) { capacity = wanted; break; }
                capacity *= 2;
            }
            uint8_t *bytes = realloc(io->output.data, capacity);
            if (!bytes) return io_fail(io, QA_ERROR_MEMORY, "allocating explicit source continuation bytes");
            io->output.data = bytes; io->capacity = capacity;
        }
        if (size) memcpy(io->output.data + io->offset, data, size);
        io->output.size = wanted;
    } else return io_fail(io, QA_ERROR_ARGUMENT, "invalid source continuation direction");
    io->offset += size;
    return true;
}
bool qa_source_save_u8(qa_source_save_io *io, uint8_t *value)
{ return value ? qa_source_save_bytes(io, value, 1) : io_fail(io, QA_ERROR_ARGUMENT, "missing source u8 field"); }
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
    if (!io || !value) return io_fail(io, QA_ERROR_ARGUMENT, "missing source boolean field");
    uint8_t word = io->direction == QA_SOURCE_SAVE_WRITE && *value ? 1 : 0;
    if (!qa_source_save_u8(io, &word)) return false;
    if (word > 1) return io_fail(io, QA_ERROR_FORMAT, "invalid source boolean field");
    if (io->direction == QA_SOURCE_SAVE_READ) *value = word != 0;
    return true;
}
bool qa_source_save_i32(qa_source_save_io *io, int32_t *value)
{
    if (!io || !value) return io_fail(io, QA_ERROR_ARGUMENT, "missing source signed field");
    uint32_t word = io->direction == QA_SOURCE_SAVE_WRITE ? (uint32_t)*value : 0;
    if (!qa_source_save_u32(io, &word)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ)
        *value = word <= INT32_MAX ? (int32_t)word : -(int32_t)(UINT32_MAX - word) - 1;
    return true;
}
bool qa_source_save_i64(qa_source_save_io *io, int64_t *value)
{
    if (!io || !value) return io_fail(io, QA_ERROR_ARGUMENT, "missing source signed field");
    uint64_t word = io->direction == QA_SOURCE_SAVE_WRITE ? (uint64_t)*value : 0;
    if (!qa_source_save_u64(io, &word)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ)
        *value = word <= INT64_MAX ? (int64_t)word : -(int64_t)(UINT64_MAX - word) - 1;
    return true;
}
bool qa_source_save_f32(qa_source_save_io *io, float *value)
{
    if (!io || !value) return io_fail(io, QA_ERROR_ARGUMENT, "missing source float field");
    uint32_t word = 0;
    if (io->direction == QA_SOURCE_SAVE_WRITE) memcpy(&word, value, 4);
    if (!qa_source_save_u32(io, &word)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) memcpy(value, &word, 4);
    return true;
}
bool qa_source_save_f64(qa_source_save_io *io, double *value)
{
    if (!io || !value) return io_fail(io, QA_ERROR_ARGUMENT, "missing source double field");
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
    if (!io || !value) return io_fail(io, QA_ERROR_ARGUMENT, "missing source count field");
    uint64_t word = io->direction == QA_SOURCE_SAVE_WRITE ? *value : 0;
    if (!qa_source_save_u64(io, &word)) return false;
    if (word > maximum || word > SIZE_MAX) return io_fail(io, QA_ERROR_FORMAT, "source count exceeds its declared owner bound");
    if (io->direction == QA_SOURCE_SAVE_READ) *value = (size_t)word;
    return true;
}

static bool string_value(qa_source_save_io *io, bool *present, qa_bytes *bytes)
{
    if (!qa_source_save_bool(io, present) || !*present) return io && !io->failed;
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? bytes->size : 0;
    if (!qa_source_save_count(io, &length, SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE)
        return qa_source_save_bytes(io, (void *)bytes->data, length);
    if (io->offset > io->input.size || length > io->input.size - io->offset)
        return io_fail(io, QA_ERROR_FORMAT, "truncated source string value");
    *bytes = (qa_bytes){io->input.data + io->offset, length}; io->offset += length;
    return true;
}
bool qa_source_save_string(qa_source_save_io *io, qa_string_id *value)
{
    if (!io || !value || !io->session) return io_fail(io, QA_ERROR_ARGUMENT, "missing source string owner");
    bool present = io->direction == QA_SOURCE_SAVE_WRITE && *value != QA_STRING_NONE;
    qa_bytes bytes = {0};
    if (present) {
        bytes = qa_strings_text(qa_session_strings(io->session), *value);
        if (!bytes.data) return io_fail(io, QA_ERROR_FORMAT, "source string ID has no value in its owner");
    }
    if (!string_value(io, &present, &bytes)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (!present) *value = QA_STRING_NONE;
        else if (!qa_strings_intern(qa_session_strings(io->session), bytes, value, io->error)) { io->failed = true; return false; }
    }
    return true;
}
bool qa_source_save_text(qa_source_save_io *io, const char **value)
{
    if (!io || !value || !io->session) return io_fail(io, QA_ERROR_ARGUMENT, "missing source text owner");
    bool present = io->direction == QA_SOURCE_SAVE_WRITE && *value != NULL;
    qa_bytes bytes = present ? (qa_bytes){(const uint8_t *)*value, strlen(*value)} : (qa_bytes){0};
    if (!string_value(io, &present, &bytes)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (!present) *value = NULL;
        else {
            if (memchr(bytes.data, 0, bytes.size)) return io_fail(io, QA_ERROR_FORMAT, "source text contains embedded NUL");
            qa_string_id id;
            if (!qa_strings_intern(qa_session_strings(io->session), bytes, &id, io->error)) { io->failed = true; return false; }
            *value = qa_strings_cstr(qa_session_strings(io->session), id);
        }
    }
    return true;
}
bool qa_source_save_actor(qa_source_save_io *io, qa_actor_id *value)
{
    if (!io || !value || !io->session) return io_fail(io, QA_ERROR_ARGUMENT, "missing source actor owner");
    bool present = io->direction == QA_SOURCE_SAVE_WRITE && value->registry != 0;
    qa_saved_actor_id saved = {0};
    if (present && !qa_actors_save_reference(qa_session_actors(io->session), *value, &saved, io->error)) { io->failed = true; return false; }
    if (!qa_source_save_bool(io, &present) || !qa_source_save_u64(io, &saved.generation) ||
        !qa_source_save_u32(io, &saved.slot)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (!present) {
            if (saved.generation || saved.slot) return io_fail(io, QA_ERROR_FORMAT, "absent source actor contains provenance");
            *value = (qa_actor_id){0};
        } else if (!qa_actors_reference_saved(qa_session_actors(io->session), saved, true, value, io->error)) { io->failed = true; return false; }
    }
    return true;
}
