#include "selected_effects_particles.h"
#include <math.h>

static uint32_t draw(qa_builtin_random *random) { return qa_builtin_random_integer(random); }
static double unit(qa_builtin_random *random) { return (double)(draw(random) & 32767) / 32767; }
static double signed_unit(qa_builtin_random *random) { return 2 * unit(random) - 1; }
static qa_vec3 signed_vector(qa_builtin_random *random, double scale)
{
    float x = (float)(signed_unit(random) * scale);
    float y = (float)(signed_unit(random) * scale);
    float z = (float)(signed_unit(random) * scale);
    return qa_v3(x, y, z);
}
static frontend_fx_q2_particle particle(double seconds)
{ return (frontend_fx_q2_particle){.spawn_milliseconds = seconds * 1000, .alpha = 1}; }
static bool append(frontend_fx_particles *state, frontend_fx_q2_particle value)
{
    if (state->count == FRONTEND_FX_PARTICLE_CAPACITY) return false;
    state->values.q2[state->count++] = value; return true;
}
static void basis(qa_vec3 direction, qa_vec3 *right, qa_vec3 *up)
{
    qa_vec3 initial = {direction.z, -direction.x, direction.y};
    *right = qa_vec_normalize(qa_vec_sub(initial, qa_vec_scale(direction, qa_vec_dot(initial, direction))));
    *up = qa_vec_cross(*right, direction);
}
static void spread(frontend_fx_q2_particle *value, qa_builtin_random *random,
    qa_vec3 point, double origin_scale, double velocity_scale)
{
    value->origin.x = (float)(point.x + signed_unit(random) * origin_scale);
    value->velocity.x = (float)(signed_unit(random) * velocity_scale);
    value->origin.y = (float)(point.y + signed_unit(random) * origin_scale);
    value->velocity.y = (float)(signed_unit(random) * velocity_scale);
    value->origin.z = (float)(point.z + signed_unit(random) * origin_scale);
    value->velocity.z = (float)(signed_unit(random) * velocity_scale);
}

void frontend_fx_q2_impact_particles(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 origin, qa_vec3 direction, uint32_t color, int32_t count, double seconds, frontend_fx_q2_impact kind)
{
    for (int32_t i = 0; i < count; ++i) {
        frontend_fx_q2_particle value = particle(seconds);
        value.color = color + ((kind == FRONTEND_FX_Q2_FIXED || kind == FRONTEND_FX_Q2_UP) ? 0 : draw(random) & 7);
        uint32_t mask = kind == FRONTEND_FX_Q2_NORMAL ? 31 : kind == FRONTEND_FX_Q2_BLASTER ? 15 : 7;
        double distance = draw(random) & mask;
        value.origin.x = (float)(origin.x + ((double)(draw(random) & 7) - 4) + distance * direction.x);
        value.velocity.x = (float)(kind == FRONTEND_FX_Q2_BLASTER ? direction.x * 30 + signed_unit(random) * 40 : signed_unit(random) * 20);
        value.origin.y = (float)(origin.y + ((double)(draw(random) & 7) - 4) + distance * direction.y);
        value.velocity.y = (float)(kind == FRONTEND_FX_Q2_BLASTER ? direction.y * 30 + signed_unit(random) * 40 : signed_unit(random) * 20);
        value.origin.z = (float)(origin.z + ((double)(draw(random) & 7) - 4) + distance * direction.z);
        value.velocity.z = (float)(kind == FRONTEND_FX_Q2_BLASTER ? direction.z * 30 + signed_unit(random) * 40 : signed_unit(random) * 20);
        value.acceleration.z = kind == FRONTEND_FX_Q2_UP ? 40 : -40;
        value.alpha_velocity = (float)(-1 / (.5 + unit(random) * .3));
        if (!append(state, value)) return;
    }
}

void frontend_fx_q2_explosion(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 origin, double seconds, bool bfg)
{
    for (unsigned i = 0; i < 256; ++i) {
        frontend_fx_q2_particle value = particle(seconds);
        value.color = (bfg ? 0xd0u : 0xe0u) + (draw(random) & 7);
        value.origin.x = origin.x + ((float)(draw(random) % 32) - 16);
        value.velocity.x = (float)(draw(random) % 384) - 192;
        value.origin.y = origin.y + ((float)(draw(random) % 32) - 16);
        value.velocity.y = (float)(draw(random) % 384) - 192;
        value.origin.z = origin.z + ((float)(draw(random) % 32) - 16);
        value.velocity.z = (float)(draw(random) % 384) - 192;
        value.acceleration.z = -40; value.alpha_velocity = (float)(-.8 / (.5 + unit(random) * .3));
        if (!append(state, value)) return;
    }
}

void frontend_fx_q2_color_explosion(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 origin, double seconds, uint32_t color, uint32_t run)
{
    for (unsigned i = 0; i < 128 && state->count < FRONTEND_FX_PARTICLE_CAPACITY; ++i) {
        frontend_fx_q2_particle value = particle(seconds); value.color = color + draw(random) % run;
        value.origin.x = origin.x + ((float)(draw(random) % 32) - 16);
        value.velocity.x = (float)(draw(random) % 256) - 128;
        value.origin.y = origin.y + ((float)(draw(random) % 32) - 16);
        value.velocity.y = (float)(draw(random) % 256) - 128;
        value.origin.z = origin.z + ((float)(draw(random) % 32) - 16);
        value.velocity.z = (float)(draw(random) % 256) - 128;
        value.acceleration.z = -40; value.alpha_velocity = (float)(-.4 / (.6 + unit(random) * .2)); append(state, value);
    }
}

void frontend_fx_q2_berserk(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 origin, qa_vec3 direction, double seconds)
{
    qa_vec3 right, up; basis(direction, &right, &up);
    for (unsigned i = 0; i < 700 && state->count < FRONTEND_FX_PARTICLE_CAPACITY; ++i) {
        frontend_fx_q2_particle value = particle(seconds); value.color = 110 + 2 * (draw(random) & 3);
        value.origin = origin; value.velocity = qa_vec_scale(direction, (float)(unit(random) * 192));
        value.velocity = qa_vec_add(value.velocity, qa_vec_scale(right, (float)(signed_unit(random) * 192)));
        value.velocity = qa_vec_add(value.velocity, qa_vec_scale(up, (float)(signed_unit(random) * 192)));
        value.alpha_velocity = (float)(-1 / (.5 + unit(random) * .3)); append(state, value);
    }
}

bool frontend_fx_q2_steam(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 origin, qa_vec3 direction, uint32_t color, int32_t count, float magnitude, double seconds, bool smoke)
{
    qa_vec3 right, up; basis(direction, &right, &up);
    for (int32_t i = 0; i < count; ++i) {
        if (state->count == FRONTEND_FX_PARTICLE_CAPACITY) return false;
        frontend_fx_q2_particle value = particle(seconds); value.color = color + (draw(random) & 7);
        value.origin = qa_vec_add(origin, signed_vector(random, (double)magnitude * .1));
        value.velocity = qa_vec_add(qa_vec_scale(direction, magnitude), qa_vec_scale(right, (float)(signed_unit(random) * magnitude / 3)));
        value.velocity = qa_vec_add(value.velocity, qa_vec_scale(up, (float)(signed_unit(random) * magnitude / 3)));
        value.acceleration.z = smoke ? 0 : -20; value.alpha_velocity = (float)(-1 / (.5 + unit(random) * .3)); append(state, value);
    }
    return true;
}

void frontend_fx_q2_force_wall(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 start, qa_vec3 end, uint32_t color, double seconds)
{
    qa_vec3 delta = qa_vec_sub(end, start), direction = qa_vec_normalize(delta); double length = qa_vec_length(delta);
    for (double distance = 0; distance < length && state->count < FRONTEND_FX_PARTICLE_CAPACITY; distance += 4) {
        if (unit(random) <= .3) continue;
        frontend_fx_q2_particle value = particle(seconds); value.alpha_velocity = (float)(-1 / (3 + unit(random) * .5));
        value.origin = qa_vec_add(qa_vec_add(start, qa_vec_scale(direction, (float)distance)), signed_vector(random, 3));
        value.velocity.z = (float)(-40 - signed_unit(random) * 10); value.color = color; append(state, value);
    }
}

void frontend_fx_q2_tracker_trail(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 start, qa_vec3 end, double seconds)
{
    (void)random;
    qa_vec3 delta = qa_vec_sub(end, start), direction = qa_vec_normalize(delta); double length = qa_vec_length(delta);
    double horizontal = hypot(direction.x, direction.y), pitch = -atan2(direction.z, horizontal), yaw = horizontal == 0 ? 0 : atan2(direction.y, direction.x);
    qa_vec3 forward = {(float)(cos(pitch) * cos(yaw)), (float)(cos(pitch) * sin(yaw)), (float)-sin(pitch)};
    qa_vec3 up = {(float)(sin(pitch) * cos(yaw)), (float)(sin(pitch) * sin(yaw)), (float)cos(pitch)};
    for (double distance = 0; distance < length && state->count < FRONTEND_FX_PARTICLE_CAPACITY; distance += 3) {
        qa_vec3 point = qa_vec_add(start, qa_vec_scale(direction, (float)distance)); frontend_fx_q2_particle value = particle(seconds);
        value.origin = qa_vec_add(point, qa_vec_scale(up, (float)(8 * cos(qa_vec_dot(point, forward)))));
        value.velocity.z = 5; value.alpha_velocity = -2; append(state, value);
    }
}

void frontend_fx_q2_tracker_shell(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 origin, double seconds)
{
    for (unsigned i = 0; i < 300 && state->count < FRONTEND_FX_PARTICLE_CAPACITY; ++i) {
        frontend_fx_q2_particle value = particle(seconds);
        value.origin = qa_vec_add(origin, qa_vec_scale(qa_vec_normalize(signed_vector(random, 1)), 40));
        value.alpha_velocity = -10000; append(state, value);
    }
}

void frontend_fx_q2_respawn_particles(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 origin, double seconds, frontend_fx_q2_respawn kind)
{
    bool logout = kind != FRONTEND_FX_Q2_ITEM;
    uint32_t base = kind == FRONTEND_FX_Q2_LOGIN ? 0xd0 : kind == FRONTEND_FX_Q2_LOGOUT ? 0x40 : kind == FRONTEND_FX_Q2_RESPAWN ? 0xe0 : 0xd4;
    for (unsigned i = 0; i < (logout ? 500u : 64u); ++i) {
        frontend_fx_q2_particle value = particle(seconds); value.color = base + (draw(random) & (logout ? 7u : 3u));
        if (logout) {
            value.origin.x = (float)(origin.x - 16 + unit(random) * 32);
            value.origin.y = (float)(origin.y - 16 + unit(random) * 32);
            value.origin.z = (float)(origin.z - 24 + unit(random) * 56);
        } else value.origin = qa_vec_add(origin, signed_vector(random, 8));
        value.velocity = signed_vector(random, logout ? 20 : 8); value.acceleration.z = logout ? -40 : -8;
        value.alpha_velocity = (float)(-1 / (1 + unit(random) * .3)); if (!append(state, value)) return;
    }
}

void frontend_fx_q2_teleport(frontend_fx_particles *state, qa_builtin_random *random, qa_vec3 origin, double seconds)
{
    for (int i = -16; i <= 16; i += 4) for (int j = -16; j <= 16; j += 4) for (int k = -16; k <= 32; k += 4) {
        frontend_fx_q2_particle value = particle(seconds); value.color = 7 + (draw(random) & 7);
        value.alpha_velocity = (float)(-1 / (.3 + (draw(random) & 7) * .02));
        value.origin.x = origin.x + ((float)i + (float)(draw(random) & 3));
        value.origin.y = origin.y + ((float)j + (float)(draw(random) & 3));
        value.origin.z = origin.z + ((float)k + (float)(draw(random) & 3));
        value.velocity = qa_vec_scale(qa_vec_normalize(qa_v3((float)(j * 8), (float)(i * 8), (float)(k * 8))), (float)(50 + (draw(random) & 63)));
        value.acceleration.z = -40; if (!append(state, value)) return;
    }
}

void frontend_fx_q2_big_teleport(frontend_fx_particles *state, qa_builtin_random *random, qa_vec3 origin, double seconds)
{
    static const uint32_t colors[4] = {16, 104, 168, 144};
    for (unsigned i = 0; i < 4096 && state->count < FRONTEND_FX_PARTICLE_CAPACITY; ++i) {
        frontend_fx_q2_particle value = particle(seconds); value.color = colors[draw(random) & 3];
        double angle = 6.28318530717958647693 * ((double)(draw(random) & 1023) / 1023), distance = draw(random) & 31;
        double x = cos(angle), y = sin(angle);
        value.velocity.x = (float)(x * (70 + (draw(random) & 63)));
        value.velocity.y = (float)(y * (70 + (draw(random) & 63)));
        value.origin.x = (float)(origin.x + x * distance); value.origin.y = (float)(origin.y + y * distance);
        value.origin.z = origin.z + 8 + (float)(draw(random) % 90); value.velocity.z = (float)(draw(random) & 31) - 100;
        value.acceleration = qa_v3((float)(-x * 100), (float)(-y * 100), 160);
        value.alpha_velocity = (float)(-.3 / (.5 + unit(random) * .3)); append(state, value);
    }
}

void frontend_fx_q2_teleporter(frontend_fx_particles *state, qa_builtin_random *random, qa_vec3 origin, double seconds)
{
    for (unsigned i = 0; i < 8 && state->count < FRONTEND_FX_PARTICLE_CAPACITY; ++i) {
        frontend_fx_q2_particle value = particle(seconds); value.color = 0xdb;
        value.origin.x = origin.x - 16 + (float)(draw(random) & 31); value.velocity.x = (float)(signed_unit(random) * 14);
        value.origin.y = origin.y - 16 + (float)(draw(random) & 31); value.velocity.y = (float)(signed_unit(random) * 14);
        value.origin.z = origin.z - 8 + (float)(draw(random) & 7); value.velocity.z = (float)(80 + (draw(random) & 7));
        value.acceleration.z = -40; value.alpha_velocity = -.5f; append(state, value);
    }
}

void frontend_fx_q2_blaster_trail(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 start, qa_vec3 end, double seconds, bool green)
{
    qa_vec3 delta = qa_vec_sub(end, start), direction = qa_vec_normalize(delta); double length = qa_vec_length(delta);
    for (double distance = 0; distance < length && state->count < FRONTEND_FX_PARTICLE_CAPACITY; distance += 5) {
        frontend_fx_q2_particle value = particle(seconds); value.alpha_velocity = (float)(-1 / (.3 + unit(random) * .2));
        spread(&value, random, qa_vec_add(start, qa_vec_scale(direction, (float)distance)), 1, 5);
        value.color = green ? 0xd0 : 0xe0; append(state, value);
    }
}

int32_t frontend_fx_q2_diminishing_trail(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 start, qa_vec3 end, double seconds, int32_t count, frontend_fx_q2_trail kind)
{
    qa_vec3 delta = qa_vec_sub(end, start), direction = qa_vec_normalize(delta); double length = qa_vec_length(delta);
    double origin_scale = count > 900 ? 4 : count > 800 ? 2 : 1, velocity_scale = count > 900 ? 15 : count > 800 ? 10 : 5;
    bool blood = kind == FRONTEND_FX_Q2_BLOOD || kind == FRONTEND_FX_Q2_GREEN_BLOOD;
    for (double distance = 0; distance < length && state->count < FRONTEND_FX_PARTICLE_CAPACITY; distance += .5) {
        if ((int32_t)(draw(random) & 1023) < count) {
            frontend_fx_q2_particle value = particle(seconds); value.alpha_velocity = (float)(-1 / (1 + unit(random) * (blood ? .4 : .2)));
            value.color = (blood ? kind == FRONTEND_FX_Q2_BLOOD ? 0xe8u : 0xdbu : 4u) + (draw(random) & 7);
            spread(&value, random, qa_vec_add(start, qa_vec_scale(direction, (float)distance)), origin_scale, velocity_scale);
            if (blood) value.velocity.z -= 40; else value.acceleration.z = 20;
            append(state, value);
        }
        count = count > 105 ? count - 5 : 100;
    }
    if (kind == FRONTEND_FX_Q2_ROCKET) for (double distance = 0; distance < length && state->count < FRONTEND_FX_PARTICLE_CAPACITY; ++distance) {
        if (draw(random) & 7) continue;
        frontend_fx_q2_particle value = particle(seconds); value.alpha_velocity = (float)(-1 / (1 + unit(random) * .2));
        value.color = 0xdc + (draw(random) & 3); spread(&value, random, qa_vec_add(start, qa_vec_scale(direction, (float)distance)), 5, 20);
        value.acceleration.z = -40; append(state, value);
    }
    return count;
}

void frontend_fx_q2_rail(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 start, qa_vec3 end, double seconds)
{
    qa_vec3 delta = qa_vec_sub(end, start), direction = qa_vec_normalize(delta), right, up;
    double length = qa_vec_length(delta); basis(direction, &right, &up);
    for (double i = 0; i < length; ++i) {
        qa_vec3 radial = qa_vec_add(qa_vec_scale(right, (float)cos(i * .1)), qa_vec_scale(up, (float)sin(i * .1)));
        frontend_fx_q2_particle value = particle(seconds); value.alpha_velocity = (float)(-1 / (1 + unit(random) * .2));
        value.color = 0x74 + (draw(random) & 7); value.origin = qa_vec_add(qa_vec_add(start, qa_vec_scale(direction, (float)i)), qa_vec_scale(radial, 3));
        value.velocity = qa_vec_scale(radial, 6); if (!append(state, value)) return;
    }
    for (double i = 0; i < length; i += .75) {
        frontend_fx_q2_particle value = particle(seconds); value.alpha_velocity = (float)(-1 / (.6 + unit(random) * .2));
        value.color = draw(random) & 15; spread(&value, random, qa_vec_add(start, qa_vec_scale(direction, (float)i)), 3, 3);
        if (!append(state, value)) return;
    }
}

void frontend_fx_q2_bubbles(frontend_fx_particles *state, qa_builtin_random *random,
    qa_vec3 start, qa_vec3 end, double seconds)
{
    qa_vec3 delta = qa_vec_sub(end, start), direction = qa_vec_normalize(delta); double length = qa_vec_length(delta);
    for (double i = 0; i < length; i += 32) {
        frontend_fx_q2_particle value = particle(seconds); value.alpha_velocity = (float)(-1 / (1 + unit(random) * .2));
        value.color = 4 + (draw(random) & 7); spread(&value, random, qa_vec_add(start, qa_vec_scale(direction, (float)i)), 2, 5);
        value.velocity.z += 6; if (!append(state, value)) return;
    }
}

bool frontend_fx_q2_sample(const frontend_fx_q2_particle *value, double milliseconds, qa_vec3 *origin, float *alpha)
{
    double seconds = value->alpha_velocity == -10000 ? 0 : (milliseconds - value->spawn_milliseconds) * .001;
    double amount = value->alpha + seconds * value->alpha_velocity;
    if (amount <= 0) return false;
    double square = seconds * seconds;
    *origin = qa_v3((float)(value->origin.x + value->velocity.x * seconds + value->acceleration.x * square),
        (float)(value->origin.y + value->velocity.y * seconds + value->acceleration.y * square),
        (float)(value->origin.z + value->velocity.z * seconds + value->acceleration.z * square));
    *alpha = (float)fmin(1, amount); return true;
}
