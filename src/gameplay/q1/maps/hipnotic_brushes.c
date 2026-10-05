#include "internal.h"

static q1_actor *brush(qa_q1_game *g, qa_actor_id id, q1_map_kind kind) {
    q1_actor *entity = q1_entity(g, id);
    return entity && entity->map && entity->map->kind == kind ? entity : NULL;
}

bool q1_map_hip_brush_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    q1_map_kind kind = entity->map->kind;
    if (kind == Q1_MAP_PUSHABLE && !entity->map->has_inline_model)
        return q1_map_fail(error, "Hipnotic brush has no inline model");
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    entity = brush(g, id, kind);
    if (!entity)
        return true;
    if (kind == Q1_MAP_BOBBING_WATER) {
        entity->physics.solid = QA_PHYSICS_NOT_SOLID;
        entity->physics.motion = QA_PHYSICS_STEP;
        entity->count = 0;
        entity->speed = 360 / (entity->speed != 0 ? entity->speed : 4);
        if (!isfinite(entity->speed))
            return q1_map_fail(error, "Hipnotic bobbing water period is too small");
        entity->map->pending.bob.amplitude = (body.bounds.maxs.z - body.bounds.mins.z) * .5f;
        entity->map->pending.bob.last_time = g->time;
    } else {
        entity->physics.solid = QA_PHYSICS_BRUSH;
        entity->physics.motion = QA_PHYSICS_PUSH;
        entity->map->mangle = body.angles;
        entity->map->pending.push_origin = body.origin;
    }
    body.angles = qa_v3(0, 0, 0);
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    entity = brush(g, id, kind);
    if (!entity)
        return true;
    if (kind == Q1_MAP_BOBBING_WATER)
        return q1_map_schedule(g, entity, .02, Q1_MAP_BOB_WATER, error) &&
               q1_link(g, entity, error);

    q1_actor *proxy;
    if (!q1_create(g, "pushablewallproxy", Q1_MAP, id, &proxy, error))
        return false;
    qa_actor_id proxy_id = proxy->id;
    if (!q1_map_allocate(g, proxy, error)) {
        (void)qa_session_release(g->services.session, proxy_id, NULL);
        return false;
    }
    proxy->map->kind = Q1_MAP_PUSHABLE_PROXY;
    proxy->map->touch_enabled = true;
    proxy->physics.solid = QA_PHYSICS_BOX;
    proxy->physics.motion = QA_PHYSICS_STEP;
    qa_vec3 half = qa_vec_scale(qa_vec_sub(body.bounds.maxs, body.bounds.mins), .5f);
    qa_vec3 origin = qa_vec_add(qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), .5f),
                                qa_v3(0, 0, 1));
    proxy->map->pending.push_origin = origin;
    qa_body_state proxy_body = {.origin = origin,
                                .bounds = {.mins = qa_vec_sub(qa_v3(-1, -1, 0), half),
                                           .maxs = qa_vec_add(qa_v3(1, 1, -2), half)}};
    if (!qa_world_body_write(g->services.world, proxy_id, &proxy_body, error)) {
        (void)qa_session_release(g->services.session, proxy_id, NULL);
        return false;
    }
    proxy = brush(g, proxy_id, Q1_MAP_PUSHABLE_PROXY);
    if (proxy && !q1_link(g, proxy, error)) {
        (void)qa_session_release(g->services.session, proxy_id, NULL);
        return false;
    }
    entity = brush(g, id, kind);
    if (!entity) {
        (void)qa_session_release(g->services.session, proxy_id, NULL);
        return true;
    }
    if (!q1_link(g, entity, error)) {
        (void)qa_session_release(g->services.session, proxy_id, NULL);
        return false;
    }
    return true;
}

bool q1_map_bob_water(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id id = entity->id;
    entity->count += entity->speed * (float)(g->time - entity->map->pending.bob.last_time);
    if (entity->count > 360)
        entity->count -= 360;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    entity = brush(g, id, Q1_MAP_BOBBING_WATER);
    if (!entity)
        return true;
    qa_vec3 forward;
    qa_builtin_angle_vectors(qa_v3(entity->count, 0, 0), &forward, NULL, NULL);
    body.origin.z = forward.z * entity->map->pending.bob.amplitude;
    if (!qa_world_body_write(g->services.world, id, &body, error))
        return false;
    entity = brush(g, id, Q1_MAP_BOBBING_WATER);
    if (!entity)
        return true;
    if (!q1_link(g, entity, error))
        return false;
    entity = brush(g, id, Q1_MAP_BOBBING_WATER);
    if (!entity)
        return true;
    entity->map->pending.bob.last_time = g->time;
    return q1_map_schedule(g, entity, .02, Q1_MAP_BOB_WATER, error);
}

bool q1_map_pushable_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    qa_actor_id id = entity->id, owner_id = q1_ref_actor(g, entity->owner);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, other, &body, NULL))
        return true;
    entity = brush(g, id, Q1_MAP_PUSHABLE_PROXY);
    if (!entity || !brush(g, owner_id, Q1_MAP_PUSHABLE))
        return true;
    float yaw = fabsf(body.velocity.x) > fabsf(body.velocity.y) ? body.velocity.x > 0 ? 0 : 180
                : body.velocity.y > 0                           ? 90
                                                                : 270;
    bool moved;
    if (!qa_physics_walk_move(g->services.physics, id, yaw, 16 * (float)g->elapsed,
                              (float)g->elapsed, true, true, &moved, error))
        return false;
    entity = brush(g, id, Q1_MAP_PUSHABLE_PROXY);
    q1_actor *owner = brush(g, owner_id, Q1_MAP_PUSHABLE);
    if (!entity || !owner)
        return true;
    if (!qa_world_body_read(g->services.world, id, &body, error))
        return false;
    entity = brush(g, id, Q1_MAP_PUSHABLE_PROXY);
    owner = brush(g, owner_id, Q1_MAP_PUSHABLE);
    if (!entity || !owner)
        return true;
    qa_vec3 position = qa_vec_add(owner->map->pending.push_origin,
                                  qa_vec_sub(body.origin, entity->map->pending.push_origin));
    if (!qa_world_body_read(g->services.world, owner_id, &body, error))
        return false;
    if (!brush(g, id, Q1_MAP_PUSHABLE_PROXY) || !brush(g, owner_id, Q1_MAP_PUSHABLE))
        return true;
    body.origin = position;
    if (!qa_world_body_write(g->services.world, owner_id, &body, error))
        return false;
    owner = brush(g, owner_id, Q1_MAP_PUSHABLE);
    return !owner || q1_link(g, owner, error);
}
