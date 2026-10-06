#include "selected_effects_particles.h"
#include <math.h>

static uint32_t draw(qa_builtin_random *random) { return qa_builtin_random_integer(random); }
static void append(frontend_fx_particles *state, qa_scene_q1_particle_state value)
{ if (state->count < FRONTEND_FX_PARTICLE_CAPACITY) state->values.q1[state->count++] = value; }

void frontend_fx_q1_explosion(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 origin, double seconds, bool blob)
{
    for (unsigned i = 0; i < 1024; ++i) {
        qa_scene_q1_particle_state value = {0};
        value.die = seconds + (blob ? 1 + (draw(random) & 8) * .05 : 5);
        value.ramp = blob ? 0 : (float)(draw(random) & 3);
        value.color = blob ? ((i & 1) ? 66u : 150u) + draw(random) % 6 : 111;
        value.origin.x = origin.x + ((float)(draw(random) % 32) - 16);
        value.velocity.x = (float)(draw(random) % 512) - 256;
        value.origin.y = origin.y + ((float)(draw(random) % 32) - 16);
        value.velocity.y = (float)(draw(random) % 512) - 256;
        value.origin.z = origin.z + ((float)(draw(random) % 32) - 16);
        value.velocity.z = (float)(draw(random) % 512) - 256;
        value.kind = blob ? ((i & 1) ? QA_Q1_PARTICLE_BLOB : QA_Q1_PARTICLE_BLOB2)
            : ((i & 1) ? QA_Q1_PARTICLE_EXPLODE : QA_Q1_PARTICLE_EXPLODE2);
        if (state->count == FRONTEND_FX_PARTICLE_CAPACITY) return;
        append(state, value);
    }
}

void frontend_fx_q1_impact(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 origin, qa_vec3 direction, uint32_t color, int32_t count, double seconds)
{
    if (count == 1024) { frontend_fx_q1_explosion(state, random, origin, seconds, false); return; }
    for (int32_t i = 0; i < count; ++i) {
        qa_scene_q1_particle_state value = {.kind = QA_Q1_PARTICLE_SLOW_GRAVITY,
            .velocity = qa_vec_scale(direction, 15)};
        value.die = seconds + .1 * (draw(random) % 5);
        value.color = (color & ~7u) + (draw(random) & 7);
        value.origin.x = origin.x + ((float)(draw(random) & 15) - 8);
        value.origin.y = origin.y + ((float)(draw(random) & 15) - 8);
        value.origin.z = origin.z + ((float)(draw(random) & 15) - 8);
        if (state->count == FRONTEND_FX_PARTICLE_CAPACITY) return;
        append(state, value);
    }
}

void frontend_fx_q1_particle_event(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 origin, qa_vec3 direction, int32_t color, int32_t count, double seconds)
{
    float *axes[3] = {&direction.x, &direction.y, &direction.z};
    for (unsigned i = 0; i < 3; ++i) {
        float encoded = truncf(*axes[i] * 16);
        *axes[i] = fminf(127, fmaxf(-128, encoded)) / 16;
    }
    uint8_t wire_count = (uint8_t)count;
    frontend_fx_q1_impact(state, random, origin, direction, (uint8_t)color,
        wire_count == 255 ? 1024 : wire_count, seconds);
}

bool frontend_fx_q1_temporary_particles(frontend_fx_particles *state, qa_builtin_random *random,
    const qa_q1_temp *event, bool quakeworld, double seconds)
{
    qa_vec3 origin = qa_v3(event->origin[0], event->origin[1], event->origin[2]);
    if (event->kind == QA_Q1_TEMP_COLORS) {
        if (!event->color_length) return false;
        frontend_fx_q1_color_explosion(state, random, origin, seconds,
            event->color_start, event->color_length);
        return true;
    }
    if (event->kind != QA_Q1_TEMP_POINT) return false;
    qa_vec3 zero = qa_v3(0, 0, 0);
    switch (event->type) {
    case 0: case 1:
        frontend_fx_q1_impact(state, random, origin, zero, 0, event->type ? 20 : 10, seconds); break;
    case 2:
        frontend_fx_q1_impact(state, random, origin, zero, 0, quakeworld ? 20 * event->count : 20, seconds); break;
    case 3: case 4:
        frontend_fx_q1_explosion(state, random, origin, seconds, event->type == 4); break;
    case 7: case 8:
        frontend_fx_q1_impact(state, random, origin, zero, event->type == 7 ? 20 : 226,
            event->type == 7 ? 30 : 20, seconds); break;
    case 10: case 11:
        frontend_fx_q1_splash(state, random, origin, seconds, event->type == 10); break;
    case 12: case 13:
        if (!quakeworld) return false;
        frontend_fx_q1_impact(state, random, origin, zero, event->type == 12 ? 73 : 225,
            event->type == 12 ? 20 * event->count : 50, seconds); break;
    default: return false;
    }
    return true;
}

void frontend_fx_q1_entity(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 origin, double seconds)
{
    if (state->angular[0].x == 0) for (unsigned i = 0; i < QA_BYTE_NORMAL_COUNT; ++i) {
        state->angular[i].x = ((float)((draw(random) & 255) * .01));
        state->angular[i].y = ((float)((draw(random) & 255) * .01));
        state->angular[i].z = ((float)((draw(random) & 255) * .01));
    }
    for (unsigned i = 0; i < QA_BYTE_NORMAL_COUNT && state->count < FRONTEND_FX_PARTICLE_CAPACITY; ++i) {
        float yaw = ((float)(seconds * state->angular[i].x)), pitch = ((float)(seconds * state->angular[i].y));
        float cp = ((float)(cos(pitch))), sp = ((float)(sin(pitch))), cy = ((float)(cos(yaw))), sy = ((float)(sin(yaw)));
        qa_vec3 forward = {((float)(cp * cy)), ((float)(cp * sy)), -sp}, normal = qa_byte_normals[i];
        qa_scene_q1_particle_state value = {.color = 111, .die = ((float)(seconds + .01)), .kind = QA_Q1_PARTICLE_EXPLODE};
        value.origin.x = ((float)(((float)(origin.x + ((float)(normal.x * 64)))) + ((float)(forward.x * 16))));
        value.origin.y = ((float)(((float)(origin.y + ((float)(normal.y * 64)))) + ((float)(forward.y * 16))));
        value.origin.z = ((float)(((float)(origin.z + ((float)(normal.z * 64)))) + ((float)(forward.z * 16))));
        append(state, value);
    }
}

void frontend_fx_q1_trail(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 start, qa_vec3 end, uint32_t type, double seconds)
{
    static const uint32_t ramp[6] = {109, 107, 6, 5, 4, 3};
    qa_vec3 delta = qa_vec_sub(end, start);
    float remaining = ((float)(sqrt(((float)(((float)(((float)(delta.x * delta.x)) + ((float)(delta.y * delta.y)))) + ((float)(delta.z * delta.z)))))));
    float inverse = remaining == 0 ? 0 : ((float)(1 / remaining));
    qa_vec3 direction = qa_vec_scale(delta, inverse), point = start;
    while (remaining > 0) {
        remaining = ((float)(remaining - 3));
        if (state->count == FRONTEND_FX_PARTICLE_CAPACITY) return;
        qa_scene_q1_particle_state value = {.origin = point, .die = ((float)(seconds + 2)), .kind = QA_Q1_PARTICLE_STATIC};
        if (type == 0 || type == 1) {
            value.ramp = (float)((draw(random) & 3) + (type == 1 ? 2 : 0));
            value.color = ramp[(unsigned)value.ramp]; value.kind = QA_Q1_PARTICLE_FIRE;
            value.origin.x = point.x + ((float)(draw(random) % 6) - 3);
            value.origin.y = point.y + ((float)(draw(random) % 6) - 3);
            value.origin.z = point.z + ((float)(draw(random) % 6) - 3);
        } else if (type == 2 || type == 4) {
            value.color = 67 + (draw(random) & 3); value.kind = QA_Q1_PARTICLE_GRAVITY;
            value.origin.x = point.x + ((float)(draw(random) % 6) - 3);
            value.origin.y = point.y + ((float)(draw(random) % 6) - 3);
            value.origin.z = point.z + ((float)(draw(random) % 6) - 3);
            if (type == 4) remaining = ((float)(remaining - 3));
        } else if (type == 3 || type == 5) {
            value.die = ((float)(seconds + .5));
            value.color = (type == 3 ? 52u : 230u) + ((state->tracer_count & 4) << 1);
            ++state->tracer_count;
            value.velocity = (state->tracer_count & 1) ? qa_v3(30 * direction.y, -30 * direction.x, 0)
                : qa_v3(-30 * direction.y, 30 * direction.x, 0);
        } else {
            value.color = 152 + (draw(random) & 3); value.die = ((float)(seconds + .3));
            value.origin.x = point.x + ((float)(draw(random) & 15) - 8);
            value.origin.y = point.y + ((float)(draw(random) & 15) - 8);
            value.origin.z = point.z + ((float)(draw(random) & 15) - 8);
        }
        append(state, value); point = qa_vec_add(point, direction);
    }
}

void frontend_fx_q1_color_explosion(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 origin, double seconds, uint32_t color_start, uint32_t color_length)
{
    for (unsigned i = 0; i < 512 && state->count < FRONTEND_FX_PARTICLE_CAPACITY; ++i) {
        qa_scene_q1_particle_state value = {.color = color_start + i % color_length,
            .die = seconds + .3, .kind = QA_Q1_PARTICLE_BLOB};
        value.origin.x = origin.x + ((float)(draw(random) % 32) - 16);
        value.velocity.x = (float)(draw(random) % 512) - 256;
        value.origin.y = origin.y + ((float)(draw(random) % 32) - 16);
        value.velocity.y = (float)(draw(random) % 512) - 256;
        value.origin.z = origin.z + ((float)(draw(random) % 32) - 16);
        value.velocity.z = (float)(draw(random) % 512) - 256;
        append(state, value);
    }
}

void frontend_fx_q1_splash(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 origin, double seconds, bool lava)
{
    int step = lava ? 1 : 4;
    for (int i = -16; i < 16; i += step) for (int j = -16; j < 16; j += step)
        for (int k = lava ? 0 : -24; k < (lava ? 1 : 32); k += 4) {
            qa_scene_q1_particle_state value = {.kind = QA_Q1_PARTICLE_SLOW_GRAVITY};
            value.die = seconds + (lava ? 2 + (draw(random) & 31) * .02 : .2 + (draw(random) & 7) * .02);
            value.color = (lava ? 224u : 7u) + (draw(random) & 7);
            qa_vec3 direction = {(float)(j * 8), (float)(i * 8), (float)(k * 8)};
            if (lava) {
                direction.x += (float)(draw(random) & 7); direction.y += (float)(draw(random) & 7); direction.z = 256;
                value.origin = qa_v3(origin.x + direction.x, origin.y + direction.y, origin.z + (float)(draw(random) & 63));
            } else {
                value.origin.x = origin.x + (float)i + (float)(draw(random) & 3);
                value.origin.y = origin.y + (float)j + (float)(draw(random) & 3);
                value.origin.z = origin.z + (float)k + (float)(draw(random) & 3);
            }
            value.velocity = qa_vec_scale(qa_vec_normalize(direction), (float)(50 + (draw(random) & 63)));
            if (state->count == FRONTEND_FX_PARTICLE_CAPACITY) return;
            append(state, value);
        }
}
