#include "internal.h"

static bool commit(qa_q1_game *g, qa_actor_id actor, const qa_body_state *body, bool teleport,
                   qa_error *error) {
    if (!qa_world_body_write(g->services.world, actor, body, error) ||
        !qa_world_link(g->services.world, actor, NULL, error))
        return false;
    qa_builtin_motion_change change = {.reason = QA_BUILTIN_MOTION_TELEPORT,
                                       .body = *body,
                                       .view_angles = body->angles,
                                       .force_view_angles = teleport,
                                       .hold_ns = teleport ? UINT64_C(700000000) : 0};
    return !q1_alive(g, actor) ||
           g->services.motion_changed(g->services.context, actor, &change, error);
}
static bool through_door(qa_q1_game *g, qa_actor_id actor, q1_actor *door, qa_error *error) {
    const q1_door_group *group = door->map->pending.mover.group;
    q1_actor *master = q1_entity(g, group ? group->members[0] : door->owner);
    if (!master || !master->map || master->map->pending.mover.position != Q1_MAP_BOTTOM)
        return true;
    group = master->map->pending.mover.group;
    qa_bounds bounds = {qa_v3(INFINITY, INFINITY, INFINITY),
                        qa_v3(-INFINITY, -INFINITY, -INFINITY)};
    size_t count = group ? group->count : 1;
    bool found = false;
    for (size_t i = 0; i < count; ++i) {
        qa_actor_id id = group ? group->members[i] : master->id;
        if (!q1_alive(g, id))
            continue;
        qa_body_state body;
        if (!qa_world_body_read(g->services.world, id, &body, error))
            return false;
        bounds = qa_bounds_union(bounds, qa_bounds_translate(body.bounds, body.origin));
        found = true;
    }
    if (!found)
        return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, actor, &body, error))
        return false;
    qa_bounds observer = qa_bounds_translate(body.bounds, body.origin);
    bool x = bounds.mins.x + 15 < observer.mins.x && observer.maxs.x < bounds.maxs.x - 15;
    bool y = bounds.mins.y + 15 < observer.mins.y && observer.maxs.y < bounds.maxs.y - 15;
    bool z = bounds.mins.z + 15 < observer.mins.z && observer.maxs.z < bounds.maxs.z - 15;
    qa_vec3 direction = {0}, origin = body.origin;
    if (x && y) {
        if (origin.z < bounds.mins.z) {
            direction.z = 1;
            origin.z = bounds.maxs.z + 25;
        } else if (origin.z > bounds.maxs.z) {
            direction.z = -1;
            origin.z = bounds.mins.z - 25;
        }
    } else if (x && z) {
        if (origin.y < bounds.mins.y) {
            direction.y = 1;
            origin.y = bounds.maxs.y + 25;
        } else if (origin.y > bounds.maxs.y) {
            direction.y = -1;
            origin.y = bounds.mins.y - 25;
        }
    } else if (y && z) {
        if (origin.x < bounds.mins.x) {
            direction.x = 1;
            origin.x = bounds.maxs.x + 25;
        } else if (origin.x > bounds.maxs.x) {
            direction.x = -1;
            origin.x = bounds.mins.x - 25;
        }
    }
    if (qa_vec_dot(direction, qa_vec_normalize(body.velocity)) < .5f)
        return true;
    body.origin = origin;
    return commit(g, actor, &body, false, error);
}
static bool through_teleport(qa_q1_game *g, qa_actor_id actor, q1_actor *teleporter,
                             qa_error *error) {
    qa_body_state body, source, destination;
    if (!qa_world_body_read(g->services.world, actor, &body, error) ||
        !qa_world_body_read(g->services.world, teleporter->id, &source, error))
        return false;
    qa_vec3 center = qa_vec_add(
        source.origin, qa_vec_scale(qa_vec_add(source.bounds.mins, source.bounds.maxs), .5f));
    if (qa_vec_dot(qa_vec_sub(center, body.origin), body.velocity) <= .1f)
        return true;
    qa_actor_id target;
    if (!qa_targets_first(g->maps->options.targets, teleporter->target, &target))
        return true;
    if (!qa_world_body_read(g->services.world, target, &destination, error))
        return false;
    q1_actor *native = q1_entity(g, target);
    body.origin = destination.origin;
    body.angles = native && native->map ? native->map->mangle : destination.angles;
    qa_vec3 forward;
    qa_builtin_angle_vectors(body.angles, &forward, NULL, NULL);
    body.velocity = qa_vec_scale(forward, 300);
    return commit(g, actor, &body, true, error);
}
bool qa_q1_game_map_observer_nearby(qa_q1_game *g, qa_actor_id actor, qa_error *error) {
    if (!g || !g->maps || !q1_alive(g, actor))
        return q1_map_fail(error, "invalid Q1 observer map actor");
    qa_body_state body;
    q1_actor_snapshot *snapshot;
    if (!qa_world_body_read(g->services.world, actor, &body, error) ||
        !q1_snapshot_actors(g, &snapshot, error))
        return false;
    bool ok = true;
    for (size_t i = 0; i < snapshot->count; ++i) {
        q1_actor *entity = q1_entity(g, snapshot->actors[i]);
        if (!entity || !entity->map ||
            (entity->map->kind != Q1_MAP_DOOR && entity->map->kind != Q1_MAP_TELEPORT))
            continue;
        qa_body_state target;
        if (!(ok = qa_world_body_read(g->services.world, entity->id, &target, error)))
            break;
        qa_vec3 center = qa_vec_add(
            target.origin, qa_vec_scale(qa_vec_add(target.bounds.mins, target.bounds.maxs), .5f));
        if (qa_vec_length(qa_vec_sub(center, body.origin)) > 75)
            continue;
        ok = entity->map->kind == Q1_MAP_DOOR ? through_door(g, actor, entity, error)
                                              : through_teleport(g, actor, entity, error);
        break;
    }
    snapshot->borrowed = false;
    return ok;
}
