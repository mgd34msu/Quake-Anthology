#include "internal.h"

bool q1_save_fail(q1_save_io *io, const char *message) {
    io->values.failed = true;
    qa_error_set(io->values.error, QA_ERROR_FORMAT, io->values.offset, "%s", message);
    return false;
}
static bool transferred(q1_save_io *io, bool okay, size_t size) {
    if (okay) return true;
    qa_error *error = io->values.error;
    if (error && error->code == QA_ERROR_MEMORY)
        qa_error_set(error, QA_ERROR_MEMORY, io->values.offset + size, "Allocating Q1 checkpoint");
    else if (error && error->code == QA_ERROR_FORMAT)
        return q1_save_fail(io, io->values.direction == QA_SOURCE_SAVE_READ
            ? "Truncated Q1 checkpoint" : "Q1 checkpoint size overflow");
    return false;
}
bool q1_save_bytes(q1_save_io *io, void *value, size_t count) {
    return transferred(io, qa_source_save_bytes(&io->values, value, count), count);
}
#define UNSIGNED_IO(bits, size) \
    bool q1_save_u##bits(q1_save_io *io, uint##bits##_t *value) { \
        return transferred(io, qa_source_save_u##bits(&io->values, value), size); \
    }
UNSIGNED_IO(8, 1)
UNSIGNED_IO(16, 2)
UNSIGNED_IO(32, 4)
UNSIGNED_IO(64, 8)
#undef UNSIGNED_IO
bool q1_save_i16(q1_save_io *io, int16_t *value) {
    uint16_t encoded;
    memcpy(&encoded, value, sizeof(encoded));
    if (!q1_save_u16(io, &encoded)) return false;
    if (io->values.direction == QA_SOURCE_SAVE_READ)
        memcpy(value, &encoded, sizeof(encoded));
    return true;
}
bool q1_save_i32(q1_save_io *io, int32_t *value) {
    return transferred(io, qa_source_save_i32(&io->values, value), 4);
}
bool q1_save_i64(q1_save_io *io, int64_t *value) {
    return transferred(io, qa_source_save_i64(&io->values, value), 8);
}
bool q1_save_float(q1_save_io *io, float *value) {
    float number = io->values.direction == QA_SOURCE_SAVE_WRITE ? *value : 0;
    if (!transferred(io, qa_source_save_f32(&io->values, &number), 4)) return false;
    if (!isfinite(number)) return q1_save_fail(io, "Nonfinite Q1 checkpoint float");
    if (io->values.direction == QA_SOURCE_SAVE_READ) *value = number;
    return true;
}
static bool number64(q1_save_io *io, double *value, bool allow_never) {
    double number = io->values.direction == QA_SOURCE_SAVE_WRITE ? *value : 0;
    if (!transferred(io, qa_source_save_f64(&io->values, &number), 8)) return false;
    if (!isfinite(number) && !(allow_never && number == INFINITY))
        return q1_save_fail(io, "Nonfinite Q1 checkpoint clock");
    if (io->values.direction == QA_SOURCE_SAVE_READ) *value = number;
    return true;
}
bool q1_save_double(q1_save_io *io, double *value) { return number64(io, value, false); }
bool q1_save_deadline(q1_save_io *io, double *value) { return number64(io, value, true); }
bool q1_save_bool(q1_save_io *io, bool *value) {
    size_t before = io->values.offset;
    if (qa_source_save_bool(&io->values, value)) return true;
    if (io->values.direction == QA_SOURCE_SAVE_READ && io->values.offset != before)
        return q1_save_fail(io, "Invalid Q1 checkpoint boolean");
    return transferred(io, false, 1);
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
    if (io->values.direction == QA_SOURCE_SAVE_WRITE && *value) {
        qa_strings *strings = qa_session_strings(io->game->services.session);
        if (!qa_strings_text(strings, *value).data)
            return q1_save_fail(io, "Unknown Q1 checkpoint string");
        if (!qa_strings_intern(io->dictionary, qa_strings_text(strings, *value), &index, io->values.error))
            return false;
    }
    if (!q1_save_u32(io, &index))
        return false;
    if (io->values.direction == QA_SOURCE_SAVE_READ) {
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
    if (io->values.direction == QA_SOURCE_SAVE_WRITE && present && !qa_actors_save_reference(actors, *value, &saved, io->values.error))
        return false;
    if (!q1_save_bool(io, &present))
        return false;
    if (!present) {
        if (io->values.direction == QA_SOURCE_SAVE_READ)
            *value = (qa_actor_id){0};
        return true;
    }
    if (!q1_save_u64(io, &saved.generation) || !q1_save_u32(io, &saved.slot))
        return false;
    if (io->values.direction == QA_SOURCE_SAVE_READ) {
        const qa_actor_record *record = qa_actors_resolve_saved(actors, saved);
        if (record)
            *value = record->id;
        else if (!qa_actors_reference_saved(actors, saved, true, value, io->values.error))
            return false;
    }
    return true;
}
bool q1_save_ref(q1_save_io *io, q1_ref *value) {
    return qa_source_save_actor_reference(&io->values, value);
}

bool q1_save_literal(q1_save_io *io, const char **value) {
    uint32_t index = 0;
    if (io->values.direction == QA_SOURCE_SAVE_WRITE &&
        (!*value || !qa_strings_intern_cstr(io->dictionary, *value, &index, io->values.error)))
        return false;
    if (!q1_save_u32(io, &index))
        return false;
    if (io->values.direction == QA_SOURCE_SAVE_READ) {
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
