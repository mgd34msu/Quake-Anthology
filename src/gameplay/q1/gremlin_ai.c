#include "internal.h"

static bool creature(qa_q1_game *g, qa_actor_id actor, qa_q1_target *traits) {
    if (!q1_target(g, actor, traits) || traits->notarget)
        return false;
    if (traits->player)
        return true;
    q1_actor *native = q1_entity(g, actor);
    if (native)
        return (native->physics.flags & QA_PHYSICS_MONSTER) != 0;
    qa_builtin_actor_traits foreign;
    return g->services.actor_traits &&
           g->services.actor_traits(g->services.context, actor, &foreign) && foreign.monster;
}
bool q1_gremlin_find_victim(qa_q1_game *g, q1_actor *entity, qa_actor_id *out, qa_error *error) {
    entity->state.monster.search_until = g->time + 1;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_radius_snapshot(g, body.origin, 1000, &snapshot, error))
        return false;
    qa_actor_id selected = {0};
    float best = 1000;
    bool ok = true;
    for (size_t i = snapshot->snapshot.count; i > 0; --i) {
        qa_actor_id actor = snapshot->snapshot.ids[i - 1];
        qa_q1_target traits;
        qa_body_state target;
        if (qa_actor_id_equal(actor, entity->id) || !creature(g, actor, &traits) ||
            q1_health(g, actor) <= 0 ||
            !qa_world_body_read(g->services.world, actor, &target, NULL))
            continue;
        bool visible;
        if (!q1_monster_visible(g, entity, actor, &visible, error)) {
            ok = false;
            break;
        }
        if (!visible)
            continue;
        float distance = qa_vec_length(qa_vec_sub(target.origin, body.origin));
        if (q1_ref_equal(q1_ref_from(g, actor), entity->state.monster.source.gremlin.last_victim))
            distance *= 2;
        if (traits.player)
            distance /= 1.5f;
        if (q1_classnamed(g, actor, g->runtime_names[Q1_NAME_MONSTER_GREMLIN]))
            distance *= 1.5f;
        if (distance < best) {
            best = distance;
            selected = actor;
        }
    }
    qa_builtin_snapshot_release(snapshot);
    if (!ok)
        return false;
    entity->state.monster.source.gremlin.last_victim = q1_ref_from(g, selected);
    *out = selected;
    return true;
}
static bool find_target(qa_q1_game *g, q1_actor *entity, bool *out, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    *out = false;
    if (!m->source.gremlin.stolen && g->time > entity->wait) {
        entity->wait = (float)(g->time + 1);
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error))
            return false;
        qa_builtin_snapshot_frame *snapshot;
        if (!q1_snapshot_actors(g, &snapshot, error))
            return false;
        qa_actor_id gorge = {0};
        float best = 2000;
        bool ok = true;
        for (size_t i = 0; i < snapshot->snapshot.count; ++i) {
            qa_actor_id actor = snapshot->snapshot.ids[i];
            qa_body_state target;
            qa_q1_target traits;
            if (q1_health(g, actor) >= 1 || !creature(g, actor, &traits) ||
                !qa_world_body_read(g->services.world, actor, &target, NULL))
                continue;
            q1_actor *native = q1_entity(g, actor);
            if (native && native->consumed_corpse)
                continue;
            bool visible;
            if (!q1_monster_visible(g, entity, actor, &visible, error)) {
                ok = false;
                break;
            }
            qa_vec3 delta = qa_vec_sub(target.origin, body.origin);
            if (!visible || fabsf(delta.z) >= 80)
                continue;
            delta.z += traits.view_height - 25;
            float distance = qa_vec_length(delta);
            if (distance < best) {
                best = distance;
                gorge = actor;
            }
        }
        qa_builtin_snapshot_release(snapshot);
        if (!ok)
            return false;
        if (gorge.registry && best < 700 * q1_random(g)) {
            m->old_enemy = m->enemy;
            m->source.gremlin.gorging = true;
            m->enemy = q1_ref_from(g, gorge);
            m->search_until = g->time + 4;
            *out = true;
            return q1_monster_found(g, entity, gorge, error);
        }
    } else if (m->source.gremlin.stolen) {
        qa_actor_id victim;
        if (!q1_gremlin_find_victim(g, entity, &victim, error))
            return false;
        if (victim.registry) {
            if (!q1_monster_found(g, entity, victim, error))
                return false;
            m->attack_finished = g->time;
            m->search_until = g->time + 2;
            *out = true;
            return true;
        }
    }
    if (!q1_monster_find_target(g, entity, out, error))
        return false;
    m->search_until = g->time + 2;
    return true;
}
bool q1_gremlin_walk(qa_q1_game *g, q1_actor *entity, float distance, qa_error *error) {
    bool found;
    if (!find_target(g, entity, &found, error))
        return false;
    if (found || !q1_alive(g, entity->id))
        return true;
    qa_actor_id goal =
        q1_ref_actor(g, q1_alive(g, q1_ref_actor(g, entity->physics.goal)) ? entity->physics.goal : q1_ref_from(g, q1_monster_route(g, entity)));
    return !goal.registry || qa_physics_q1_move_to_goal(g->services.physics, entity->id, goal,
                                                        distance, false, error);
}
static bool flee_goal(qa_q1_game *g, qa_actor_id source, q1_actor **out, qa_error *error) {
    *out = NULL;
    if (!q1_entity(g, source))
        return true;
    q1_actor *goal;
    if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_GREMLIN_GOAL], Q1_ENTITY, (qa_actor_id){0}, &goal, error))
        return false;
    qa_actor_id child = goal->id;
    if (!q1_entity(g, source) || !q1_entity(g, child))
        goto cancelled;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, child, &body, error))
        goto failed;
    if (!q1_entity(g, source) || !q1_entity(g, child))
        goto cancelled;
    body.bounds = (qa_bounds){{-1, -1, -1}, {1, 1, 1}};
    if (!qa_world_body_write(g->services.world, child, &body, error))
        goto failed;
    goal = q1_entity(g, child);
    if (!goal || !q1_entity(g, source))
        goto cancelled;
    if (!q1_link(g, goal, error))
        goto failed;
    goal = q1_entity(g, child);
    if (!goal || !q1_entity(g, source))
        goto cancelled;
    *out = goal;
    return true;
cancelled:
    if (qa_actors_get(qa_session_actors(g->services.session), child))
        (void)qa_session_release(g->services.session, child, NULL);
    return true;
failed:
    if (qa_actors_get(qa_session_actors(g->services.session), child))
        (void)qa_session_release(g->services.session, child, NULL);
    return false;
}
static q1_actor *run_source(qa_q1_game *g, qa_actor_id source) {
    q1_actor *entity = q1_entity(g, source);
    return entity && entity->kind == Q1_MONSTER &&
                   entity->state.monster.species->species == QA_Q1_GREMLIN
               ? entity
               : NULL;
}
bool q1_gremlin_run(qa_q1_game *g, q1_actor *entity, float distance, qa_error *error) {
    qa_actor_id source = entity->id;
    q1_monster *m = &entity->state.monster;
    if (entity->physics.water_type == -5 &&
        !q1_damage(g, entity->id, g->services.physics->world_actor,
                   g->services.physics->world_actor, 2000, QA_Q1_WEAPON_COUNT, error))
        return false;
    if (!q1_alive(g, entity->id) || entity->kind != Q1_MONSTER || q1_health(g, entity->id) <= 0)
        return true;
    if (m->source.gremlin.stolen)
        entity->frame += 135;
    qa_body_state body, target;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, m->enemy), &target, NULL))
        return q1_monster_ai(g, entity, Q1_AI_RUN, distance, error);
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    float range = qa_vec_length(qa_vec_sub(target.origin, body.origin));
    if (m->source.gremlin.gorging) {
        qa_trace_result trace;
        bool visible;
        if (!q1_trace(g, body.origin, target.origin, entity->id, false, &trace, error) ||
            !q1_monster_visible(g, entity, q1_ref_actor(g, m->enemy), &visible, error))
            return false;
        if (trace.fraction != 1 || !visible) {
            m->source.gremlin.gorging = false;
            return true;
        }
        if (range < 130) {
            if (!q1_monster_face(g, entity, error))
                return false;
            entity = run_source(g, source);
            if (!entity)
                return true;
            m = &entity->state.monster;
            if (range < 45) {
                if (!q1_gremlin_melee(g, entity, error))
                    return false;
                entity = run_source(g, source);
                if (!entity)
                    return true;
                m = &entity->state.monster;
                m->attack_state = 0;
                return true;
            }
            bool moved;
            if (!qa_world_body_read(g->services.world, source, &body, error))
                return !run_source(g, source);
            if (!run_source(g, source))
                return true;
            if (!qa_physics_walk_move(g->services.physics, source, body.angles.y, distance,
                                      (float)g->elapsed, true, true, &moved, error))
                return false;
            entity = run_source(g, source);
            if (!entity)
                return true;
            m = &entity->state.monster;
            if (!moved)
                m->source.gremlin.gorging = false;
            return true;
        }
        return qa_physics_q1_move_to_goal(g->services.physics, entity->id, q1_ref_actor(g, m->enemy), distance,
                                          false, error);
    }
    if (q1_random(g) > 0.97f) {
        bool found;
        if (!find_target(g, entity, &found, error))
            return false;
        if (found)
            return true;
    }
    if (m->source.gremlin.stolen) {
        if (q1_health(g, q1_ref_actor(g, m->enemy)) < 0 && q1_classnamed(g, q1_ref_actor(g, m->enemy), g->runtime_names[Q1_NAME_PLAYER]))
            return q1_monster_play(g, entity, "gremlin_glook1", error);
        q1_actor *goal = q1_entity(g, q1_ref_actor(g, m->source.gremlin.flee_goal));
        if (!q1_gremlin_has_ammo(entity)) {
            if (goal) {
                if (!q1_remove(g, goal, error))
                    return false;
                m->source.gremlin.flee_goal = (q1_ref){0};
                entity->physics.goal = m->enemy;
            }
            return true;
        }
        if (!goal && range < 150) {
            if (!flee_goal(g, source, &goal, error))
                return false;
            if (!goal)
                return true;
            entity = q1_entity(g, source);
            if (!entity)
                return true;
            m = &entity->state.monster;
            m->source.gremlin.flee_goal = q1_ref_from(g, goal->id);
        }
        if (goal) {
            if (range > 250) {
                if (!q1_remove(g, goal, error))
                    return false;
                m->source.gremlin.flee_goal = (q1_ref){0};
                entity->physics.goal = m->enemy;
            } else {
                qa_body_state destination;
                if (!qa_world_body_read(g->services.world, goal->id, &destination, error))
                    return false;
                if (range < 160) {
                    qa_vec3 direction = qa_vec_normalize(qa_vec_sub(body.origin, target.origin));
                    qa_vec3 angles = qa_v3(
                        qa_builtin_angle_mod(atan2f(direction.z, hypotf(direction.x, direction.y)) *
                                             57.29577951308232f),
                        qa_builtin_angle_mod(atan2f(direction.y, direction.x) * 57.29577951308232f),
                        0);
                    qa_vec3 end = target.origin;
                    for (unsigned i = 0; i < 10; ++i) {
                        qa_builtin_angle_vectors(angles, &g->forward, &g->right, &g->up);
                        end = qa_vec_add(target.origin, qa_vec_scale(g->forward, 350));
                        qa_trace_result trace;
                        if (!q1_trace(g, target.origin, end, entity->id, true, &trace, error))
                            return false;
                        bool clear = trace.fraction == 1;
                        if (clear) {
                            if (!q1_trace(g, body.origin, end, entity->id, true, &trace, error))
                                return false;
                            clear = trace.fraction == 1;
                        }
                        angles.y = qa_builtin_angle_mod(angles.y + 36);
                        if (clear)
                            break;
                    }
                    destination.origin = end;
                    if (!qa_world_body_write(g->services.world, goal->id, &destination, error))
                        return false;
                }
                entity->physics.goal = q1_ref_from(g, goal->id);
                qa_vec3 delta = qa_vec_sub(destination.origin, body.origin);
                entity->physics.ideal_yaw =
                    qa_builtin_angle_mod(atan2f(delta.y, delta.x) * 57.29577951308232f);
                return qa_physics_change_yaw(g->services.physics, entity->id, (float)g->elapsed,
                                             error) &&
                       qa_physics_q1_move_to_goal(g->services.physics, entity->id, goal->id,
                                                  distance, false, error) &&
                       (!q1_alive(g, entity->id) ||
                        q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error));
            }
        }
    }
    return q1_monster_ai(g, entity, Q1_AI_RUN, distance, error) &&
           (!q1_alive(g, entity->id) || q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error));
}
