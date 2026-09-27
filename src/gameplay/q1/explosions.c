#include "internal.h"

bool q1_multi_explosion_think(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    if (!q1_schedule(g, entity, entity->wait, Q1_THINK_MULTI_EXPLOSION, error))
        return false;
    if (!q1_map_multi_explosion_begin(g, entity, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    if (g->time > entity->state.effect.expires)
        return q1_remove(g, entity, error);
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_vec3 low = qa_vec_add(body.origin, body.bounds.mins),
            high = qa_vec_add(body.origin, body.bounds.maxs);
    float x = low.x + q1_random(g) * (high.x - low.x), y = low.y + q1_random(g) * (high.y - low.y),
          z = low.z + q1_random(g) * (high.z - low.z);
    q1_actor *explosion;
    if (!q1_create(g, "hip_explosion", Q1_TIMER, entity->owner, &explosion, error))
        return false;
    explosion->damage = entity->damage;
    body = (qa_body_state){.origin = {x, y, z}};
    if (!qa_world_body_write(g->services.world, explosion->id, &body, error))
        return false;
    qa_builtin_event sound = {.kind = QA_BUILTIN_SOUND,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = explosion->id,
                              .time_ns = g->time_ns,
                              .channel = 2,
                              .volume = entity->state.effect.volume,
                              .attenuation = entity->speed};
    if (!qa_builtin_resource(&g->services, "misc/shortexp.wav", &sound.resource, error) ||
        !qa_builtin_emit(&g->services, &sound, error) ||
        !q1_radius(g, explosion->id, entity->owner, entity->damage, entity->id, QA_Q1_WEAPON_COUNT,
                   error))
        return false;
    if (!q1_alive(g, explosion->id))
        return true;
    if ((entity->spawnflags & 1) &&
        !q1_effect(g, QA_BUILTIN_EXPLOSION, explosion->id, body.origin, 1, 0, error))
        return false;
    if (!q1_alive(g, explosion->id))
        return true;
    explosion->physics.solid = QA_PHYSICS_NOT_SOLID;
    explosion->physics.motion = QA_PHYSICS_STATIONARY;
    return q1_model(g, explosion, "progs/s_explod.spr", error) && q1_link(g, explosion, error) &&
           q1_schedule(g, explosion, 0.1, Q1_THINK_SPRITE, error);
}
bool qa_q1_spawn_multi_explosion(qa_q1_game *g, qa_vec3 origin, float radius, float damage,
                                 float duration, float pause, float volume, qa_actor_id *out,
                                 qa_error *error) {
    if (!g || !isfinite(radius) || radius < 0 || !isfinite(damage) || !isfinite(duration) ||
        duration < 0 || !isfinite(pause) || pause <= 0 || !isfinite(volume)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Q1 multi-explosion parameters");
        return false;
    }
    q1_actor *entity;
    if (!q1_create(g, "hip_multi_explosion", Q1_TIMER, g->services.physics->world_actor, &entity,
                   error))
        return false;
    entity->damage = damage;
    entity->wait = pause;
    entity->state.effect.expires = g->time + duration;
    entity->state.effect.volume = volume;
    qa_body_state body = {.origin = origin,
                          .bounds = {{-radius, -radius, -radius}, {radius, radius, radius}}};
    if (!qa_world_body_write(g->services.world, entity->id, &body, error) ||
        !q1_multi_explosion_think(g, entity, error))
        return false;
    if (out)
        *out = entity->id;
    return true;
}
