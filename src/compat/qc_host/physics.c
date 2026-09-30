#include "internal.h"

static bool scalar(qa_qc_game *game, int32_t reference, const char *name, float fallback,
                   bool optional, float *out, qa_error *error) {
    const qa_qc_definition *field = qa_qc_program_find_field(game->program, name);
    if (!field && optional) { *out = fallback; return true; }
    if (!field || field->type != QA_QC_FLOAT)
        return qc_game_fail(error, QA_ERROR_FORMAT, "QC physics scalar field differs");
    if (!qa_qc_entity_float(game->vm, reference, field->offset, out, error)) return false;
    return isfinite(*out) || qc_game_fail(error, QA_ERROR_FORMAT, "Nonfinite QC physics scalar");
}
static bool vector(qa_qc_game *game, int32_t reference, const char *name, qa_vec3 *out, qa_error *error) {
    const qa_qc_definition *field = qa_qc_program_find_field(game->program, name);
    if (!field || field->type != QA_QC_VECTOR)
        return qc_game_fail(error, QA_ERROR_FORMAT, "QC physics vector field differs");
    if (!qa_qc_entity_vector(game->vm, reference, field->offset, out, error)) return false;
    return qa_vec_finite(*out) || qc_game_fail(error, QA_ERROR_FORMAT, "Nonfinite QC physics vector");
}
static bool entity(qa_qc_game *game, int32_t reference, const char *name, qa_actor_id *out, qa_error *error) {
    const qa_qc_definition *field = qa_qc_program_find_field(game->program, name);
    int32_t target;
    if (!field || field->type != QA_QC_ENTITY)
        return qc_game_fail(error, QA_ERROR_FORMAT, "QC physics entity field differs");
    if (!qa_qc_entity_int(game->vm, reference, field->offset, &target, error)) return false;
    if (!target) { *out = (qa_actor_id){0}; return true; }
    return qa_qc_reference_actor(game->vm, target, out, error);
}
static uint32_t flags_word(float value) {
    double bits = fmod(trunc((double)value), 4294967296.0);
    if (bits < 0) bits += 4294967296.0;
    return (uint32_t)bits;
}
static int64_t deadline(float seconds) {
    double ns = (double)seconds * 1000000000.0;
    if (!(ns > 0)) return 0;
    return ns >= (double)INT64_MAX ? INT64_MAX : (int64_t)ns;
}
static bool still_actor(qa_qc_game *game, int32_t reference, qa_actor_id actor, qa_error *error) {
    qa_actor_id current;
    return qa_qc_reference_actor(game->vm, reference, &current, error) &&
        (qa_actor_id_equal(current, actor) || qc_game_fail(error, QA_ERROR_NOT_FOUND, "QC physics actor changed"));
}
bool qa_qc_game_read_physics(qa_qc_game *game, qa_actor_id actor, qa_physics_properties *out, qa_error *error) {
    if (!game || !out) return qc_game_fail(error, QA_ERROR_ARGUMENT, "Missing QC physics output");
    int32_t reference;
    if (!qa_qc_actor_reference(game->vm, actor, false, &reference, error)) return false;
    float motion, solid, flags, health, water_level, water_type, local, next;
    qa_physics_properties value = {.family = QA_COLLISION_Q1, .clip_mask = 3,
        .gravity_direction = {0, 0, -1}, .gravity_scale = 1};
    if (!scalar(game, reference, "movetype", 0, false, &motion, error) ||
        !scalar(game, reference, "solid", 0, false, &solid, error) ||
        !scalar(game, reference, "flags", 0, false, &flags, error) ||
        !scalar(game, reference, "health", 0, false, &health, error) ||
        !scalar(game, reference, "waterlevel", 0, false, &water_level, error) ||
        !scalar(game, reference, "watertype", 0, false, &water_type, error) ||
        !scalar(game, reference, "ltime", 0, false, &local, error) ||
        !scalar(game, reference, "nextthink", 0, false, &next, error) ||
        !scalar(game, reference, "gravity", 1, true, &value.gravity_scale, error) ||
        !scalar(game, reference, "ideal_yaw", 0, false, &value.ideal_yaw, error) ||
        !scalar(game, reference, "yaw_speed", 0, false, &value.yaw_speed, error) ||
        !vector(game, reference, "avelocity", &value.angular_velocity, error) ||
        !entity(game, reference, "enemy", &value.enemy, error) ||
        !entity(game, reference, "goalentity", &value.goal, error)) return false;
    if (truncf(motion) != motion || motion < 0 || motion > 11)
        return qc_game_fail(error, QA_ERROR_UNSUPPORTED, "QC movetype is unsupported");
    switch ((int32_t)motion) {
    case 0: value.motion = QA_PHYSICS_STATIONARY; break;
    case 8: value.motion = QA_PHYSICS_NOCLIP; break;
    case 3: case 4: value.motion = QA_PHYSICS_STEP; break;
    case 5: value.motion = QA_PHYSICS_FLY; break;
    case 6: value.motion = QA_PHYSICS_TOSS; break;
    case 7: value.motion = QA_PHYSICS_PUSH; break;
    case 9: value.motion = QA_PHYSICS_FLY_MISSILE; break;
    case 10: value.motion = QA_PHYSICS_BOUNCE; break;
    case 11:
        if (game->options.vm.profile != QA_QC_RERELEASE)
            return qc_game_fail(error, QA_ERROR_UNSUPPORTED, "QC bounce missile requires rerelease profile");
        value.motion = QA_PHYSICS_BOUNCE; break;
    default: return qc_game_fail(error, QA_ERROR_UNSUPPORTED, "QC movetype is unsupported");
    }
    value.solid = solid == 0 || (solid == 5 && game->options.vm.profile != QA_QC_RERELEASE) ? QA_PHYSICS_NOT_SOLID
        : solid == 1 ? QA_PHYSICS_TRIGGER : solid == 4 ? QA_PHYSICS_BRUSH
        : solid == 5 ? QA_PHYSICS_CORPSE : QA_PHYSICS_BOX;
    uint32_t source = flags_word(flags);
    if (source & 1u) value.flags |= QA_PHYSICS_FLYING;
    if (source & 2u) value.flags |= QA_PHYSICS_SWIMMING;
    if (source & 1024u) value.flags |= QA_PHYSICS_PARTIAL_GROUND;
    if (source & 32u) value.flags |= QA_PHYSICS_MONSTER;
    if (source & 512u) value.flags |= QA_PHYSICS_ONGROUND;
    qa_qc_entity_layout layout = game->options.vm.entity_layout;
    if (!layout.stride_bytes) layout = qa_qc_default_entity_layout(game->program, game->options.vm.profile);
    uint32_t slot = (uint32_t)reference / layout.stride_bytes;
    if (slot && slot <= game->options.max_clients) value.flags |= QA_PHYSICS_PLAYER;
    if (health <= 0) value.flags |= QA_PHYSICS_DEAD;
    if ((double)water_level < INT32_MIN || (double)water_level > INT32_MAX ||
        (double)water_type < INT32_MIN || (double)water_type > INT32_MAX)
        return qc_game_fail(error, QA_ERROR_FORMAT, "QC water properties exceed source bounds");
    value.water_level = (int32_t)water_level; value.water_type = (int32_t)water_type;
    if (!value.gravity_scale) value.gravity_scale = 1;
    value.local_time_ns = deadline(local); value.next_think_ns = deadline(next);
    if (!still_actor(game, reference, actor, error)) return false;
    *out = value; return true;
}
static bool store_scalar(qa_qc_game *game, int32_t reference, qa_actor_id actor,
                         const char *name, float value, qa_error *error) {
    const qa_qc_definition *field = qa_qc_program_find_field(game->program, name);
    if (!field || field->type != QA_QC_FLOAT)
        return qc_game_fail(error, QA_ERROR_FORMAT, "QC physics store field differs");
    return still_actor(game, reference, actor, error) &&
           qa_qc_set_entity_float(game->vm, reference, field->offset, value, error) &&
           still_actor(game, reference, actor, error);
}
bool qa_qc_game_write_physics(qa_qc_game *game, qa_actor_id actor,
                              const qa_physics_properties *value, qa_error *error) {
    if (!game || !value || value->family != QA_COLLISION_Q1 || !isfinite(value->ideal_yaw) ||
        !isfinite(value->yaw_speed) || !qa_vec_finite(value->angular_velocity))
        return qc_game_fail(error, QA_ERROR_ARGUMENT, "Invalid QC physics store");
    int32_t reference;
    if (!qa_qc_actor_reference(game->vm, actor, false, &reference, error)) return false;
    float flags;
    if (!scalar(game, reference, "flags", 0, false, &flags, error)) return false;
    uint32_t bits = flags_word(flags) & ~(1u | 2u | 1024u | 512u);
    if (value->flags & QA_PHYSICS_FLYING) bits |= 1u;
    if (value->flags & QA_PHYSICS_SWIMMING) bits |= 2u;
    if (value->flags & QA_PHYSICS_PARTIAL_GROUND) bits |= 1024u;
    if (value->flags & QA_PHYSICS_ONGROUND) bits |= 512u;
    const qa_qc_definition *angular = qa_qc_program_find_field(game->program, "avelocity");
    if (!angular || angular->type != QA_QC_VECTOR)
        return qc_game_fail(error, QA_ERROR_FORMAT, "Missing QC angular velocity field");
    if (!store_scalar(game, reference, actor, "flags", (float)bits, error) ||
        !store_scalar(game, reference, actor, "waterlevel", (float)value->water_level, error) ||
        !store_scalar(game, reference, actor, "watertype", (float)value->water_type, error) ||
        !store_scalar(game, reference, actor, "ideal_yaw", value->ideal_yaw, error) ||
        !store_scalar(game, reference, actor, "yaw_speed", value->yaw_speed, error) ||
        !store_scalar(game, reference, actor, "ltime", (float)((double)value->local_time_ns / 1000000000.0), error) ||
        !store_scalar(game, reference, actor, "nextthink", (float)((double)value->next_think_ns / 1000000000.0), error) ||
        !qa_qc_set_entity_vector(game->vm, reference, angular->offset, value->angular_velocity, error)) return false;
    return still_actor(game, reference, actor, error);
}
