#include "internal.h"
#include "qa/binary.h"
#include "qa/persistence_fields.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool string_value(qa_source_save_io *io, bool *present, qa_bytes *bytes)
{
    if (!qa_source_save_bool(io, present) || !*present) return io && !io->failed;
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE ? bytes->size : 0;
    if (!qa_source_save_count(io, &length, SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE)
        return qa_source_save_bytes(io, (void *)bytes->data, length);
    if (io->offset > io->input.size || length > io->input.size - io->offset)
        return persistence_io_fail(io, QA_ERROR_FORMAT, "truncated source string value");
    *bytes = (qa_bytes){io->input.data + io->offset, length}; io->offset += length;
    return true;
}
bool qa_source_save_string(qa_source_save_io *io, qa_string_id *value)
{
    if (!io || !value || !io->session) return persistence_io_fail(io, QA_ERROR_ARGUMENT, "missing source string owner");
    bool present = io->direction == QA_SOURCE_SAVE_WRITE && *value != QA_STRING_NONE;
    qa_bytes bytes = {0};
    if (present) {
        bytes = qa_strings_text(qa_session_strings(io->session), *value);
        if (!bytes.data) return persistence_io_fail(io, QA_ERROR_FORMAT, "source string ID has no value in its owner");
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
    if (!io || !value || !io->session) return persistence_io_fail(io, QA_ERROR_ARGUMENT, "missing source text owner");
    bool present = io->direction == QA_SOURCE_SAVE_WRITE && *value != NULL;
    qa_bytes bytes = present ? (qa_bytes){(const uint8_t *)*value, strlen(*value)} : (qa_bytes){0};
    if (!string_value(io, &present, &bytes)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (!present) *value = NULL;
        else {
            if (memchr(bytes.data, 0, bytes.size)) return persistence_io_fail(io, QA_ERROR_FORMAT, "source text contains embedded NUL");
            qa_string_id id;
            if (!qa_strings_intern(qa_session_strings(io->session), bytes, &id, io->error)) { io->failed = true; return false; }
            *value = qa_strings_cstr(qa_session_strings(io->session), id);
        }
    }
    return true;
}
bool qa_source_save_text_assert(qa_source_save_io *io, const char *expected)
{
    if (!io) return persistence_io_fail(io, QA_ERROR_ARGUMENT, "missing source text assertion");
    size_t length = expected ? strlen(expected) : 0;
    bool present = io->direction == QA_SOURCE_SAVE_WRITE && expected != NULL;
    qa_bytes bytes = present ? (qa_bytes){(const uint8_t *)expected, length} : (qa_bytes){0};
    if (!string_value(io, &present, &bytes)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return true;
    if (present != (expected != NULL) ||
        (present && (bytes.size != length || (length && memcmp(bytes.data, expected, length)))))
        return persistence_io_fail(io, QA_ERROR_FORMAT, "source text differs from its actual owner");
    return true;
}
bool qa_source_save_owned_text(qa_source_save_io *io, char **value)
{
    if (!io || !value) return persistence_io_fail(io, QA_ERROR_ARGUMENT, "missing owned source text");
    bool present = io->direction == QA_SOURCE_SAVE_WRITE && *value != NULL;
    qa_bytes bytes = present ? (qa_bytes){(const uint8_t *)*value, strlen(*value)} : (qa_bytes){0};
    if (!string_value(io, &present, &bytes)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        char *text = NULL;
        if (present) {
            if (memchr(bytes.data, 0, bytes.size)) return persistence_io_fail(io, QA_ERROR_FORMAT, "source text contains embedded NUL");
            text = malloc(bytes.size + 1);
            if (!text) return persistence_io_fail(io, QA_ERROR_MEMORY, "retaining owned source text");
            if (bytes.size) memcpy(text, bytes.data, bytes.size);
            text[bytes.size] = 0;
        }
        free(*value);
        *value = text;
    }
    return true;
}
bool qa_source_save_actor(qa_source_save_io *io, qa_actor_id *value)
{
    if (!io || !value || !io->session) return persistence_io_fail(io, QA_ERROR_ARGUMENT, "missing source actor owner");
    bool present = io->direction == QA_SOURCE_SAVE_WRITE && value->registry != 0;
    qa_saved_actor_id saved = {0};
    if (present && !qa_actors_save_reference(qa_session_actors(io->session), *value, &saved, io->error)) { io->failed = true; return false; }
    if (!qa_source_save_bool(io, &present) || !qa_source_save_u64(io, &saved.generation) ||
        !qa_source_save_u32(io, &saved.slot)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (!present) {
            if (saved.generation || saved.slot) return persistence_io_fail(io, QA_ERROR_FORMAT, "absent source actor contains provenance");
            *value = (qa_actor_id){0};
        } else if (!qa_actors_reference_saved(qa_session_actors(io->session), saved, true, value, io->error)) { io->failed = true; return false; }
    }
    return true;
}

bool qa_persistence_physics(qa_source_save_io *io, qa_physics_properties *value)
{
    if (!io || !value) return persistence_io_fail(io, QA_ERROR_ARGUMENT, "missing physics continuation field");
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t family = reading ? 0 : (uint32_t)value->family;
    uint32_t motion = reading ? 0 : (uint32_t)value->motion;
    uint32_t solid = reading ? 0 : (uint32_t)value->solid;
    if (!qa_source_save_u32(io, &family) || !qa_source_save_u32(io, &motion) ||
        !qa_source_save_u32(io, &solid)) return false;
    if (family < QA_COLLISION_Q1 || family > QA_COLLISION_Q3 ||
        motion > QA_PHYSICS_STEP || solid > QA_PHYSICS_CORPSE)
        return persistence_io_fail(io, QA_ERROR_FORMAT, "invalid physics continuation enum");
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
        qa_source_save_f64(io, &value->q1_pusher.local_seconds) &&
        qa_source_save_f64(io, &value->q1_pusher.next_think_seconds);
}

bool qa_persistence_collision(qa_source_save_io *io, qa_actor_collision *value)
{
    if (!io || !value) return persistence_io_fail(io, QA_ERROR_ARGUMENT, "missing collision continuation field");
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint32_t family = reading ? 0 : (uint32_t)value->family;
    uint32_t shape = reading ? 0 : (uint32_t)value->shape;
    uint32_t role = reading ? 0 : (uint32_t)value->role;
    if (!qa_source_save_u32(io, &family) || !qa_source_save_u32(io, &shape) ||
        !qa_source_save_u32(io, &role)) return false;
    if (family < QA_COLLISION_Q1 || family > QA_COLLISION_Q3 ||
        shape > QA_SHAPE_CAPSULE || role > QA_COLLISION_BOTH)
        return persistence_io_fail(io, QA_ERROR_FORMAT, "invalid collision continuation enum");
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
    if (kind > QA_CAUSE_ENVIRONMENT) return persistence_io_fail(io, QA_ERROR_FORMAT, "invalid damage cause kind");
    if (reading) { *value = (qa_damage_cause){0}; value->kind = (qa_cause_kind)kind; }
    switch (value->kind) {
    case QA_CAUSE_Q1: {
        uint32_t armor = reading ? 0 : (uint32_t)value->source.q1.armor;
        if (!qa_source_save_u32(io, &value->source.q1.death_type) || !qa_source_save_u32(io, &armor)) return false;
        if (armor > QA_Q1_ARMOR_HALF) return persistence_io_fail(io, QA_ERROR_FORMAT, "invalid Q1 damage armor policy");
        if (reading) value->source.q1.armor = (qa_q1_armor_effect)armor;
        return true;
    }
    case QA_CAUSE_Q2: {
        uint32_t edition = reading ? 0 : (uint32_t)value->source.q2.native;
        if (!qa_source_save_i32(io, &value->source.q2.means_of_death) ||
            !qa_source_save_u32(io, &value->source.q2.flags) || !qa_source_save_u32(io, &edition)) return false;
        if (edition > QA_Q2_CAUSE_RERELEASE) return persistence_io_fail(io, QA_ERROR_FORMAT, "invalid Q2 damage edition");
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
        if (hazard > QA_HAZARD_TRIGGER) return persistence_io_fail(io, QA_ERROR_FORMAT, "invalid environment damage cause");
        if (reading) value->source.hazard = (qa_hazard)hazard;
        return true;
    }
    }
    return persistence_io_fail(io, QA_ERROR_FORMAT, "invalid damage cause");
}

bool qa_persistence_attack(qa_source_save_io *io, qa_attack *value)
{
    if (!io || !value) return persistence_io_fail(io, QA_ERROR_ARGUMENT, "missing attack continuation field");
    return qa_source_save_u64(io, &value->sequence) && qa_source_save_u64(io, &value->time_ns) &&
        qa_source_save_actor(io, &value->attacker) && qa_source_save_actor(io, &value->inflictor) &&
        qa_source_save_actor(io, &value->projectile) && qa_source_save_u32(io, &value->weapon) &&
        qa_source_save_u32(io, &value->weapon_provider) && qa_source_save_u32(io, &value->combat_provider) &&
        qa_source_save_u32(io, &value->inventory_provider) && qa_source_save_u32(io, &value->movement_provider) &&
        qa_source_save_bool(io, &value->powerup_applied) && qa_source_save_u32(io, &value->powerup_owner) &&
        persistence_cause(io, &value->cause);
}
