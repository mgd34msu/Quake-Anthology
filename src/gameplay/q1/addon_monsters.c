#include "internal.h"

unsigned q1_mg3_range(const q1_actor *entity, float distance) {
    if (!(entity->spawnflags & 8192))
        return distance < 120 ? 0 : distance < 500 ? 1 : distance < 1000 ? 2 : 3;
    if (!entity->state.monster.enemy.registry)
        return distance < 120 ? 0 : distance < 300 ? 1 : distance < 340 ? 2 : 3;
    return distance < 96 ? 0 : distance < 400 ? 1 : distance < 800 ? 2 : 3;
}
bool q1_addon_target(qa_q1_game *g, q1_actor *entity, qa_actor_id *out, qa_error *error) {
    *out = (qa_actor_id){0};
    if (!(entity->spawnflags & 32) || !entity->target)
        return true;
    if (!g->host.find_targets) {
        qa_error_set(error, QA_ERROR_ARGUMENT, entity->id.slot,
                     "Q1 authored monster target selection requires the shared target router");
        return false;
    }
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    size_t count = 0;
    bool result = g->host.find_targets(g->host.context, entity->target, snapshot->snapshot.ids,
                                       snapshot->snapshot.capacity, &count, error);
    if (result && count > snapshot->snapshot.capacity) {
        qa_error_set(error, QA_ERROR_FORMAT, count,
                     "Q1 target router exceeded reserved actor capacity");
        result = false;
    }
    if (result) {
        size_t eligible = 0;
        for (size_t i = 0; i < count; ++i)
            if (q1_damageable(g, snapshot->snapshot.ids[i]))
                snapshot->snapshot.ids[eligible++] = snapshot->snapshot.ids[i];
        if (eligible) {
            size_t index = (size_t)floorf(q1_random(g) * (float)eligible);
            if (index == eligible)
                index = eligible - 1;
            *out = snapshot->snapshot.ids[index];
        }
    }
    qa_builtin_snapshot_release(snapshot);
    return result;
}
bool q1_addon_contents(qa_q1_game *g, q1_actor *entity, bool *stop, qa_error *error) {
    *stop = false;
    q1_monster *monster = &entity->state.monster;
    if (g->options.program != QA_Q1_MG3 || !monster->addon.enabled ||
        (entity->spawnflags & 16384) || entity->physics.water_level == 0 ||
        q1_health(g, entity->id) <= 0 || g->time < monster->addon.damage_at)
        return true;
    bool lava = entity->physics.water_type == -5;
    if (!lava && entity->physics.water_type != -4)
        return true;
    bool zombie = monster->species->species == QA_Q1_ZOMBIE;
    monster->addon.damage_at = g->time + (lava ? 0.2 : 1);
    qa_actor_id world = g->services.physics->world_actor;
    if (!q1_damage(g, entity->id, world, world,
                   lava ? zombie ? 120 : 30.0f * (float)entity->physics.water_level
                        : 4.0f * (float)entity->physics.water_level,
                   QA_Q1_WEAPON_COUNT, error))
        return false;
    *stop = !q1_alive(g, entity->id) || (lava && zombie) || q1_health(g, entity->id) <= 0;
    return true;
}
bool q1_addon_move(qa_q1_game *g, q1_actor *entity, float distance, bool seen, qa_error *error) {
    q1_monster *monster = &entity->state.monster;
    if (g->options.program == QA_Q1_MG3 && monster->addon.allow_path && g->host.horde &&
        g->host.horde(g->host.context)) {
        qa_body_state self, other;
        if (!qa_world_body_read(g->services.world, monster->enemy, &other, NULL))
            return true;
        if (!qa_world_body_read(g->services.world, entity->id, &self, error))
            return false;
        unsigned range = q1_mg3_range(entity, qa_vec_length(qa_vec_sub(other.origin, self.origin)));
        if (!seen || (monster->addon.combat_style == 2 && range > 1) ||
            (monster->addon.combat_style == 3 && range > 2)) {
            if (!g->host.monster_path) {
                qa_error_set(error, QA_ERROR_ARGUMENT, entity->id.slot,
                             "MG3 Horde monster needs selected navigation");
                return false;
            }
            qa_q1_path_result result;
            if (!g->host.monster_path(g->host.context, entity->id, other.origin, distance, &result,
                                      error))
                return false;
            if (result == QA_Q1_PATH_IN_PROGRESS || !q1_alive(g, entity->id))
                return true;
        }
    }
    return qa_physics_q1_move_to_goal(g->services.physics, entity->id, monster->enemy, distance,
                                      false, error);
}
