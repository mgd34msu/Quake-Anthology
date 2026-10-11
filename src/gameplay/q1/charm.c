#include "internal.h"

bool qa_q1_monster_charm(qa_q1_game *g, qa_actor_id actor, qa_actor_id charmer, qa_error *error) {
    q1_actor *entity = g ? q1_entity(g, actor) : NULL;
    if (!entity || entity->kind != Q1_MONSTER) {
        qa_error_set(error, QA_ERROR_ARGUMENT, actor.slot, "charm requires a native monster");
        return false;
    }
    qa_saved_actor_id reference;
    if (!qa_actors_save_reference(qa_session_actors(g->services.session), charmer, &reference,
                                  error))
        return false;
    entity->state.monster.charmer = q1_ref_from(g, charmer);
    return true;
}
static bool update_goal(qa_q1_game *g, q1_actor *entity, q1_actor **out, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    qa_body_state owner, body;
    *out = NULL;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, m->charmer), &owner, NULL))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    q1_actor *goal = q1_entity(g, q1_ref_actor(g, m->charm_goal));
    if (m->hunting_charmer == 1) {
        if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_CHARMED_GOAL], Q1_ENTITY, (qa_actor_id){0}, &goal, error))
            return false;
        qa_body_state destination = {.origin = owner.origin};
        if (!qa_world_body_write(g->services.world, goal->id, &destination, error))
            return false;
        m->charm_goal = q1_ref_from(g, goal->id);
        m->hunting_charmer = 2;
        entity->physics.goal = q1_ref_from(g, goal->id);
    }
    if (!goal)
        return true;
    qa_body_state destination;
    if (!qa_world_body_read(g->services.world, goal->id, &destination, error))
        return false;
    if (m->hunting_charmer == 2) {
        qa_trace_result trace;
        if (!q1_trace(g, body.origin, owner.origin, entity->id, false, &trace, error))
            return false;
        if (trace.fraction == 1)
            destination.origin = owner.origin;
    } else
        destination.origin =
            qa_vec_add(owner.origin,
                       qa_vec_scale(qa_vec_normalize(qa_vec_sub(body.origin, owner.origin)), 300));
    if (!qa_world_body_write(g->services.world, goal->id, &destination, error))
        return false;
    *out = goal;
    return true;
}
bool q1_charmed_hunt(qa_q1_game *g, q1_actor *entity, bool flee, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    m->hunting_charmer = 1;
    q1_actor *goal;
    if (!update_goal(g, entity, &goal, error))
        return false;
    if (flee)
        m->hunting_charmer = 3;
    else if (goal) {
        qa_body_state body, target;
        if (!qa_world_body_read(g->services.world, entity->id, &body, error) ||
            !qa_world_body_read(g->services.world, goal->id, &target, error))
            return false;
        qa_vec3 delta = qa_vec_sub(target.origin, body.origin);
        entity->physics.ideal_yaw =
            qa_builtin_angle_mod(atan2f(delta.y, delta.x) * 57.29577951308232f);
    }
    m->next_frame = q1_frame_index(m->species->walk);
    return q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error);
}
bool q1_charmed_find_target(qa_q1_game *g, q1_actor *entity, bool *out, qa_error *error) {
    q1_monster *m = &entity->state.monster;
    qa_body_state body, owner;
    *out = false;
    if (!qa_world_body_read(g->services.world, q1_ref_actor(g, m->charmer), &owner, NULL))
        return true;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    entity->effects |= 8;
    if (m->hunting_charmer) {
        q1_actor *goal;
        if (!update_goal(g, entity, &goal, error))
            return false;
        qa_body_state target;
        float distance = goal && qa_world_body_read(g->services.world, goal->id, &target, NULL)
                             ? qa_vec_length(qa_vec_sub(body.origin, target.origin))
                             : INFINITY;
        if (distance < 150) {
            if (m->hunting_charmer == 3 && distance > 120)
                return true;
            if (goal && m->hunting_charmer > 1 && !q1_remove(g, goal, error))
                return false;
            entity->physics.goal = (q1_ref){0};
            m->hunting_charmer = 0;
            m->next_frame = q1_frame_index(m->species->stand);
            *out = true;
            return q1_schedule(g, entity, 0.1, Q1_THINK_MONSTER_FRAME, error);
        }
    } else {
        float distance = qa_vec_length(qa_vec_sub(body.origin, owner.origin));
        if (distance > 200)
            return q1_charmed_hunt(g, entity, false, error);
        if (distance < 120)
            return q1_charmed_hunt(g, entity, true, error);
    }
    qa_builtin_snapshot_frame *snapshot;
    if (!q1_radius_snapshot(g, body.origin, 1500, &snapshot, error))
        return false;
    qa_actor_id selected = {0};
    float best = 1500;
    bool ok = true;
    for (size_t i = snapshot->snapshot.count; i > 0; --i) {
        qa_actor_id actor = snapshot->snapshot.ids[i - 1];
        q1_actor *candidate = q1_entity(g, actor);
        qa_builtin_actor_traits foreign = {0};
        bool monster = candidate
                           ? (candidate->physics.flags & QA_PHYSICS_MONSTER) != 0
                           : g->services.actor_traits &&
                                 g->services.actor_traits(g->services.context, actor, &foreign) &&
                                 foreign.monster;
        qa_q1_target traits;
        if (!monster || !q1_target(g, actor, &traits) || traits.notarget ||
            qa_actor_id_equal(actor, entity->id) || q1_ref_equal(q1_ref_from(g, actor), m->charmer) ||
            (candidate && candidate->kind == Q1_MONSTER &&
             q1_ref_equal(candidate->state.monster.charmer, m->charmer)) ||
            q1_health(g, actor) <= 0)
            continue;
        bool visible;
        if (!q1_monster_visible(g, entity, actor, &visible, error)) {
            ok = false;
            break;
        }
        if (!visible)
            continue;
        qa_body_state target;
        if (!qa_world_body_read(g->services.world, actor, &target, NULL))
            continue;
        qa_vec3 delta = qa_vec_sub(target.origin, body.origin);
        delta.z += traits.view_height - ((entity->physics.flags & QA_PHYSICS_SWIMMING) ? 10 : 25);
        float distance = qa_vec_length(delta);
        if (distance < best) {
            best = distance;
            selected = actor;
        }
    }
    qa_builtin_snapshot_release(snapshot);
    if (!ok)
        return false;
    qa_q1_target traits;
    if (!selected.registry || q1_ref_equal(q1_ref_from(g, selected), m->enemy) || best >= 1000 ||
        (q1_target(g, selected, &traits) && traits.invisible))
        return true;
    *out = true;
    return q1_monster_found(g, entity, selected, error);
}
bool q1_charmed_walk(qa_q1_game *g, q1_actor *entity, float distance, qa_error *error) {
    bool found;
    if (!q1_charmed_find_target(g, entity, &found, error))
        return false;
    if (found || !q1_alive(g, entity->id))
        return true;
    qa_actor_id goal =
        q1_ref_actor(g, q1_alive(g, q1_ref_actor(g, entity->physics.goal)) ? entity->physics.goal : q1_ref_from(g, q1_monster_route(g, entity)));
    if (goal.registry &&
        !qa_physics_q1_move_to_goal(g->services.physics, entity->id, goal, distance, false, error))
        return false;
    return !q1_alive(g, entity->id) || !entity->state.monster.hunting_charmer ||
           q1_schedule(g, entity, (entity->next_think - g->time) * 0.5, Q1_THINK_MONSTER_FRAME,
                       error);
}
