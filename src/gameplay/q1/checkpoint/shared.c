#include "internal.h"

bool q1_save_attack(q1_save_io *io, qa_attack *attack) {
    Q1_SAVE(io, u64, attack->sequence);
    Q1_SAVE(io, u64, attack->time_ns);
    Q1_SAVE(io, actor, attack->attacker);
    Q1_SAVE(io, actor, attack->inflictor);
    Q1_SAVE(io, actor, attack->projectile);
    Q1_SAVE(io, string, attack->weapon);
    Q1_SAVE(io, string, attack->weapon_provider);
    Q1_SAVE(io, string, attack->combat_provider);
    Q1_SAVE(io, string, attack->inventory_provider);
    Q1_SAVE(io, string, attack->movement_provider);
    Q1_SAVE(io, bool, attack->powerup_applied);
    Q1_SAVE(io, string, attack->powerup_owner);
    qa_damage_cause *cause = &attack->cause;
    Q1_SAVE_ENUM(io, cause->kind, QA_CAUSE_ENVIRONMENT);
    switch (cause->kind) {
    case QA_CAUSE_Q1:
        Q1_SAVE(io, string, cause->source.q1.death_type);
        Q1_SAVE_ENUM(io, cause->source.q1.armor, QA_Q1_ARMOR_HALF);
        break;
    case QA_CAUSE_Q2:
        Q1_SAVE(io, i32, cause->source.q2.means_of_death);
        Q1_SAVE(io, u32, cause->source.q2.flags);
        Q1_SAVE_ENUM(io, cause->source.q2.native, QA_Q2_CAUSE_RERELEASE);
        Q1_SAVE(io, i32, cause->source.q2.native_value);
        Q1_SAVE(io, u32, cause->source.q2.classic_product);
        Q1_SAVE(io, bool, cause->source.q2.friendly_fire);
        Q1_SAVE(io, bool, cause->source.q2.no_point_loss);
        break;
    case QA_CAUSE_Q3:
        Q1_SAVE(io, i32, cause->source.q3.means_of_death);
        Q1_SAVE(io, u32, cause->source.q3.flags);
        break;
    case QA_CAUSE_ENVIRONMENT:
        Q1_SAVE_ENUM(io, cause->source.hazard, QA_HAZARD_TRIGGER);
        break;
    }
    return true;
}

bool q1_save_physics(q1_save_io *io, qa_physics_properties *physics) {
    Q1_SAVE_ENUM(io, physics->family, QA_COLLISION_Q3);
    if (physics->family < QA_COLLISION_Q1)
        return q1_save_fail(io, "Invalid Q1 checkpoint collision family");
    Q1_SAVE_ENUM(io, physics->motion, QA_PHYSICS_STEP);
    Q1_SAVE_ENUM(io, physics->solid, QA_PHYSICS_CORPSE);
    Q1_SAVE(io, bool, physics->q2_rerelease);
    Q1_SAVE(io, u32, physics->flags);
    Q1_SAVE(io, u32, physics->clip_mask);
    Q1_SAVE(io, vector, physics->angular_velocity);
    Q1_SAVE(io, vector, physics->gravity_direction);
    Q1_SAVE(io, float, physics->gravity_scale);
    Q1_SAVE(io, float, physics->delta_yaw);
    Q1_SAVE(io, float, physics->ideal_yaw);
    Q1_SAVE(io, float, physics->yaw_speed);
    Q1_SAVE(io, i32, physics->water_level);
    Q1_SAVE(io, i32, physics->water_type);
    Q1_SAVE(io, actor, physics->enemy);
    Q1_SAVE(io, actor, physics->goal);
    Q1_SAVE(io, double, physics->q1_pusher.local_seconds);
    if (io->values.direction == QA_SOURCE_SAVE_READ)
        physics->q1_pusher.next_think_seconds = 0;
    return true;
}
