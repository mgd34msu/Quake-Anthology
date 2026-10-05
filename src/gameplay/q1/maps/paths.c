#include "internal.h"
#include <float.h>

bool qa_q1_game_path_read(const qa_q1_game *g, qa_actor_id actor, qa_q1_path_state *out) {
    const q1_actor *entity = g ? q1_entity_const(g, actor) : NULL;
    if (!entity || !entity->native || !out)
        return false;
    *out = (qa_q1_path_state){.owner = entity->owner};
    if (entity->map &&
        (entity->map->kind == Q1_MAP_ENDING_ACTOR || entity->map->kind == Q1_MAP_BUZZSAW)) {
        out->move_target = entity->map->pending.follower.move_target;
        out->pause_until = entity->map->pause_time;
        out->path = entity->target;
    }
    if (entity->kind == Q1_MONSTER) {
        const q1_monster *m = &entity->state.monster;
        out->move_target = m->move_target;
        out->enemy = m->enemy;
        out->old_enemy = m->old_enemy;
        out->previous_corner = m->previous_corner;
        out->path = m->path;
        out->pause_until = m->pause_until;
        out->follow_until = m->follow_until;
        const qa_authored_monster *row = g->maps ? qa_targets_monster(g->maps->options.targets, actor) : NULL;
        if (row) out->follow_until = (double)row->follow_until_ns / 1e9;
        out->monster = true;
    }
    return true;
}

bool qa_q1_game_path_change(qa_q1_game *g, qa_actor_id actor, const qa_q1_path_change *change,
                            qa_error *error) {
    q1_actor *entity = g ? q1_entity(g, actor) : NULL;
    if (!entity || !entity->native || !change || change->kind < QA_Q1_PATH_OWNER ||
        change->kind > QA_Q1_PATH_FOUND)
        return q1_map_fail(error, "invalid Q1 path change");
    if (change->kind == QA_Q1_PATH_OWNER) {
        entity->owner = change->reference;
        return true;
    }
    if (entity->kind != Q1_MONSTER)
        return q1_map_fail(error, "Q1 actor has no monster path continuation");
    q1_monster *m = &entity->state.monster;
    switch (change->kind) {
    case QA_Q1_PATH_VISIT:
        m->previous_corner = change->reference;
        return true;
    case QA_Q1_PATH_DESTINATION: {
        qa_body_state self, destination = {0};
        if ((change->target &&
             !qa_strings_cstr(qa_session_strings(g->services.session), change->target)) ||
            (change->reference.registry && !q1_alive(g, change->reference)))
            return q1_map_fail(error, "invalid Q1 path destination");
        if (!qa_world_body_read(g->services.world, actor, &self, error))
            return false;
        if (!q1_alive(g, actor))
            return true;
        if (change->reference.registry &&
            !qa_world_body_read(g->services.world, change->reference, &destination, error))
            return false;
        entity = q1_entity(g, actor);
        if (!entity || !entity->native || entity->kind != Q1_MONSTER ||
            (change->reference.registry && !q1_alive(g, change->reference)))
            return true;
        m = &entity->state.monster;
        m->path = change->reference.registry ? change->target : QA_STRING_NONE;
        entity->physics.goal = m->move_target = change->reference;
        if (g->maps && !change->combat_route) qa_targets_monster_route(g->maps->options.targets, actor,
            m->path, change->reference);
        qa_vec3 direction = qa_vec_sub(destination.origin, self.origin);
        entity->physics.ideal_yaw =
            qa_builtin_angle_mod(atan2f(direction.y, direction.x) * 57.29577951308232f);
        return true;
    }
    case QA_Q1_PATH_PAUSE_END:
    case QA_Q1_PATH_STAND:
        if (!isfinite(change->pause_until) || fabs(change->pause_until) > FLT_MAX)
            return q1_map_fail(error, "invalid Q1 path pause deadline");
        m->pause_until = (float)change->pause_until;
        return (change->kind == QA_Q1_PATH_PAUSE_END && !m->path_end) ||
               q1_monster_play(g, entity, m->species->stand, error);
    case QA_Q1_PATH_CANCEL_PAUSE:
        m->pause_until = 0;
        m->addon.waiting = m->addon.path_wait = false;
        m->addon.normal_use = true;
        return true;
    case QA_Q1_PATH_FOLLOW_BEGIN: {
        uint16_t frame = q1_frame_index(m->species->walk);
        if (frame == UINT16_MAX)
            return q1_map_fail(error, "Q1 follow target has no walk continuation");
        m->old_enemy = change->reference;
        m->enemy = entity->physics.enemy = (qa_actor_id){0};
        m->next_frame = frame;
        entity->think = Q1_THINK_MONSTER_FRAME;
        return true;
    }
    case QA_Q1_PATH_FOLLOW_UNTIL:
        if (!isfinite(change->follow_until) || fabs(change->follow_until) > FLT_MAX)
            return q1_map_fail(error, "invalid Q1 follow cooldown deadline");
        {
            qa_authored_monster *row = g->maps ? qa_targets_monster(g->maps->options.targets, actor) : NULL;
            if (row) row->follow_until_ns = (uint64_t)(change->follow_until * 1e9);
            else m->follow_until = (float)change->follow_until;
        }
        return true;
    case QA_Q1_PATH_FOUND:
        return !q1_alive(g, change->reference) ||
               q1_monster_found(g, entity, change->reference, error);
    case QA_Q1_PATH_OWNER:
        break;
    }
    return q1_map_fail(error, "unknown Q1 path change");
}

static bool read_path(qa_q1_game *g, qa_actor_id actor, qa_q1_path_state *out) {
    if (qa_q1_game_path_read(g, actor, out))
        return true;
    return q1_alive(g, actor) && g->maps->options.path_read &&
           g->maps->options.path_read(g->maps->options.context, actor, out);
}
static bool change_path(qa_q1_game *g, qa_actor_id actor, qa_q1_path_change change,
                        qa_error *error) {
    if (!q1_alive(g, actor))
        return true;
    const q1_actor *entity = q1_entity_const(g, actor);
    if (entity && entity->native)
        return qa_q1_game_path_change(g, actor, &change, error);
    if (!g->maps->options.path_change)
        return q1_map_fail(error, "Q1 path control needs the foreign continuation owner");
    return g->maps->options.path_change(g->maps->options.context, actor, &change, error);
}
static bool destination(qa_q1_game *g, qa_actor_id actor, qa_string_id name, qa_actor_id *out,
                        qa_error *error) {
    *out = (qa_actor_id){0};
    (void)qa_targets_first(g->maps->options.targets, name, out);
    return change_path(
        g, actor,
        (qa_q1_path_change){.kind = QA_Q1_PATH_DESTINATION, .reference = *out, .target = name},
        error);
}

bool q1_map_path_touch(qa_q1_game *g, q1_actor *corner, qa_actor_id actor, qa_error *error) {
    qa_actor_id corner_id = corner->id;
    qa_q1_path_state mover;
    if (!read_path(g, actor, &mover) || !qa_actor_id_equal(mover.move_target, corner_id) ||
        mover.enemy.registry)
        return true;
    corner = q1_entity(g, corner_id);
    if (!corner || !corner->map || corner->map->kind != Q1_MAP_PATH || !q1_alive(g, actor))
        return true;
    corner->owner = actor;
    qa_q1_path_state previous;
    if (q1_classnamed(g, mover.previous_corner, "path_corner") &&
        read_path(g, mover.previous_corner, &previous) &&
        qa_actor_id_equal(previous.owner, mover.previous_corner) &&
        !change_path(g, mover.previous_corner, (qa_q1_path_change){.kind = QA_Q1_PATH_OWNER},
                     error))
        return false;
    if (!q1_alive(g, corner_id) || !q1_alive(g, actor))
        return true;
    if (!change_path(g, actor,
                     (qa_q1_path_change){.kind = QA_Q1_PATH_VISIT, .reference = corner_id}, error))
        return false;
    if (!q1_alive(g, corner_id) || !read_path(g, actor, &mover) || mover.pause_until > g->time)
        return true;
    if (q1_classnamed(g, actor, "monster_ogre") &&
        !q1_sound(g, actor, "ogre/ogdrag.wav", 2, 2, error))
        return false;
    corner = q1_entity(g, corner_id);
    if (!corner || !corner->map || corner->map->kind != Q1_MAP_PATH || !q1_alive(g, actor))
        return true;
    qa_actor_id next;
    if (!destination(g, actor, corner->target, &next, error))
        return false;
    corner = q1_entity(g, corner_id);
    if (!corner || !corner->map || corner->map->kind != Q1_MAP_PATH || !q1_alive(g, actor))
        return true;
    if (!next.registry || corner->wait != 0)
        return change_path(
            g, actor,
            (qa_q1_path_change){.kind = QA_Q1_PATH_PAUSE_END,
                                .pause_until = g->time + (next.registry ? corner->wait : 999999)},
            error);
    return true;
}

bool q1_map_path_use(qa_q1_game *g, q1_actor *trigger, qa_error *error) {
    qa_actor_id trigger_id = trigger->id;
    qa_builtin_snapshot_frame *targets;
    if (!q1_snapshot_targets(g, g->maps->options.targets, trigger->target, &targets, error))
        return false;
    bool ok = true;
    for (size_t i = 0; i < targets->snapshot.count && q1_alive(g, trigger_id); ++i) {
        trigger = q1_entity(g, trigger_id);
        if (!trigger || !trigger->map)
            break;
        qa_actor_id actor = targets->snapshot.ids[i];
        if (!q1_alive(g, actor))
            continue;
        if (trigger->map->kind == Q1_MAP_CANCEL_PAUSE) {
            qa_physics_properties physics;
            if (!g->services.physics->services.read(g->services.physics->services.context, actor,
                                                    &physics) ||
                !(physics.flags & QA_PHYSICS_MONSTER))
                continue;
            ok = change_path(g, actor, (qa_q1_path_change){.kind = QA_Q1_PATH_CANCEL_PAUSE}, error);
        } else {
            qa_authored_target corner;
            if (!qa_targets_read(g->maps->options.targets, actor, &corner) ||
                !q1_classnamed(g, actor, "path_corner"))
                continue;
            ok = qa_targets_set_target(g->maps->options.targets, actor, trigger->map->netname,
                                       error);
            if (!ok || !q1_alive(g, trigger_id))
                break;
            qa_q1_path_state occupant, mover;
            if (!read_path(g, actor, &occupant) || !read_path(g, occupant.owner, &mover))
                continue;
            qa_actor_id previous = {0};
            (void)qa_targets_first(g->maps->options.targets, corner.target, &previous);
            if (qa_actor_id_equal(mover.move_target, previous)) {
                qa_authored_target current;
                qa_actor_id next;
                if (qa_targets_read(g->maps->options.targets, actor, &current))
                    ok = destination(g, occupant.owner, current.target, &next, error);
            }
        }
        if (!ok)
            break;
    }
    qa_builtin_snapshot_release(targets);
    return ok;
}

bool q1_map_hip_path_touch(qa_q1_game *g, q1_actor *corner, qa_actor_id actor, qa_error *error) {
    qa_actor_id corner_id = corner->id;
    qa_q1_path_state mover;
    if (!read_path(g, actor, &mover) || !mover.monster || mover.enemy.registry)
        return true;
    corner = q1_entity(g, corner_id);
    if (!corner || !corner->map || corner->map->kind != Q1_MAP_PATH || !q1_alive(g, actor) ||
        mover.path != corner->targetname)
        return true;
    if (q1_classnamed(g, actor, "monster_ogre") &&
        !q1_sound(g, actor, "ogre/ogdrag.wav", 2, 2, error))
        return false;
    corner = q1_entity(g, corner_id);
    if (!corner || !corner->map || corner->map->kind != Q1_MAP_PATH || !q1_alive(g, actor))
        return true;
    if (q1_map_text(g, corner->target)) {
        qa_actor_id next;
        if (!destination(g, actor, corner->target, &next, error))
            return false;
        corner = q1_entity(g, corner_id);
        if (!corner || !corner->map || corner->map->kind != Q1_MAP_PATH || !q1_alive(g, actor))
            return true;
        if (next.registry)
            return corner->delay == 0 ||
                   change_path(g, actor,
                               (qa_q1_path_change){.kind = QA_Q1_PATH_STAND,
                                                   .pause_until = g->time + corner->delay},
                               error);
    }
    return change_path(
        g, actor, (qa_q1_path_change){.kind = QA_Q1_PATH_STAND, .pause_until = g->time + 999999},
        error);
}

static bool follow_eye(qa_q1_game *g, qa_actor_id actor, qa_vec3 *out) {
    qa_body_state body;
    if (!q1_alive(g, actor) || !qa_world_body_read(g->services.world, actor, &body, NULL))
        return false;
    qa_vec3 offset;
    if (!qa_targets_vector(g->maps->options.targets, actor, "view_ofs", &offset)) {
        qa_q1_target target;
        offset = qa_v3(0, 0, q1_target(g, actor, &target) ? target.view_height : 25);
    }
    if (!q1_alive(g, actor))
        return false;
    *out = qa_vec_add(body.origin, offset);
    return true;
}
bool q1_map_follow_touch(qa_q1_game *g, q1_actor *trigger, qa_actor_id actor, qa_error *error) {
    qa_actor_id trigger_id = trigger->id;
    qa_physics_properties physics;
    qa_q1_path_state mover;
    if (!g->services.physics->services.read(g->services.physics->services.context, actor,
                                            &physics) ||
        !(physics.flags & QA_PHYSICS_MONSTER) || q1_classnamed(g, actor, "monster_decoy") ||
        !read_path(g, actor, &mover) || !mover.monster || mover.follow_until > g->time)
        return true;
    qa_vec3 start, end = {0};
    qa_actor_id world =
        g->maps->world_actor.registry ? g->maps->world_actor : g->services.physics->world_actor;
    if (!follow_eye(g, actor, &start))
        return true;
    if (mover.enemy.registry) {
        if (!follow_eye(g, mover.enemy, &end))
            return true;
    } else if (q1_alive(g, world)) {
        qa_body_state world_body;
        if (!qa_world_body_read(g->services.world, world, &world_body, error))
            return false;
        end = world_body.origin;
    }
    qa_trace_result trace;
    if (!q1_trace(g, start, end, actor, true, &trace, error))
        return false;
    if (trace.fraction == 1 || !q1_alive(g, actor) || !q1_alive(g, trigger_id))
        return true;
    if (mover.enemy.registry &&
        !change_path(g, actor,
                     (qa_q1_path_change){.kind = QA_Q1_PATH_FOLLOW_BEGIN, .reference = mover.enemy},
                     error))
        return false;
    trigger = q1_entity(g, trigger_id);
    if (!trigger || !trigger->map || trigger->map->kind != Q1_MAP_FOLLOW || !q1_alive(g, actor))
        return true;
    qa_actor_id next;
    if (!destination(g, actor, trigger->target, &next, error) ||
        !change_path(
            g, actor,
            (qa_q1_path_change){.kind = QA_Q1_PATH_FOLLOW_UNTIL, .follow_until = g->time + 2},
            error))
        return false;
    if (next.registry || !read_path(g, actor, &mover))
        return true;
    if (mover.old_enemy.registry)
        return change_path(
            g, actor, (qa_q1_path_change){.kind = QA_Q1_PATH_FOUND, .reference = mover.old_enemy},
            error);
    if (!g->host.check_client)
        return q1_map_fail(error, "Q1 follow needs source check-client service");
    qa_actor_id client = {0};
    (void)g->host.check_client(g->host.context, actor, &client);
    /* Source FoundTarget is entered for checkclient's world/null result. */
    if (!client.registry && q1_alive(g, world))
        return change_path(
            g, actor, (qa_q1_path_change){.kind = QA_Q1_PATH_FOUND, .reference = world}, error);
    return change_path(
        g, actor, (qa_q1_path_change){.kind = QA_Q1_PATH_STAND, .pause_until = g->time + 999999},
        error);
}
