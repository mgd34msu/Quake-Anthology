#include "internal.h"

bool q2_save_fail(q2_save_io *io, const char *message) {
    qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "%s", message);
    return false;
}
bool q2_save_raw(q2_save_io *io, void *data, size_t size) {
    if (io->reading) {
        if (io->offset > io->input.size || size > io->input.size - io->offset)
            return q2_save_fail(io, "Truncated Q2 continuation");
        if (size) memcpy(data, io->input.data + io->offset, size);
    } else {
        if (size > SIZE_MAX - io->offset)
            return q2_save_fail(io, "Q2 continuation extent overflow");
        size_t needed = io->offset + size;
        if (needed > io->capacity) {
            size_t capacity = io->capacity ? io->capacity : 4096;
            while (capacity < needed) {
                if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
                capacity *= 2;
            }
            void *next = realloc(io->output.data, capacity);
            if (!next) {
                qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "Allocating Q2 continuation");
                return false;
            }
            io->output.data = next;
            io->capacity = capacity;
        }
        if (size) memcpy(io->output.data + io->offset, data, size);
        io->output.size = needed;
    }
    io->offset += size;
    return true;
}
bool q2_save_u32(q2_save_io *io, uint32_t *value) {
    uint8_t bytes[4];
    if (!io->reading) qa_store_u32le(bytes, *value);
    if (!q2_save_raw(io, bytes, sizeof(bytes))) return false;
    if (io->reading) *value = qa_load_u32le(bytes);
    return true;
}
bool q2_save_u64(q2_save_io *io, uint64_t *value) {
    uint8_t bytes[8];
    if (!io->reading) qa_store_u64le(bytes, *value);
    if (!q2_save_raw(io, bytes, sizeof(bytes))) return false;
    if (io->reading) *value = qa_load_u64le(bytes);
    return true;
}
bool q2_save_i32(q2_save_io *io, int32_t *value) {
    int32_t signed_value = (int32_t)*value;
    uint32_t bits;
    memcpy(&bits, &signed_value, sizeof(bits));
    if (!q2_save_u32(io, &bits)) return false;
    if (io->reading) { memcpy(&signed_value, &bits, sizeof(bits)); *value = signed_value; }
    return true;
}
bool q2_save_i64(q2_save_io *io, int64_t *value) {
    uint64_t bits;
    memcpy(&bits, value, sizeof(bits));
    if (!q2_save_u64(io, &bits)) return false;
    if (io->reading) memcpy(value, &bits, sizeof(bits));
    return true;
}
bool q2_save_f32(q2_save_io *io, float *value) {
    uint32_t bits;
    if (!io->reading && !isfinite(*value)) return q2_save_fail(io, "Nonfinite Q2 continuation float");
    memcpy(&bits, value, sizeof(bits));
    if (!q2_save_u32(io, &bits)) return false;
    if (io->reading) memcpy(value, &bits, sizeof(bits));
    return isfinite(*value) || q2_save_fail(io, "Nonfinite Q2 continuation float");
}
bool q2_save_f64(q2_save_io *io, double *value) {
    uint64_t bits;
    if (!io->reading && !isfinite(*value)) return q2_save_fail(io, "Nonfinite Q2 continuation number");
    memcpy(&bits, value, sizeof(bits));
    if (!q2_save_u64(io, &bits)) return false;
    if (io->reading) memcpy(value, &bits, sizeof(bits));
    return isfinite(*value) || q2_save_fail(io, "Nonfinite Q2 continuation number");
}
bool q2_save_bool(q2_save_io *io, bool *value) {
    uint8_t byte = *value ? 1 : 0;
    if (!q2_save_raw(io, &byte, 1)) return false;
    if (byte > 1) return q2_save_fail(io, "Invalid Q2 continuation boolean");
    if (io->reading) *value = byte != 0;
    return true;
}
bool q2_save_vec(q2_save_io *io, qa_vec3 *s) {
    return q2_save_f32(io, &s->x) && q2_save_f32(io, &s->y) && q2_save_f32(io, &s->z);
}
bool q2_save_string(q2_save_io *io, qa_string_id *id) {
    qa_strings *strings = qa_session_strings(io->game->services.session);
    qa_bytes text = {0};
    uint32_t size = UINT32_MAX;
    if (!io->reading && *id) {
        if (!qa_strings_cstr(strings, *id)) return q2_save_fail(io, "Unknown Q2 continuation string");
        text = qa_strings_text(strings, *id);
        if (text.size >= UINT32_MAX) return q2_save_fail(io, "Oversized Q2 continuation string");
        size = (uint32_t)text.size;
    }
    if (!q2_save_u32(io, &size)) return false;
    if (size == UINT32_MAX) { if (io->reading) *id = 0; return true; }
    if (io->reading) {
        if (size > io->input.size - io->offset) return q2_save_fail(io, "Truncated Q2 string");
        text = (qa_bytes){io->input.data + io->offset, size};
        if (memchr(text.data, 0, text.size)) return q2_save_fail(io, "Embedded NUL in Q2 string");
        if (!qa_strings_intern(strings, text, id, io->error)) return false;
        io->offset += size;
        return true;
    }
    return q2_save_raw(io, (void *)text.data, size);
}
bool q2_save_text(q2_save_io *io, char *text, size_t capacity) {
    uint32_t size = 0;
    if (!io->reading) {
        const char *end = memchr(text, 0, capacity);
        if (!end || (size_t)(end - text) >= UINT32_MAX) return q2_save_fail(io, "Invalid Q2 text field");
        size = (uint32_t)(end - text);
    }
    if (!q2_save_u32(io, &size)) return false;
    if (size >= capacity) return q2_save_fail(io, "Oversized Q2 text field");
    if (!q2_save_raw(io, text, size)) return false;
    if (memchr(text, 0, size)) return q2_save_fail(io, "Embedded NUL in Q2 text field");
    if (io->reading) text[size] = 0;
    return true;
}
bool q2_save_ref(q2_save_io *io, qa_q2_saved_reference *s) {
    Q2B(present);
    if (!s->present) { if (io->reading) s->actor = (qa_saved_actor_id){0}; return true; }
    Q2S(u32, actor.slot); Q2T(actor.generation);
    return s->actor.slot < io->game->capacity ||
           q2_save_fail(io, "Invalid Q2 saved actor identity");
}
bool q2_save_count(q2_save_io *io, size_t *count, size_t minimum, size_t element_size, void **data) {
    if (!io->reading && (*count > UINT32_MAX || (*count && !*data)))
        return q2_save_fail(io, "Invalid Q2 continuation array");
    uint32_t value = io->reading ? 0 : (uint32_t)*count;
    if (!q2_save_u32(io, &value)) return false;
    if (io->reading) {
        if ((minimum && value > (io->input.size - io->offset) / minimum) ||
            (element_size && value > SIZE_MAX / element_size))
            return q2_save_fail(io, "Invalid Q2 continuation array extent");
        *count = value;
        *data = value ? calloc(value, element_size) : NULL;
        if (value && !*data) {
            qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "Allocating Q2 continuation array");
            return false;
        }
    }
    return true;
}
