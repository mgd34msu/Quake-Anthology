#include "selected_effects_q1_temporary.h"
#include <math.h>
#include <string.h>

void frontend_fx_q1_state_initialize(frontend_fx_q1_state *state, size_t lights, size_t beams)
{
    memset(state, 0, sizeof(*state));
    state->particles.family = QA_GAME_Q1;
    state->light_capacity = lights;
    state->beam_capacity = beams;
}

frontend_fx_q1_light *frontend_fx_q1_state_light(frontend_fx_q1_state *state,
    qa_actor_id actor, uint32_t source_entity, qa_vec3 origin, double seconds,
    const frontend_fx_q1_light_recipe *recipe)
{
    size_t at = state->light_capacity;
    if (actor.registry || source_entity)
        for (size_t i = 0; i < state->light_capacity; ++i)
            if (state->lights[i].active && (actor.registry ?
                    qa_actor_id_equal(state->lights[i].actor, actor) :
                    state->lights[i].source_entity == source_entity)) { at = i; break; }
    if (at == state->light_capacity)
        for (size_t i = 0; i < state->light_capacity; ++i)
            if (!state->lights[i].active || state->lights[i].die < seconds) { at = i; break; }
    if (at == state->light_capacity) at = 0;
    frontend_fx_q1_light *light = state->lights + at;
    uint64_t identity = light->identity ? light->identity : qa_scene_identity();
    *light = (frontend_fx_q1_light){.actor = actor, .source_entity = source_entity,
        .origin = origin, .color = recipe->color, .born = seconds,
        .die = seconds + recipe->duration, .radius = recipe->radius,
        .decay = recipe->decay, .minimum = recipe->minimum, .identity = identity, .active = true};
    return light;
}

bool frontend_fx_q1_state_beam(frontend_fx_q1_state *state, qa_actor_id actor,
    bool received, const qa_q1_temp *event, double seconds, uint32_t roll_seed)
{
    size_t at = state->beam_capacity;
    for (size_t i = 0; i < state->beam_capacity; ++i)
        if (state->beams[i].active && (received ?
                state->beams[i].source_entity == event->entity :
                qa_actor_id_equal(state->beams[i].actor, actor))) { at = i; break; }
    if (at == state->beam_capacity)
        for (size_t i = 0; i < state->beam_capacity; ++i)
            if (!state->beams[i].active || state->beams[i].die < seconds) { at = i; break; }
    if (at == state->beam_capacity) return false;
    state->beams[at] = (frontend_fx_q1_beam){.actor = actor, .source_entity = event->entity,
        .start = {event->origin[0], event->origin[1], event->origin[2]},
        .end = {event->end[0], event->end[1], event->end[2]}, .die = seconds + .2,
        .type = event->type, .roll_seed = roll_seed, .active = true};
    return true;
}

bool frontend_fx_q1_state_temporary(frontend_fx_q1_state *state, qa_builtin_random *random,
    const qa_q1_temp *event, qa_actor_id actor, bool received, bool quakeworld,
    double seconds, const char **sound)
{
    *sound = NULL;
    if (event->kind == QA_Q1_TEMP_BEAM) {
        if (!frontend_fx_q1_beam_model(event->type)) return false;
        (void)frontend_fx_q1_state_beam(state, actor, received, event, seconds, 0);
        return true;
    }
    if (!frontend_fx_q1_temporary_particles(&state->particles, random, event, quakeworld, seconds))
        return false;
    frontend_fx_q1_light_recipe recipe;
    if (frontend_fx_q1_temporary_light(event, &recipe))
        frontend_fx_q1_state_light(state, (qa_actor_id){0}, 0,
            qa_v3(event->origin[0], event->origin[1], event->origin[2]), seconds, &recipe);
    *sound = frontend_fx_q1_temporary_sound(event, random);
    return true;
}

bool frontend_fx_q1_temporary_light(const qa_q1_temp *event, frontend_fx_q1_light_recipe *out)
{
    if (event->kind != QA_Q1_TEMP_COLORS &&
        (event->kind != QA_Q1_TEMP_POINT || event->type != 3)) return false;
    *out = (frontend_fx_q1_light_recipe){.radius = 350, .decay = 300,
        .duration = .5, .color = {1, 1, 1}};
    return true;
}

const char *frontend_fx_q1_temporary_sound(const qa_q1_temp *event, qa_builtin_random *random)
{
    if (event->kind == QA_Q1_TEMP_COLORS) return "weapons/r_exp3.wav";
    if (event->kind != QA_Q1_TEMP_POINT) return NULL;
    switch (event->type) {
    case 0: case 1:
        if (qa_builtin_random_integer(random) % 5) return "weapons/tink1.wav";
        switch (qa_builtin_random_integer(random) & 3) {
        case 1: return "weapons/ric1.wav";
        case 2: return "weapons/ric2.wav";
        default: return "weapons/ric3.wav";
        }
    case 3: case 4: return "weapons/r_exp3.wav";
    case 7: return "wizard/hit.wav";
    case 8: return "hknight/hit.wav";
    default: return NULL;
    }
}

int frontend_fx_q1_model_trail(uint32_t flags, bool quakeworld)
{
    if (quakeworld) {
        if (flags & 1u) return 0;
        if (flags & 2u) return 1;
    }
    return flags & 4u ? 2 : flags & 32u ? 4 : flags & 16u ? 3 : flags & 64u ? 5 :
        flags & 1u ? 0 : flags & 2u ? 1 : flags & 128u ? 6 : -1;
}

bool frontend_fx_q1_entity_light(qa_builtin_random *random, qa_vec3 origin, qa_vec3 angles,
    uint32_t effects, uint32_t model_flags, bool quakeworld, bool rerelease, double seconds,
    qa_vec3 *light_origin, frontend_fx_q1_light_recipe *out)
{
    bool present = false;
    double brief = quakeworld ? .1 : .001;
    *light_origin = origin;
    *out = (frontend_fx_q1_light_recipe){.color = {1, 1, 1}};
    if (effects & 2u) {
        qa_vec3 forward;
        qa_builtin_angle_vectors(angles, &forward, NULL, NULL);
        *light_origin = qa_vec_add(qa_vec_add(origin, qa_v3(0, 0, 16)), qa_vec_scale(forward, 18));
        out->radius = 200 + (float)(qa_builtin_random_integer(random) & 31);
        out->minimum = 32; out->duration = .1; present = true;
    }
    if (effects & 4u) {
        *light_origin = qa_vec_add(origin, qa_v3(0, 0, 16));
        out->radius = 400 + (float)(qa_builtin_random_integer(random) & 31);
        out->minimum = 0; out->duration = brief; present = true;
    }
    if (effects & 8u) {
        *light_origin = origin;
        out->radius = 200 + (float)(qa_builtin_random_integer(random) & 31);
        out->minimum = 0; out->duration = brief; present = true;
    }
    if (rerelease && !quakeworld) {
        if (effects & 16u) {
            *light_origin = origin;
            out->radius = 200 + (float)(qa_builtin_random_integer(random) & 31);
            out->minimum = 0; out->duration = .001; out->color = qa_v3(.25f, .25f, 1); present = true;
        }
        if (effects & 32u) {
            *light_origin = origin;
            out->radius = 200 + (float)(qa_builtin_random_integer(random) & 31);
            out->minimum = 0; out->duration = .001; out->color = qa_v3(1, .25f, .25f); present = true;
        }
        if (effects & 64u) {
            *light_origin = origin;
            out->radius = 64 + (float)(qa_builtin_random_integer(random) & 31);
            out->minimum = 0; out->duration = (double)(float)(seconds + .001) - seconds;
            out->color = qa_v3(1, 192.0f / 255, 120.0f / 255); present = true;
        }
    }
    if (frontend_fx_q1_model_trail(model_flags, quakeworld) == 0) {
        *light_origin = origin;
        *out = (frontend_fx_q1_light_recipe){.radius = 200,
            .duration = quakeworld ? .1 : .01, .color = {1, 1, 1}};
        present = true;
    }
    return present;
}

int frontend_fx_q1_beam_index(uint8_t type)
{
    switch (type) {
    case 5: return 0;
    case 6: return 1;
    case 9: return 2;
    case 13: return 3;
    default: return -1;
    }
}
const char *frontend_fx_q1_beam_model(uint8_t type)
{
    static const char *const models[]={"progs/bolt.mdl","progs/bolt2.mdl","progs/bolt3.mdl","progs/beam.mdl"};
    int index=frontend_fx_q1_beam_index(type);
    return index<0?NULL:models[index];
}

/* CL_UpdateTEnts quantizes pitch/yaw, advances by 30 units and picks a fresh
 * roll for each bolt. Our model transform encodes R_RotateForEntity's -pitch. */
void frontend_fx_q1_beam_begin(frontend_fx_q1_beam_cursor *cursor, qa_vec3 start, qa_vec3 end)
{
    qa_vec3 delta = qa_vec_sub(end, start);
    float yaw, pitch;
    if (delta.x == 0 && delta.y == 0) {
        yaw = 0;
        pitch = delta.z > 0 ? 90 : 270;
    } else {
        yaw = (float)(int)(atan2(delta.y, delta.x) * 180 / 3.14159265358979323846);
        if (yaw < 0) yaw += 360;
        float forward = (float)sqrt(delta.x * delta.x + delta.y * delta.y);
        pitch = (float)(int)(atan2(delta.z, forward) * 180 / 3.14159265358979323846);
        if (pitch < 0) pitch += 360;
    }
    float distance = qa_vec_length(delta);
    *cursor = (frontend_fx_q1_beam_cursor){.origin = start,
        .direction = distance != 0 ? qa_vec_scale(delta, 1 / distance) : qa_v3(0, 0, 0),
        .angles = {-pitch, yaw, 0}, .remaining = distance};
}

bool frontend_fx_q1_beam_next(frontend_fx_q1_beam_cursor *cursor, qa_builtin_random *random,
    qa_model_transform *transform)
{
    if (cursor->remaining <= 0) return false;
    cursor->angles.z = (float)(qa_builtin_random_integer(random) % 360);
    qa_vec3 axes[3], right;
    qa_builtin_angle_vectors(cursor->angles, axes, &right, axes + 2);
    axes[1] = qa_vec_scale(right, -1);
    qa_model_transform_identity(transform);
    transform->origin[0] = cursor->origin.x;
    transform->origin[1] = cursor->origin.y;
    transform->origin[2] = cursor->origin.z;
    for (unsigned i = 0; i < 3; ++i) {
        transform->axes[i][0] = axes[i].x;
        transform->axes[i][1] = axes[i].y;
        transform->axes[i][2] = axes[i].z;
    }
    cursor->origin = qa_vec_add(cursor->origin, qa_vec_scale(cursor->direction, 30));
    cursor->remaining -= 30;
    return true;
}

bool frontend_fx_q1_entity_effects(frontend_fx_particles *particles, qa_builtin_random *random,
    qa_vec3 start, qa_vec3 origin, qa_vec3 angles, uint32_t effects, uint32_t model_flags,
    bool quakeworld, bool rerelease, double seconds, qa_vec3 *light_origin,
    frontend_fx_q1_light_recipe *recipe)
{
    if (effects & 1u) frontend_fx_q1_entity(particles, random, origin, seconds);
    bool lit=frontend_fx_q1_entity_light(random,origin,angles,effects,model_flags,
        quakeworld,rerelease,seconds,light_origin,recipe);
    int type=frontend_fx_q1_model_trail(model_flags,quakeworld);
    if (type>=0) frontend_fx_q1_trail(particles,random,start,origin,(uint32_t)type,seconds);
    return lit;
}
