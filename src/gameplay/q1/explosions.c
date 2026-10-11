#include "internal.h"

static void release_fresh(qa_q1_game *g, qa_actor_id actor) {
    if (qa_actors_get(qa_session_actors(g->services.session), actor))
        (void)qa_session_release(g->services.session, actor, NULL);
}

bool q1_multi_explosion_think(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    qa_actor_id parent = entity->id;
    if (!q1_schedule(g, entity, entity->wait, Q1_THINK_MULTI_EXPLOSION, error))
        return false;
    if (!q1_map_multi_explosion_begin(g, entity, error))
        return false;
    entity = q1_entity(g, parent);
    if (!entity)
        return true;
    if (g->time > entity->state.effect.expires)
        return q1_remove(g, entity, error);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, parent, &body, error))
        return false;
    entity = q1_entity(g, parent);
    if (!entity)
        return true;
    qa_vec3 low = qa_vec_add(body.origin, body.bounds.mins),
            high = qa_vec_add(body.origin, body.bounds.maxs);
    q1_actor *explosion;
    if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_HIP_EXPLOSION], Q1_TIMER, q1_ref_actor(g, entity->owner), &explosion, error))
        return false;
    qa_actor_id child = explosion->id;
    entity = q1_entity(g, parent);
    explosion = q1_entity(g, child);
    if (!entity || !explosion)
        goto retired;
    explosion->owner = entity->owner;
    explosion->damage = entity->damage;
    float x = low.x + q1_random(g) * (high.x - low.x), y = low.y + q1_random(g) * (high.y - low.y),
          z = low.z + q1_random(g) * (high.z - low.z);
    body = (qa_body_state){.origin = {x, y, z}};
    if (!qa_world_body_write(g->services.world, child, &body, error))
        goto failed;
    entity = q1_entity(g, parent);
    explosion = q1_entity(g, child);
    if (!entity || !explosion)
        goto retired;
    qa_builtin_event sound = {.kind = QA_BUILTIN_SOUND,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = child,
                              .time_ns = g->time_ns,
                              .channel = 2,
                              .volume = entity->state.effect.volume,
                              .attenuation = entity->speed};
    if (!qa_builtin_resource(&g->services, "misc/shortexp.wav", &sound.resource, error) ||
        !qa_builtin_emit(&g->services, &sound, error))
        goto failed;
    entity = q1_entity(g, parent);
    explosion = q1_entity(g, child);
    if (!entity || !explosion)
        goto retired;
    if (!q1_radius(g, child, q1_ref_actor(g, entity->owner), entity->damage, parent, QA_Q1_WEAPON_COUNT, error))
        goto failed;
    entity = q1_entity(g, parent);
    explosion = q1_entity(g, child);
    if (!entity || !explosion)
        goto retired;
    if (entity->spawnflags & 1) {
        if (!qa_world_body_read(g->services.world, child, &body, error))
            goto failed;
        if (!q1_entity(g, parent) || !q1_entity(g, child))
            goto retired;
        if (!q1_effect(g, QA_BUILTIN_EXPLOSION, child, body.origin, 1, 0, error))
            goto failed;
    }
    explosion = q1_entity(g, child);
    if (!q1_entity(g, parent) || !explosion)
        goto retired;
    if (!q1_sprite_prepare(g, explosion, error))
        goto failed;
    explosion = q1_entity(g, child);
    if (!q1_entity(g, parent) || !explosion)
        goto retired;
    if (!q1_link(g, explosion, error))
        goto failed;
    explosion = q1_entity(g, child);
    if (!q1_entity(g, parent) || !explosion)
        goto retired;
    if (!q1_schedule(g, explosion, 0.1, Q1_THINK_SPRITE, error))
        goto failed;
    return true;
failed:
    release_fresh(g, child);
    return false;
retired:
    release_fresh(g, child);
    return true;
}
bool qa_q1_spawn_multi_explosion(qa_q1_game *g, qa_vec3 origin, float radius, float damage,
                                 float duration, float pause, float volume, qa_actor_id *out,
                                 qa_error *error) {
    if (out)
        *out = (qa_actor_id){0};
    if (!g || !qa_vec_finite(origin) || !isfinite(radius) || radius < 0 || !isfinite(damage) ||
        !isfinite(duration) || duration < 0 || !isfinite(pause) || pause <= 0 || !isfinite(volume)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 multi-explosion parameters");
        return false;
    }
    qa_q1_game_operation operation = {0};
    if (!qa_q1_game_operation_begin(g, &operation, error))
        return false;
    bool ok = false;
    qa_actor_id parent = {0};
    q1_actor *entity;
    qa_actor_id world = g->services.physics ? g->services.physics->world_actor : (qa_actor_id){0};
    if (!q1_create(g, g->runtime_names[Q1_NAME_CLASS_HIP_MULTI_EXPLOSION], Q1_TIMER, world, &entity,
                   error))
        goto finish;
    ok = true;
    parent = entity->id;
    entity = q1_entity(g, parent);
    if (!entity)
        goto finish;
    entity->damage = damage;
    entity->wait = pause;
    entity->state.effect.expires = g->time + duration;
    entity->state.effect.volume = volume;
    qa_body_state body = {.origin = origin,
                          .bounds = {{-radius, -radius, -radius}, {radius, radius, radius}}};
    if (!qa_world_body_write(g->services.world, parent, &body, error)) {
        ok = false;
        goto finish;
    }
    entity = q1_entity(g, parent);
    if (!entity)
        goto finish;
    ok = q1_multi_explosion_think(g, entity, error);
    if (ok && out && q1_alive(g, parent))
        *out = parent;
finish:
    if (!qa_q1_game_operation_live(&operation)) {
        if (ok || (error && error->code == QA_OK))
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q1 source retired during multi-explosion spawn");
        ok = false;
    }
    if (!ok) {
        release_fresh(g, parent);
        if (out)
            *out = (qa_actor_id){0};
    }
    qa_q1_game_operation_end(&operation);
    return ok;
}
