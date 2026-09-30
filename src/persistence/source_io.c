#include "qa/source_save.h"
#include "qa/binary.h"
#include "qa/persistence_fields.h"
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

bool qa_persistence_physics(qa_source_save_io *io, qa_physics_properties *value)
{
    if (!io || !value) return io_fail(io, QA_ERROR_ARGUMENT, "missing physics continuation field");
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t family = reading ? 0 : (uint32_t)value->family;
    uint32_t motion = reading ? 0 : (uint32_t)value->motion;
    uint32_t solid = reading ? 0 : (uint32_t)value->solid;
    if (!qa_source_save_u32(io, &family) || !qa_source_save_u32(io, &motion) ||
        !qa_source_save_u32(io, &solid)) return false;
    if (family < QA_COLLISION_Q1 || family > QA_COLLISION_Q3 ||
        motion > QA_PHYSICS_STEP || solid > QA_PHYSICS_CORPSE)
        return io_fail(io, QA_ERROR_FORMAT, "invalid physics continuation enum");
    if (reading) {
        value->family = (qa_collision_family)family; value->motion = (qa_physics_motion)motion;
        value->solid = (qa_physics_solid)solid;
    }
    return qa_source_save_bool(io, &value->q2_rerelease) &&
        qa_source_save_u32(io, &value->flags) && qa_source_save_u32(io, &value->clip_mask) &&
        qa_source_save_vec3(io, &value->angular_velocity) && qa_source_save_vec3(io, &value->gravity_direction) &&
        qa_source_save_f32(io, &value->gravity_scale) && qa_source_save_f32(io, &value->delta_yaw) &&
        qa_source_save_f32(io, &value->ideal_yaw) && qa_source_save_f32(io, &value->yaw_speed) &&
        qa_source_save_i32(io, &value->water_level) && qa_source_save_i32(io, &value->water_type) &&
        qa_source_save_actor(io, &value->enemy) && qa_source_save_actor(io, &value->goal) &&
        qa_source_save_i64(io, &value->local_time_ns) && qa_source_save_i64(io, &value->next_think_ns);
}

bool qa_persistence_collision(qa_source_save_io *io, qa_actor_collision *value)
{
    if (!io || !value) return io_fail(io, QA_ERROR_ARGUMENT, "missing collision continuation field");
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t family = reading ? 0 : (uint32_t)value->family;
    uint32_t shape = reading ? 0 : (uint32_t)value->shape;
    uint32_t role = reading ? 0 : (uint32_t)value->role;
    if (!qa_source_save_u32(io, &family) || !qa_source_save_u32(io, &shape) ||
        !qa_source_save_u32(io, &role)) return false;
    if (family < QA_COLLISION_Q1 || family > QA_COLLISION_Q3 ||
        shape > QA_SHAPE_CAPSULE || role > QA_COLLISION_BOTH)
        return io_fail(io, QA_ERROR_FORMAT, "invalid collision continuation enum");
    if (reading) {
        value->family = (qa_collision_family)family; value->shape = (qa_shape_kind)shape;
        value->role = (qa_collision_role)role;
    }
    return qa_source_save_bool(io, &value->inline_model) && qa_source_save_u32(io, &value->model) &&
        qa_source_save_i32(io, &value->contents) && qa_source_save_actor(io, &value->owner) &&
        qa_source_save_bool(io, &value->monster) && qa_source_save_bool(io, &value->dead_monster) &&
        qa_source_save_bool(io, &value->q1_corpse) && qa_source_save_bool(io, &value->has_q3_owner) &&
        qa_source_save_i32(io, &value->q3_entity_number) && qa_source_save_i32(io, &value->q3_owner_number);
}

static bool persistence_cause(qa_source_save_io *io, qa_damage_cause *value)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t kind = reading ? 0 : (uint32_t)value->kind;
    if (!qa_source_save_u32(io, &kind)) return false;
    if (kind > QA_CAUSE_ENVIRONMENT) return io_fail(io, QA_ERROR_FORMAT, "invalid damage cause kind");
    if (reading) { *value = (qa_damage_cause){0}; value->kind = (qa_cause_kind)kind; }
    switch (value->kind) {
    case QA_CAUSE_Q1: {
        uint32_t armor = reading ? 0 : (uint32_t)value->source.q1.armor;
        if (!qa_source_save_u32(io, &value->source.q1.death_type) || !qa_source_save_u32(io, &armor)) return false;
        if (armor > QA_Q1_ARMOR_HALF) return io_fail(io, QA_ERROR_FORMAT, "invalid Q1 damage armor policy");
        if (reading) value->source.q1.armor = (qa_q1_armor_effect)armor;
        return true;
    }
    case QA_CAUSE_Q2: {
        uint32_t edition = reading ? 0 : (uint32_t)value->source.q2.native;
        if (!qa_source_save_i32(io, &value->source.q2.means_of_death) ||
            !qa_source_save_u32(io, &value->source.q2.flags) || !qa_source_save_u32(io, &edition)) return false;
        if (edition > QA_Q2_CAUSE_RERELEASE) return io_fail(io, QA_ERROR_FORMAT, "invalid Q2 damage edition");
        if (reading) value->source.q2.native = (qa_q2_native_edition)edition;
        return qa_source_save_i32(io, &value->source.q2.native_value) &&
            qa_source_save_u32(io, &value->source.q2.classic_product) &&
            qa_source_save_bool(io, &value->source.q2.friendly_fire) &&
            qa_source_save_bool(io, &value->source.q2.no_point_loss);
    }
    case QA_CAUSE_Q3:
        return qa_source_save_i32(io, &value->source.q3.means_of_death) &&
            qa_source_save_u32(io, &value->source.q3.flags);
    case QA_CAUSE_ENVIRONMENT: {
        uint32_t hazard = reading ? 0 : (uint32_t)value->source.hazard;
        if (!qa_source_save_u32(io, &hazard)) return false;
        if (hazard > QA_HAZARD_TRIGGER) return io_fail(io, QA_ERROR_FORMAT, "invalid environment damage cause");
        if (reading) value->source.hazard = (qa_hazard)hazard;
        return true;
    }
    }
    return io_fail(io, QA_ERROR_FORMAT, "invalid damage cause");
}

bool qa_persistence_attack(qa_source_save_io *io, qa_attack *value)
{
    if (!io || !value) return io_fail(io, QA_ERROR_ARGUMENT, "missing attack continuation field");
    return qa_source_save_u64(io, &value->sequence) && qa_source_save_u64(io, &value->time_ns) &&
        qa_source_save_actor(io, &value->attacker) && qa_source_save_actor(io, &value->inflictor) &&
        qa_source_save_actor(io, &value->projectile) && qa_source_save_u32(io, &value->weapon) &&
        qa_source_save_u32(io, &value->weapon_provider) && qa_source_save_u32(io, &value->combat_provider) &&
        qa_source_save_u32(io, &value->inventory_provider) && qa_source_save_u32(io, &value->movement_provider) &&
        qa_source_save_bool(io, &value->powerup_applied) && qa_source_save_u32(io, &value->powerup_owner) &&
        persistence_cause(io, &value->cause);
}
