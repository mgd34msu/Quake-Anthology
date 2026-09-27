#include "internal.h"
#include <float.h>

bool qa_q1_game_path_read(const qa_q1_game *g, qa_actor_id actor, qa_q1_path_state *out) {
    const q1_actor *entity = g ? q1_entity_const(g, actor) : NULL;
    if (!entity || !entity->native || !out)
        return false;
    *out = (qa_q1_path_state){.owner = entity->owner};
    if (entity->kind == Q1_MONSTER) {
        const q1_monster *m = &entity->state.monster;
        out->move_target = m->move_target;
        out->enemy = m->enemy;
        out->previous_corner = m->previous_corner;
        out->pause_until = m->pause_until;
    }
    return true;
}

bool qa_q1_game_path_change(qa_q1_game *g, qa_actor_id actor, const qa_q1_path_change *change,
                            qa_error *error) {
    q1_actor *entity = g ? q1_entity(g, actor) : NULL;
    if (!entity || !entity->native || !change || change->kind < QA_Q1_PATH_OWNER ||
        change->kind > QA_Q1_PATH_CANCEL_PAUSE)
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
        qa_vec3 direction = qa_vec_sub(destination.origin, self.origin);
        entity->physics.ideal_yaw =
            qa_builtin_angle_mod(atan2f(direction.y, direction.x) * 57.29577951308232f);
        return true;
    }
    case QA_Q1_PATH_PAUSE_END:
        if (!isfinite(change->pause_until) || fabs(change->pause_until) > FLT_MAX)
            return q1_map_fail(error, "invalid Q1 path pause deadline");
        m->pause_until = (float)change->pause_until;
        return !m->path_end || q1_monster_play(g, entity, m->species->stand, error);
    case QA_Q1_PATH_CANCEL_PAUSE:
        m->pause_until = 0;
        m->addon.waiting = m->addon.path_wait = false;
        m->addon.normal_use = true;
        return true;
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
    q1_actor_snapshot *targets;
    if (!q1_snapshot_targets(g, g->maps->options.targets, trigger->target, &targets, error))
        return false;
    bool ok = true;
    for (size_t i = 0; i < targets->count && q1_alive(g, trigger_id); ++i) {
        trigger = q1_entity(g, trigger_id);
        if (!trigger || !trigger->map)
            break;
        qa_actor_id actor = targets->actors[i];
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
    targets->borrowed = false;
    return ok;
}
