#include "internal.h"

bool q1_save_fail(q1_save_io *io, const char *message) {
    qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "%s", message);
    return false;
}
bool q1_save_bytes(q1_save_io *io, void *value, size_t count) {
    if (io->reading) {
        if (io->offset > io->input.size || count > io->input.size - io->offset)
            return q1_save_fail(io, "Truncated Q1 checkpoint");
        if (count)
            memcpy(value, io->input.data + io->offset, count);
    } else {
        if (count > SIZE_MAX - io->offset)
            return q1_save_fail(io, "Q1 checkpoint size overflow");
        size_t needed = io->offset + count;
        if (needed > io->capacity) {
            size_t capacity = io->capacity ? io->capacity : 4096;
            while (capacity < needed) {
                if (capacity > SIZE_MAX / 2) {
                    capacity = needed;
                    break;
                }
                capacity *= 2;
            }
            void *memory = realloc(io->output.data, capacity);
            if (!memory) {
                qa_error_set(io->error, QA_ERROR_MEMORY, needed, "Allocating Q1 checkpoint");
                return false;
            }
            io->output.data = memory;
            io->capacity = capacity;
        }
        if (count)
            memcpy(io->output.data + io->offset, value, count);
        io->output.size = needed;
    }
    io->offset += count;
    return true;
}
bool q1_save_u8(q1_save_io *io, uint8_t *value) { return q1_save_bytes(io, value, 1); }
bool q1_save_u16(q1_save_io *io, uint16_t *value) {
    uint8_t bytes[2];
    if (!io->reading)
        qa_store_u16le(bytes, *value);
    if (!q1_save_bytes(io, bytes, sizeof(bytes)))
        return false;
    if (io->reading)
        *value = qa_load_u16le(bytes);
    return true;
}
bool q1_save_u32(q1_save_io *io, uint32_t *value) {
    uint8_t bytes[4];
    if (!io->reading)
        qa_store_u32le(bytes, *value);
    if (!q1_save_bytes(io, bytes, sizeof(bytes)))
        return false;
    if (io->reading)
        *value = qa_load_u32le(bytes);
    return true;
}
bool q1_save_u64(q1_save_io *io, uint64_t *value) {
    uint8_t bytes[8];
    if (!io->reading)
        qa_store_u64le(bytes, *value);
    if (!q1_save_bytes(io, bytes, sizeof(bytes)))
        return false;
    if (io->reading)
        *value = qa_load_u64le(bytes);
    return true;
}
#define SIGNED_IO(bits)                                                                            \
    bool q1_save_i##bits(q1_save_io *io, int##bits##_t *value) {                                   \
        uint##bits##_t encoded;                                                                    \
        memcpy(&encoded, value, sizeof(encoded));                                                  \
        if (!q1_save_u##bits(io, &encoded))                                                        \
            return false;                                                                          \
        if (io->reading)                                                                           \
            memcpy(value, &encoded, sizeof(encoded));                                              \
        return true;                                                                               \
    }
SIGNED_IO(16)
SIGNED_IO(32)
SIGNED_IO(64)
#undef SIGNED_IO

bool q1_save_float(q1_save_io *io, float *value) {
    uint32_t encoded;
    _Static_assert(sizeof(float) == sizeof(encoded), "Q1 saves require binary32 floats");
    memcpy(&encoded, value, sizeof(encoded));
    if (!q1_save_u32(io, &encoded))
        return false;
    float number;
    memcpy(&number, &encoded, sizeof(number));
    if (!isfinite(number))
        return q1_save_fail(io, "Nonfinite Q1 checkpoint float");
    if (io->reading)
        *value = number;
    return true;
}
static bool number64(q1_save_io *io, double *value, bool allow_never) {
    uint64_t encoded;
    _Static_assert(sizeof(double) == sizeof(encoded), "Q1 saves require binary64 clocks");
    memcpy(&encoded, value, sizeof(encoded));
    if (!q1_save_u64(io, &encoded))
        return false;
    double number;
    memcpy(&number, &encoded, sizeof(number));
    if (!isfinite(number) && !(allow_never && number == INFINITY))
        return q1_save_fail(io, "Nonfinite Q1 checkpoint clock");
    if (io->reading)
        *value = number;
    return true;
}
bool q1_save_double(q1_save_io *io, double *value) { return number64(io, value, false); }
bool q1_save_deadline(q1_save_io *io, double *value) { return number64(io, value, true); }
bool q1_save_bool(q1_save_io *io, bool *value) {
    uint8_t encoded = *value ? 1 : 0;
    if (!q1_save_u8(io, &encoded))
        return false;
    if (encoded > 1)
        return q1_save_fail(io, "Invalid Q1 checkpoint boolean");
    if (io->reading)
        *value = encoded != 0;
    return true;
}
bool q1_save_vector(q1_save_io *io, qa_vec3 *value) {
    return q1_save_float(io, &value->x) && q1_save_float(io, &value->y) &&
           q1_save_float(io, &value->z);
}
bool q1_save_bounds(q1_save_io *io, qa_bounds *value) {
    return q1_save_vector(io, &value->mins) && q1_save_vector(io, &value->maxs) &&
           ((value->mins.x <= value->maxs.x && value->mins.y <= value->maxs.y &&
             value->mins.z <= value->maxs.z) ||
            q1_save_fail(io, "Inverted Q1 checkpoint bounds"));
}
bool q1_save_string(q1_save_io *io, qa_string_id *value) {
    uint32_t index = 0;
    if (!io->reading && *value) {
        qa_strings *strings = qa_session_strings(io->game->services.session);
        if (!qa_strings_text(strings, *value).data)
            return q1_save_fail(io, "Unknown Q1 checkpoint string");
        if (!qa_strings_intern(io->dictionary, qa_strings_text(strings, *value), &index, io->error))
            return false;
    }
    if (!q1_save_u32(io, &index))
        return false;
    if (io->reading) {
        if (index >= io->string_count)
            return q1_save_fail(io, "Q1 checkpoint string index is out of range");
        *value = io->strings[index];
    }
    return true;
}
bool q1_save_actor(q1_save_io *io, qa_actor_id *value) {
    bool present = value->registry != 0;
    qa_saved_actor_id saved = {0};
    const qa_actor_registry *actors = qa_session_actors(io->game->services.session);
    if (!io->reading && present && !qa_actors_save_reference(actors, *value, &saved, io->error))
        return false;
    if (!q1_save_bool(io, &present))
        return false;
    if (!present) {
        if (io->reading)
            *value = (qa_actor_id){0};
        return true;
    }
    if (!q1_save_u64(io, &saved.generation) || !q1_save_u32(io, &saved.slot))
        return false;
    if (io->reading) {
        const qa_actor_record *record = qa_actors_resolve_saved(actors, saved);
        if (record)
            *value = record->id;
        else if (!qa_actors_reference_saved(actors, saved, true, value, io->error))
            return false;
    }
    return true;
}

bool q1_save_literal(q1_save_io *io, const char **value) {
    uint32_t index = 0;
    if (!io->reading &&
        (!*value || !qa_strings_intern_cstr(io->dictionary, *value, &index, io->error)))
        return false;
    if (!q1_save_u32(io, &index))
        return false;
    if (io->reading) {
        if (!index || index >= io->string_count)
            return q1_save_fail(io, "Invalid Q1 checkpoint immutable identity");
        *value =
            qa_strings_cstr(qa_session_strings(io->game->services.session), io->strings[index]);
        if (!*value)
            return q1_save_fail(io, "Q1 immutable identity contains a NUL byte");
    }
    return true;
}

bool q1_save_owned_actor(q1_save_io *io, qa_actor_id *value) {
    if (!q1_save_actor(io, value))
        return false;
    const qa_actor_record *record =
        qa_actors_get(qa_session_actors(io->game->services.session), *value);
    if (!record)
        return q1_save_fail(io, "Q1 checkpoint continuation has no shared live actor");
    qa_actor_owner owner = record->owner;
    qa_actor_definition definition = record->definition;
    bool source = record->has_source;
    uint32_t slot = record->source_slot;
    if (!q1_save_string(io, &owner) || !q1_save_string(io, &definition) ||
        !q1_save_bool(io, &source) || !q1_save_u32(io, &slot))
        return false;
    return (owner == record->owner && definition == record->definition &&
            source == record->has_source && (!source || slot == record->source_slot)) ||
           q1_save_fail(io, "Q1 checkpoint shared actor identity mismatch");
}
