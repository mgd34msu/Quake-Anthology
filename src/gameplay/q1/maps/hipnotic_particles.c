#include "internal.h"
#include "qa/game_q1_wire.h"
#include <limits.h>

static bool voice(qa_q1_game *g, q1_actor *entity, unsigned index, qa_error *error) {
    return q1_sound_resource(g, entity->id, entity->map->noise[index], 2, 1, 1, error);
}

bool q1_map_hip_particles_spawn(qa_q1_game *g, q1_actor *entity, qa_error *error) {
    q1_map_state *state = entity->map;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    entity->physics.solid = QA_PHYSICS_NOT_SOLID;
    entity->physics.motion = QA_PHYSICS_STATIONARY;
    switch (state->kind) {
    case Q1_MAP_PARTICLE_FIELD: {
        qa_vec3 center = qa_vec_scale(qa_vec_add(body.bounds.mins, body.bounds.maxs), .5f);
        qa_vec3 size =
            qa_vec_sub(qa_vec_sub(body.bounds.maxs, body.bounds.mins), qa_v3(16, 16, 16));
        qa_vec3 start = qa_vec_sub(qa_vec_add(body.bounds.mins, qa_v3(8, 8, 8)), center);
        qa_vec3 end = qa_vec_sub(qa_vec_add(body.bounds.maxs, qa_v3(7.9f, 7.9f, 7.9f)), center);
        if (size.x > size.z && size.y > size.z) {
            state->pending.particles.plane = 2;
            start.z = (start.z + end.z) * .5f;
        } else if (size.x <= size.z && size.y > size.x) {
            state->pending.particles.plane = 1;
            start.x = (start.x + end.x) * .5f;
        } else {
            state->pending.particles.plane = 0;
            start.y = (start.y + end.y) * .5f;
        }
        state->pending.particles.start = start;
        state->pending.particles.end = end;
        entity->model = QA_STRING_NONE;
        if (entity->count == 0)
            entity->count = 2;
        if (!state->particle_color)
            state->particle_color = 192;
        if ((double)entity->count < INT32_MIN || (double)entity->count > INT32_MAX)
            return q1_map_fail(error, "Q1 particle count is outside event range");
        state->use_enabled = state->touch_enabled = true;
        body.origin = center;
        break;
    }
    case Q1_MAP_TOGGLE_WALL:
        if (!state->has_inline_model)
            return q1_map_fail(error, "Q1 toggle wall has no brush model");
        entity->physics.motion = QA_PHYSICS_PUSH;
        entity->physics.solid = QA_PHYSICS_BRUSH;
        entity->model = QA_STRING_NONE;
        state->use_enabled = state->touch_enabled = true;
        for (unsigned i = 0; i < 2; ++i)
            if (!q1_map_text(g, state->noise[i]) &&
                !qa_builtin_resource(&g->services, "misc/null.wav", &state->noise[i], error))
                return false;
        state->effect_active = !(entity->spawnflags & 1);
        if (!state->effect_active)
            body.origin = qa_vec_add(body.origin, qa_v3(8000, 8000, 8000));
        break;
    case Q1_MAP_WALL_SPRITE: {
        qa_actor_id id = entity->id;
        if (!q1_map_text(g, entity->model) && !q1_model(g, entity, g->runtime_names[Q1_NAME_RESOURCE_PROGS_S_BLOOD1_SPR], error))
            return false;
        entity = q1_entity(g, id);
        if (!entity || !entity->map) return true;
        const char *model = qa_strings_cstr(qa_session_strings(g->services.session), entity->model);
        if (!qa_q1_wire_declare_model(g, model, error)) return false;
        entity = q1_entity(g, id);
        if (!entity || !entity->map) return true;
        if (!qa_world_body_read(g->services.world, id, &body, error)) return false;
        entity = q1_entity(g, id);
        if (!entity || !entity->map) return true;
        if (body.angles.x == 0 && body.angles.z == 0) {
            if (body.angles.y == -1)
                body.angles = qa_v3(-90, 0, 0);
            else if (body.angles.y == -2)
                body.angles = qa_v3(90, 0, 0);
        }
        qa_vec3 forward;
        qa_builtin_angle_vectors(body.angles, &forward, NULL, NULL);
        body.origin = qa_vec_sub(body.origin, qa_vec_scale(forward, .2f));
        return qa_world_body_write(g->services.world, entity->id, &body, error) &&
               q1_map_make_static(g, entity, error);
    }
    default:
        return q1_map_fail(error, "unknown Q1 particle map actor");
    }
    if (!qa_world_body_write(g->services.world, entity->id, &body, error) ||
        !q1_link(g, entity, error))
        return false;
    return state->kind != Q1_MAP_TOGGLE_WALL || !state->effect_active || voice(g, entity, 1, error);
}

static bool particle_field(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    q1_map_state *state = entity->map;
    if (entity->spawnflags & 1) {
        double counter = 0;
        qa_authored_target source;
        if (qa_targets_read(g->maps->options.targets, other, &source) &&
            source.classname == g->runtime_names[Q1_NAME_FUNC_COUNTER])
            (void)qa_targets_number(g->maps->options.targets, other, qa_targets_field_keys(g->maps->options.targets)[QA_TARGET_KEY_COUNTER_STATE], &counter);
        if (counter != state->counter_value)
            return true;
    }
    state->active_until = g->time + .25;
    if (q1_map_text(g, state->noise[0]) && !voice(g, entity, 0, error))
        return false;
    if (!q1_alive(g, entity->id))
        return true;
    qa_actor_id viewer = {0};
    if (!g->host.check_client)
        return q1_map_fail(error, "Q1 particle field requires check_client");
    if (!g->host.check_client(g->host.context, entity->id, &viewer, error)) return false;
    if (!viewer.registry) return true;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    qa_vec3 start = qa_vec_add(state->pending.particles.start, body.origin);
    qa_vec3 end = qa_vec_add(state->pending.particles.end, body.origin);
    unsigned plane = state->pending.particles.plane;
    const float first = plane == 1 ? start.y : start.x;
    const float last = plane == 1 ? end.y : end.x;
    const float inner_first = plane == 2 ? start.y : start.z;
    const float inner_last = plane == 2 ? end.y : end.z;
    qa_builtin_event event = {.kind = QA_BUILTIN_PARTICLES,
                              .family = QA_GAME_Q1,
                              .provider = g->options.provider,
                              .actor = entity->id,
                              .time_ns = g->time_ns,
                              .code = state->particle_color,
                              .count = (int32_t)entity->count};
    for (double a = first; plane == 0 ? a <= last : a < last; a += 16)
        for (double b = inner_first; plane == 0 ? b <= inner_last : b < inner_last; b += 16) {
            event.origin = plane == 0   ? qa_v3((float)a, start.y, (float)b)
                           : plane == 1 ? qa_v3(start.x, (float)a, (float)b)
                                        : qa_v3((float)a, (float)b, start.z);
            if (!qa_builtin_emit(&g->services, &event, error))
                return false;
            if (!q1_alive(g, entity->id))
                return true;
        }
    return true;
}

bool q1_map_hip_particles_use(qa_q1_game *g, q1_actor *entity, qa_actor_id other, qa_error *error) {
    if (entity->map->kind == Q1_MAP_PARTICLE_FIELD)
        return particle_field(g, entity, other, error);
    if (entity->map->kind != Q1_MAP_TOGGLE_WALL)
        return true;
    q1_map_state *state = entity->map;
    qa_body_state body;
    if (!qa_world_body_read(g->services.world, entity->id, &body, error))
        return false;
    state->effect_active = !state->effect_active;
    float shift = state->effect_active ? -8000 : 8000;
    body.origin = qa_vec_add(body.origin, qa_v3(shift, shift, shift));
    return qa_world_body_write(g->services.world, entity->id, &body, error) &&
           q1_link(g, entity, error) && voice(g, entity, state->effect_active ? 1u : 0u, error);
}

bool q1_map_hip_particles_touch(qa_q1_game *g, q1_actor *entity, qa_actor_id other,
                                qa_error *error) {
    q1_map_state *state = entity->map;
    if (entity->damage == 0 || g->time < state->cooldown ||
        (state->kind == Q1_MAP_PARTICLE_FIELD && g->time > state->active_until))
        return true;
    state->cooldown = g->time + .5;
    return q1_damage(g, other, entity->id, entity->id, entity->damage, QA_Q1_WEAPON_COUNT, error);
}
