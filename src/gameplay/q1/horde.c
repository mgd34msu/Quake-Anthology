#include "internal.h"

bool qa_q1_horde_spawn(qa_q1_game *g, const char *classname, qa_vec3 origin, qa_vec3 angles,
                       qa_actor_id manager, qa_actor_id enemy, qa_actor_id *out, qa_error *error) {
    if (!g || !classname || !out || !qa_vec_finite(origin) || !qa_vec_finite(angles)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 Horde spawn");
        return false;
    }
    const q1_species *species = q1_species_find(classname);
    if (!species || species->species > QA_Q1_ZOMBIE || species->species == QA_Q1_TARBABY ||
        species->species == QA_Q1_FISH) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "unsupported Horde monster %s", classname);
        return false;
    }
    qa_q1_spawn spawn = {.classname = classname, .origin = origin, .angles = angles};
    qa_actor_id actor;
    if (!qa_q1_game_spawn(g, &spawn, &actor, error))
        return false;
    q1_actor *entity = q1_entity(g, actor);
    if (!entity) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Horde native spawn was inhibited by source mode");
        return false;
    }
    qa_scheduler_cancel(qa_session_scheduler(g->services.session), actor);
    entity->think = Q1_THINK_NONE;
    entity->next_think = 0;
    entity->state.monster.horde = true;
    entity->state.monster.addon.waiting = false;
    entity->state.monster.addon.started = true;
    entity->state.monster.enemy = enemy;
    entity->owner = manager;
    entity->aimed_damage = true;
    entity->physics.solid = QA_PHYSICS_BOX;
    entity->physics.motion = QA_PHYSICS_STEP;
    entity->physics.flags |= QA_PHYSICS_MONSTER;
    entity->physics.yaw_speed = 20;
    entity->physics.enemy = enemy;
    entity->physics.goal = enemy;
    if (species->species == QA_Q1_WIZARD)
        entity->physics.flags |= QA_PHYSICS_FLYING;
    float offset = species->species == QA_Q1_DEMON ? 48
                   : species->species == QA_Q1_OGRE || species->species == QA_Q1_SHAMBLER ||
                           species->species == QA_Q1_SHALRATH || species->species == QA_Q1_WIZARD ||
                           species->species == QA_Q1_ZOMBIE
                       ? 32
                       : 24;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    if (!entity->state.monster.addon.enabled)
        body.bounds = species->bounds;
    if (!q1_model(g, entity, species->model, error))
        return false;
    body.origin = qa_vec_add(origin, qa_v3(0, 0, offset + 1));
    if (species->species != QA_Q1_WIZARD) {
        qa_trace_query query = {.start = body.origin,
                                .end = qa_vec_sub(body.origin, qa_v3(0, 0, 256)),
                                .shape = {.kind = QA_SHAPE_BOX, .bounds = body.bounds},
                                .pass_actor = actor,
                                .policy = qa_collision_default_policy(QA_COLLISION_Q1)};
        qa_trace_result trace;
        if (!qa_world_trace(g->services.world, &query, &trace, error))
            return false;
        if (trace.fraction < 1 && !trace.all_solid) {
            body.origin = trace.end;
            body.ground = trace.actor;
            entity->physics.flags |= QA_PHYSICS_ONGROUND;
        }
    }
    qa_combat_state combat;
    bool moved;
    if (!qa_combat_read_traits(g->services.combat, actor, &combat, error))
        return false;
    combat.can_take_damage = true;
    if (!qa_builtin_resource(&g->services, "q1:monsters", &combat.team, error))
        return false;
    if (!qa_combat_set_traits(g->services.combat, actor, &combat, error) ||
        !qa_world_body_write(g->services.world, actor, &body, error) ||
        !qa_physics_walk_move(g->services.physics, actor, 0, 0, (float)g->elapsed, true, true,
                              &moved, error))
        return false;
    if (species->species == QA_Q1_ZOMBIE)
        --g->total_monsters;
    if (!q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FOUND, error))
        return false;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    qa_actor_id death_id;
    if (!q1_spawn_teledeath(g, body.origin, actor, 0.01, false, &death_id, error))
        return false;
    q1_actor *death = q1_entity(g, death_id);
    qa_body_state death_body;
    if (!death || !qa_world_body_read(g->services.world, death_id, &death_body, error))
        return false;
    qa_bounds overlap = {qa_vec_add(death_body.origin, death_body.bounds.mins),
                         qa_vec_add(death_body.origin, death_body.bounds.maxs)};
    q1_actor_snapshot *snapshot;
    if (!q1_snapshot_actors(g, &snapshot, error))
        return false;
    bool ok = true;
    for (size_t i = 0; i < snapshot->count; ++i) {
        qa_actor_id victim = snapshot->actors[i];
        qa_body_state target;
        if (!qa_world_body_read(g->services.world, victim, &target, NULL))
            continue;
        qa_bounds bounds = {qa_vec_add(target.origin, target.bounds.mins),
                            qa_vec_add(target.origin, target.bounds.maxs)};
        if (qa_bounds_overlap(overlap, bounds) && !q1_teledeath_touch(g, death, victim, error)) {
            ok = false;
            break;
        }
        if (!q1_alive(g, death_id))
            break;
    }
    snapshot->borrowed = false;
    if (ok)
        *out = actor;
    return ok;
}
