#include "../internal.h"
#include <limits.h>

enum {
    Q2_NORMAL, Q2_SPECTATOR, Q2_DEAD, Q2_GIB, Q2_FREEZE
};
enum {
    Q2_DUCKED = 1u, Q2_JUMP_HELD = 2u, Q2_ON_GROUND = 4u,
    Q2_TIME_WATERJUMP = 8u, Q2_TIME_LAND = 16u, Q2_TIME_TELEPORT = 32u,
    Q2_TIME_FLAGS = Q2_TIME_WATERJUMP | Q2_TIME_LAND | Q2_TIME_TELEPORT,
    Q2_SOLID = 1u, Q2_SLIME = 16u, Q2_WATER = 32u,
    Q2_MASK_WATER = 8u | Q2_SLIME | Q2_WATER,
    Q2_CURRENT_0 = 1u << 18, Q2_CURRENT_90 = 1u << 19,
    Q2_CURRENT_180 = 1u << 20, Q2_CURRENT_270 = 1u << 21,
    Q2_CURRENT_UP = 1u << 22, Q2_CURRENT_DOWN = 1u << 23,
    Q2_MASK_CURRENT = Q2_CURRENT_0 | Q2_CURRENT_90 | Q2_CURRENT_180 |
                      Q2_CURRENT_270 | Q2_CURRENT_UP | Q2_CURRENT_DOWN,
    Q2_LADDER = 1u << 29,
    Q2_MASK_PLAYER = 1u | 2u | (1u << 16) | (1u << 25),
    Q2_SLICK = 2u,
    Q2_MAX_TOUCH = 32
};

typedef struct q2_classic_move {
    qa_move_context *context;
    qa_q2_movement_state *state;
    qa_movement_command command;
    qa_vec3 origin, velocity, forward, right, up;
    qa_vec3 angles;
    qa_bounds character, bounds;
    qa_movement_ground ground;
    qa_trace_result ground_trace;
    int32_t previous_origin[3];
    float dt, speed_multiplier, view_height;
    int32_t water_level, water_type;
    unsigned raw_contacts;
    bool ladder;
} q2_classic_move;

static uint32_t q2_time(const qa_q2_movement_state *state);
static void q2_time_set(qa_q2_movement_state *state, uint32_t value);
static uint32_t q2_time_shift(const qa_q2_movement_state *state);

static qa_vec3 q2_advance(qa_vec3 origin, float time, qa_vec3 velocity)
{
    return qa_vec_add(origin, qa_vec_scale(velocity, time));
}

static float q2_normalize(qa_vec3 *v)
{
    float length = qa_vec_length(*v);
    if (length != 0.0f) *v = qa_vec_scale(*v, 1.0f / length);
    return length;
}

static bool q2_grounded(const q2_classic_move *pm)
{
    return pm->ground.hit != QA_TRACE_HIT_NONE;
}

static float q2_height(const q2_classic_move *pm, float source)
{
    return pm->character.mins.z + ((source + 24.0f) / 56.0f) *
        (pm->character.maxs.z - pm->character.mins.z);
}

static bool q2_trace_bounds(q2_classic_move *pm, qa_vec3 start, qa_vec3 end,
                            qa_bounds bounds, qa_trace_result *trace)
{
    uint32_t mask = Q2_MASK_PLAYER;
    if (pm->state->wide_coordinates) {
        if (pm->state->type == Q2_DEAD || pm->state->type == Q2_GIB)
            mask &= ~(UINT32_C(1) << 25);
        if (!(pm->state->flags & (UINT32_C(1) << 7))) mask |= UINT32_C(1) << 30;
    }
    return qa_move_trace(pm->context, start, end, bounds, mask,
                         false, trace);
}

static bool q2_trace(q2_classic_move *pm, qa_vec3 start, qa_vec3 end,
                     qa_trace_result *trace)
{
    return q2_trace_bounds(pm, start, end, pm->bounds, trace);
}

static bool q2_contact(q2_classic_move *pm, const qa_trace_result *trace)
{
    if (trace->hit == QA_TRACE_HIT_NONE || pm->raw_contacts == Q2_MAX_TOUCH)
        return true;
    ++pm->raw_contacts;
    qa_trace_result contact = *trace;
    if (contact.contact) contact.contact_plane = contact.plane;
    return qa_move_contact(pm->context, &contact, false, true);
}

static bool q2_slide(q2_classic_move *pm)
{
    qa_vec3 primal = pm->velocity;
    qa_vec3 planes[5];
    unsigned count = 0;
    float remaining = pm->dt;

    for (unsigned bump = 0; bump < 4; ++bump) {
        qa_trace_result trace;
        if (!q2_trace(pm, pm->origin, q2_advance(pm->origin, remaining, pm->velocity), &trace))
            return false;
        if (trace.all_solid) {
            pm->velocity.z = 0;
            return true;
        }
        if (trace.fraction > 0.0f) {
            pm->origin = trace.end;
            count = 0;
        }
        if (trace.fraction == 1.0f) break;
        if (!q2_contact(pm, &trace)) return false;
        remaining -= remaining * trace.fraction;
        if (count == 5) {
            pm->velocity = qa_v3(0, 0, 0);
            break;
        }
        planes[count++] = trace.plane.normal;
        qa_vec3 candidate = pm->velocity;
        bool accepted = false;
        for (unsigned i = 0; i < count; ++i) {
            candidate = qa_move_clip(candidate, planes[i], 1.01f, 0.1f);
            unsigned j;
            for (j = 0; j < count; ++j) {
                if (i != j && qa_vec_dot(candidate, planes[j]) < 0.0f) break;
            }
            if (j == count) {
                accepted = true;
                break;
            }
        }
        if (!accepted) {
            if (count != 2) {
                pm->velocity = qa_v3(0, 0, 0);
                break;
            }
            qa_vec3 direction = qa_vec_cross(planes[0], planes[1]);
            candidate = qa_vec_scale(direction, qa_vec_dot(direction, candidate));
        }
        if (qa_vec_dot(candidate, primal) <= 0.0f) {
            pm->velocity = qa_v3(0, 0, 0);
            break;
        }
        pm->velocity = candidate;
    }
    if (q2_time(pm->state) != 0) pm->velocity = primal;
    return true;
}

static bool q2_step_slide(q2_classic_move *pm)
{
    qa_vec3 start_origin = pm->origin, start_velocity = pm->velocity;
    if (!q2_slide(pm)) return false;
    qa_vec3 down_origin = pm->origin, down_velocity = pm->velocity;
    qa_vec3 up = start_origin;
    up.z += 18.0f;
    qa_trace_result trace;
    if (!q2_trace(pm, up, up, &trace)) return false;
    if (trace.all_solid) return true;
    pm->origin = up;
    pm->velocity = start_velocity;
    if (!q2_slide(pm)) return false;
    qa_vec3 down = pm->origin;
    down.z -= 18.0f;
    if (!q2_trace(pm, pm->origin, down, &trace)) return false;
    if (!trace.all_solid) pm->origin = trace.end;
    qa_vec3 down_delta = qa_vec_sub(down_origin, start_origin);
    qa_vec3 up_delta = qa_vec_sub(pm->origin, start_origin);
    float down_distance = down_delta.x * down_delta.x + down_delta.y * down_delta.y;
    float up_distance = up_delta.x * up_delta.x + up_delta.y * up_delta.y;
    if (down_distance > up_distance || trace.plane.normal.z < 0.7f) {
        pm->origin = down_origin;
        pm->velocity = down_velocity;
    } else {
        pm->velocity.z = down_velocity.z;
    }
    return true;
}

static void q2_friction(q2_classic_move *pm)
{
    float speed = qa_vec_length(pm->velocity);
    if (speed < 1.0f) {
        pm->velocity.x = pm->velocity.y = 0;
        return;
    }
    float drop = 0;
    if ((q2_grounded(pm) && pm->ground_trace.has_surface &&
         !(pm->ground_trace.surface.flags & Q2_SLICK)) || pm->ladder)
        drop += fmaxf(speed, 100.0f) * 6.0f * pm->dt;
    if (pm->water_level && !pm->ladder)
        drop += speed * (float)pm->water_level * pm->dt;
    pm->velocity = qa_vec_scale(pm->velocity, fmaxf(speed - drop, 0.0f) / speed);
}

static void q2_accelerate(q2_classic_move *pm, qa_vec3 direction,
                          float speed, float acceleration, bool air)
{
    float capped = air ? fminf(speed, 30.0f) : speed;
    float add = capped - qa_vec_dot(pm->velocity, direction);
    if (add <= 0.0f) return;
    float amount = air ? acceleration * speed * pm->dt : acceleration * pm->dt * speed;
    pm->velocity = q2_advance(pm->velocity, fminf(amount, add), direction);
}

static qa_vec3 q2_current(int32_t contents)
{
    return qa_v3(((contents & Q2_CURRENT_0) ? 1.0f : 0.0f) -
                 ((contents & Q2_CURRENT_180) ? 1.0f : 0.0f),
                 ((contents & Q2_CURRENT_90) ? 1.0f : 0.0f) -
                 ((contents & Q2_CURRENT_270) ? 1.0f : 0.0f),
                 ((contents & Q2_CURRENT_UP) ? 1.0f : 0.0f) -
                 ((contents & Q2_CURRENT_DOWN) ? 1.0f : 0.0f));
}

static qa_vec3 q2_add_currents(q2_classic_move *pm, qa_vec3 wish)
{
    if (pm->ladder && fabsf(pm->velocity.z) <= 200.0f) {
        if (pm->angles.x <= -15.0f && pm->command.forward_move > 0) wish.z = 200;
        else if (pm->angles.x >= 15.0f && pm->command.forward_move > 0) wish.z = -200;
        else if (pm->command.up_move > 0) wish.z = 200;
        else if (pm->command.up_move < 0) wish.z = -200;
        else wish.z = 0;
        wish.x = fmaxf(-25.0f, fminf(wish.x, 25.0f));
        wish.y = fmaxf(-25.0f, fminf(wish.y, 25.0f));
    }
    if (pm->water_type & Q2_MASK_CURRENT) {
        float water_speed = pm->water_level == 1 && q2_grounded(pm) ? 200.0f : 400.0f;
        wish = q2_advance(wish, water_speed, q2_current(pm->water_type));
    }
    if (q2_grounded(pm)) wish = q2_advance(wish, 100.0f, q2_current(pm->ground_trace.contents));
    return wish;
}

static qa_vec3 q2_wish_velocity(const q2_classic_move *pm)
{
    return qa_vec_add(qa_vec_scale(pm->forward, (float)pm->command.forward_move * pm->speed_multiplier),
                       qa_vec_scale(pm->right, (float)pm->command.side_move * pm->speed_multiplier));
}

static bool q2_water_move(q2_classic_move *pm)
{
    qa_vec3 wish = q2_wish_velocity(pm);
    if (pm->command.forward_move == 0 && pm->command.side_move == 0 && pm->command.up_move == 0)
        wish.z -= 60.0f;
    else
        wish.z += (float)pm->command.up_move * pm->speed_multiplier;
    wish = q2_add_currents(pm, wish);
    float speed = fminf(q2_normalize(&wish), 300.0f * pm->speed_multiplier) * 0.5f;
    q2_accelerate(pm, wish, speed, 10.0f, false);
    return q2_step_slide(pm);
}

static bool q2_air_move(q2_classic_move *pm)
{
    qa_vec3 wish = q2_wish_velocity(pm);
    wish.z = 0;
    wish = q2_add_currents(pm, wish);
    float wish_z = wish.z;
    float max_speed = ((pm->state->flags & Q2_DUCKED) ? 100.0f : 300.0f) * pm->speed_multiplier;
    float speed = q2_normalize(&wish);
    if (speed > max_speed) {
        wish_z *= max_speed / speed;
        speed = max_speed;
    }
    float gravity_step = (float)pm->state->gravity * pm->dt;
    if (pm->ladder) {
        q2_accelerate(pm, wish, speed, 10.0f, false);
        if (wish_z == 0.0f) {
            if (pm->velocity.z > 0.0f) pm->velocity.z = fmaxf(pm->velocity.z - gravity_step, 0.0f);
            else pm->velocity.z = fminf(pm->velocity.z + gravity_step, 0.0f);
        }
    } else if (q2_grounded(pm)) {
        pm->velocity.z = 0;
        q2_accelerate(pm, wish, speed, 10.0f, false);
        if (pm->state->gravity > 0) pm->velocity.z = 0;
        else pm->velocity.z -= gravity_step;
        if (pm->velocity.x == 0.0f && pm->velocity.y == 0.0f) return true;
    } else {
        bool air_acceleration = pm->context->input->profile.data.q2.air_accelerate != 0.0f;
        q2_accelerate(pm, wish, speed, air_acceleration ? 10.0f : 1.0f, air_acceleration);
        pm->velocity.z -= gravity_step;
    }
    return q2_step_slide(pm);
}

static bool q2_categorize(q2_classic_move *pm)
{
    qa_vec3 point = pm->origin;
    point.z -= 0.25f;
    if (pm->velocity.z > 180.0f) {
        pm->state->flags &= ~(uint32_t)Q2_ON_GROUND;
        pm->ground = (qa_movement_ground){0};
    } else {
        qa_trace_result trace;
        if (!q2_trace(pm, pm->origin, point, &trace)) return false;
        pm->ground_trace = trace;
        if (trace.hit == QA_TRACE_HIT_NONE || (trace.plane.normal.z < 0.7f && !trace.start_solid)) {
            pm->ground = (qa_movement_ground){0};
            pm->state->flags &= ~(uint32_t)Q2_ON_GROUND;
        } else {
            pm->ground = qa_move_ground(&trace);
            if (pm->state->flags & Q2_TIME_WATERJUMP) {
                pm->state->flags &= ~(uint32_t)Q2_TIME_FLAGS;
                q2_time_set(pm->state, 0);
            }
            if (!(pm->state->flags & Q2_ON_GROUND)) {
                pm->state->flags |= Q2_ON_GROUND;
                if (pm->velocity.z < -200.0f && !pm->context->input->profile.data.q2.strafejump_hack) {
                    pm->state->flags |= Q2_TIME_LAND;
                    q2_time_set(pm->state, (pm->velocity.z < -400.0f ? 200u : 144u) >> q2_time_shift(pm->state));
                }
            }
        }
        if (!q2_contact(pm, &trace)) return false;
    }
    pm->water_level = pm->water_type = 0;
    float sample2 = truncf(pm->view_height - pm->bounds.mins.z);
    float sample1 = truncf(sample2 * 0.5f);
    point.z = pm->origin.z + pm->bounds.mins.z + 1.0f;
    int32_t contents;
    if (!qa_move_contents(pm->context, point, &contents)) return false;
    if (!(contents & Q2_MASK_WATER)) return true;
    pm->water_type = contents;
    pm->water_level = 1;
    point.z = pm->origin.z + pm->bounds.mins.z + sample1;
    if (!qa_move_contents(pm->context, point, &contents)) return false;
    if (!(contents & Q2_MASK_WATER)) return true;
    pm->water_level = 2;
    point.z = pm->origin.z + pm->bounds.mins.z + sample2;
    if (!qa_move_contents(pm->context, point, &contents)) return false;
    if (contents & Q2_MASK_WATER) pm->water_level = 3;
    return true;
}

static void q2_check_jump(q2_classic_move *pm)
{
    if (pm->state->flags & Q2_TIME_LAND) return;
    if (pm->command.up_move < 10) {
        pm->state->flags &= ~(uint32_t)Q2_JUMP_HELD;
        return;
    }
    if ((pm->state->flags & Q2_JUMP_HELD) || pm->state->type == Q2_DEAD) return;
    if (pm->water_level >= 2) {
        pm->ground = (qa_movement_ground){0};
        if (pm->velocity.z <= -300.0f) return;
        pm->velocity.z = pm->water_type == Q2_WATER ? 100.0f : pm->water_type == Q2_SLIME ? 80.0f : 50.0f;
        return;
    }
    if (!q2_grounded(pm)) return;
    pm->state->flags |= Q2_JUMP_HELD;
    pm->ground = (qa_movement_ground){0};
    pm->velocity.z = fmaxf(pm->velocity.z + 270.0f, 270.0f);
}

static bool q2_check_special(q2_classic_move *pm)
{
    if (q2_time(pm->state) != 0) return true;
    pm->ladder = false;
    qa_vec3 flat = qa_v3(pm->forward.x, pm->forward.y, 0);
    q2_normalize(&flat);
    qa_trace_result trace;
    if (!q2_trace(pm, pm->origin, qa_vec_add(pm->origin, flat), &trace)) return false;
    pm->ladder = trace.fraction < 1.0f && (trace.contents & Q2_LADDER) != 0;
    if (pm->water_level != 2) return true;
    qa_vec3 spot = q2_advance(pm->origin, 30.0f, flat);
    spot.z += 4.0f;
    int32_t contents;
    if (!qa_move_contents(pm->context, spot, &contents)) return false;
    if (!(contents & Q2_SOLID)) return true;
    spot.z += 16.0f;
    if (!qa_move_contents(pm->context, spot, &contents)) return false;
    if (contents != 0) return true;
    pm->velocity = qa_vec_scale(flat, 50.0f);
    pm->velocity.z = 350.0f;
    pm->state->flags |= Q2_TIME_WATERJUMP;
    q2_time_set(pm->state, 2040u >> q2_time_shift(pm->state));
    return true;
}

static bool q2_fly_move(q2_classic_move *pm, bool collide)
{
    pm->view_height = q2_height(pm, 22.0f);
    float speed = qa_vec_length(pm->velocity);
    if (speed < 1.0f) pm->velocity = qa_v3(0, 0, 0);
    else {
        float drop = fmaxf(speed, 100.0f) * 9.0f * pm->dt;
        pm->velocity = qa_vec_scale(pm->velocity, fmaxf(speed - drop, 0.0f) / speed);
    }
    q2_normalize(&pm->forward);
    q2_normalize(&pm->right);
    qa_vec3 wish = q2_wish_velocity(pm);
    wish.z += (float)pm->command.up_move * pm->speed_multiplier;
    float wish_speed = fminf(q2_normalize(&wish), 300.0f * pm->speed_multiplier);
    float add = wish_speed - qa_vec_dot(pm->velocity, wish);
    if (add <= 0.0f && !collide) return true;
    float acceleration = fmaxf(0.0f, fminf(10.0f * pm->dt * wish_speed, add));
    pm->velocity = q2_advance(pm->velocity, acceleration, wish);
    if (collide) return q2_step_slide(pm);
    pm->origin = q2_advance(pm->origin, pm->dt, pm->velocity);
    return true;
}

static bool q2_expands(qa_bounds previous, qa_bounds requested)
{
    return requested.mins.x < previous.mins.x || requested.mins.y < previous.mins.y ||
        requested.mins.z < previous.mins.z || requested.maxs.x > previous.maxs.x ||
        requested.maxs.y > previous.maxs.y || requested.maxs.z > previous.maxs.z;
}

static bool q2_check_duck(q2_classic_move *pm)
{
    const qa_movement_input *input = pm->context->input;
    uint32_t previous_duck = pm->state->flags & Q2_DUCKED;
    pm->bounds = pm->character;
    if (pm->state->type == Q2_GIB) {
        pm->bounds.mins.z = q2_height(pm, 0);
        pm->bounds.maxs.z = q2_height(pm, 16);
        pm->view_height = q2_height(pm, 8);
        return true;
    }
    pm->bounds.mins.z = q2_height(pm, -24);
    if (pm->state->type == Q2_DEAD ||
        (pm->command.up_move < 0 && (pm->state->flags & Q2_ON_GROUND))) {
        pm->state->flags |= Q2_DUCKED;
    } else if (pm->state->flags & Q2_DUCKED) {
        pm->bounds.maxs.z = q2_height(pm, 32);
        qa_trace_result trace;
        if (!q2_trace_bounds(pm, pm->origin, pm->origin, pm->bounds, &trace)) return false;
        if (!trace.all_solid) pm->state->flags &= ~(uint32_t)Q2_DUCKED;
    }
    bool ducked = (pm->state->flags & Q2_DUCKED) != 0;
    pm->bounds.maxs.z = q2_height(pm, ducked ? 4.0f : 32.0f);
    pm->view_height = q2_height(pm, ducked ? -2.0f : 22.0f);
    qa_bounds previous = input->has_current_bounds ? input->current_bounds : pm->character;
    if (q2_expands(previous, pm->bounds)) {
        qa_trace_result trace;
        if (!q2_trace_bounds(pm, pm->origin, pm->origin, pm->bounds, &trace)) return false;
        if (trace.all_solid) {
            pm->bounds = previous;
            pm->state->flags = (pm->state->flags & ~(uint32_t)Q2_DUCKED) | previous_duck;
            pm->view_height = q2_height(pm, previous_duck ? -2.0f : 22.0f);
            return true;
        }
    }
    return true;
}

static qa_vec3 q2_unpack(const qa_q2_movement_state *state, bool velocity)
{
    return qa_v3((float)qa_q2_movement_coordinate(state, velocity, 0) * 0.125f,
                 (float)qa_q2_movement_coordinate(state, velocity, 1) * 0.125f,
                 (float)qa_q2_movement_coordinate(state, velocity, 2) * 0.125f);
}

static uint32_t q2_time(const qa_q2_movement_state *state)
{ return state->wide_coordinates ? state->wide.time_ms : state->time_eight_ms; }

static void q2_time_set(qa_q2_movement_state *state, uint32_t value)
{
    if (state->wide_coordinates) state->wide.time_ms = (uint16_t)value;
    else state->time_eight_ms = (uint8_t)value;
}

static uint32_t q2_time_shift(const qa_q2_movement_state *state)
{ return state->wide_coordinates ? 0u : 3u; }

static bool q2_good_position(q2_classic_move *pm, bool *good)
{
    if (pm->state->type == Q2_SPECTATOR) {
        *good = true;
        return true;
    }
    qa_vec3 origin = q2_unpack(pm->state, false);
    qa_trace_result trace;
    if (!q2_trace(pm, origin, origin, &trace)) return false;
    *good = !trace.all_solid;
    return true;
}

static int32_t q2_offset(int32_t base, int offset)
{
    uint32_t word = (uint32_t)base + (uint32_t)offset;
    return word <= INT32_MAX ? (int32_t)word : -1 - (int32_t)(UINT32_MAX - word);
}

static bool q2_snap(q2_classic_move *pm)
{
    static const unsigned jitter[8] = {0, 4, 1, 2, 3, 5, 6, 7};
    int sign[3];
    int32_t base[3];
    for (unsigned i = 0; i < 3; ++i) {
        float origin = qa_move_component(pm->origin, i);
        qa_q2_movement_coordinate_set(pm->state, true, i, qa_move_q2_coordinate_word(pm->state, qa_move_component(pm->velocity, i) * 8.0f));
        base[i] = qa_move_q2_coordinate_word(pm->state, origin * 8.0f);
        sign[i] = (float)base[i] * 0.125f == origin ? 0 : origin >= 0.0f ? 1 : -1;
    }
    for (unsigned j = 0; j < 8; ++j) {
        for (unsigned i = 0; i < 3; ++i)
            qa_q2_movement_coordinate_set(pm->state, false, i, q2_offset(base[i], (jitter[j] & (1u << i)) ? sign[i] : 0));
        bool good;
        if (!q2_good_position(pm, &good)) return false;
        if (good) return true;
    }
    for (unsigned i = 0; i < 3; ++i)
        qa_q2_movement_coordinate_set(pm->state, false, i, pm->previous_origin[i]);
    return true;
}

static bool q2_initial_snap(q2_classic_move *pm)
{
    static const int offset[3] = {0, -1, 1};
    int32_t base[3];
    for (unsigned i = 0; i < 3; ++i) base[i] = qa_q2_movement_coordinate(pm->state, false, i);
    for (unsigned z = 0; z < 3; ++z) {
        qa_q2_movement_coordinate_set(pm->state, false, 2, q2_offset(base[2], offset[z]));
        for (unsigned y = 0; y < 3; ++y) {
            qa_q2_movement_coordinate_set(pm->state, false, 1, q2_offset(base[1], offset[y]));
            for (unsigned x = 0; x < 3; ++x) {
                qa_q2_movement_coordinate_set(pm->state, false, 0, q2_offset(base[0], offset[x]));
                bool good;
                if (!q2_good_position(pm, &good)) return false;
                if (good) {
                    pm->origin = q2_unpack(pm->state, false);
                    for (unsigned i = 0; i < 3; ++i)
                        pm->previous_origin[i] = qa_q2_movement_coordinate(pm->state, false, i);
                    return true;
                }
            }
        }
    }
    return true;
}

static void q2_clamp_angles(q2_classic_move *pm)
{
    if (pm->state->flags & Q2_TIME_TELEPORT) {
        pm->angles = qa_v3(0, ((float)pm->command.angle_words[1] + (float)pm->state->delta_angle_shorts[1]) *
                          (360.0f / 65536.0f), 0);
    } else {
        for (unsigned i = 0; i < 3; ++i) {
            uint32_t word = (uint32_t)pm->command.angle_words[i] + (uint32_t)(int32_t)pm->state->delta_angle_shorts[i];
            qa_move_set_component(&pm->angles, i, (float)qa_move_short((int32_t)(word & 65535u)) * (360.0f / 65536.0f));
        }
        if (pm->angles.x > 89.0f && pm->angles.x < 180.0f) pm->angles.x = 89.0f;
        else if (pm->angles.x < 271.0f && pm->angles.x >= 180.0f) pm->angles.x = 271.0f;
    }
    qa_move_angles(pm->angles, &pm->forward, &pm->right, &pm->up);
}

static bool q2_run(q2_classic_move *pm)
{
    q2_clamp_angles(pm);
    if (pm->context->input->environment.flight && pm->context->input->environment.health > 0 &&
        pm->state->type == Q2_NORMAL) {
        pm->state->flags &= ~(uint32_t)(Q2_ON_GROUND | Q2_DUCKED | Q2_TIME_WATERJUMP);
        q2_time_set(pm->state, 0);
        return q2_fly_move(pm, true) && q2_snap(pm);
    }
    if (pm->state->type == Q2_SPECTATOR) return q2_fly_move(pm, false) && q2_snap(pm);
    if (pm->state->type >= Q2_DEAD)
        pm->command.forward_move = pm->command.side_move = pm->command.up_move = 0;
    if (pm->state->type == Q2_FREEZE) return true;
    if (!q2_check_duck(pm)) return false;
    if (pm->context->input->profile.data.q2.snap_initial && !q2_initial_snap(pm)) return false;
    if (!q2_categorize(pm)) return false;
    if (pm->state->type == Q2_DEAD && q2_grounded(pm)) {
        float speed = qa_vec_length(pm->velocity);
        pm->velocity = speed <= 20.0f ? qa_v3(0, 0, 0) : qa_vec_scale(qa_vec_normalize(pm->velocity), speed - 20.0f);
    }
    if (!q2_check_special(pm)) return false;
    if (q2_time(pm->state) != 0) {
        uint32_t elapsed = pm->command.milliseconds >> q2_time_shift(pm->state);
        if (elapsed == 0) elapsed = 1;
        if (elapsed >= q2_time(pm->state)) {
            pm->state->flags &= ~(uint32_t)Q2_TIME_FLAGS;
            q2_time_set(pm->state, 0);
        } else {
            q2_time_set(pm->state, q2_time(pm->state) - elapsed);
        }
    }
    if (pm->state->flags & Q2_TIME_TELEPORT) {
        /* The source teleport pause still categorizes and snaps below. */
    } else if (pm->state->flags & Q2_TIME_WATERJUMP) {
        pm->velocity.z -= (float)pm->state->gravity * pm->dt;
        if (pm->velocity.z < 0.0f) {
            pm->state->flags &= ~(uint32_t)Q2_TIME_FLAGS;
            q2_time_set(pm->state, 0);
        }
        if (!q2_step_slide(pm)) return false;
    } else {
        q2_check_jump(pm);
        q2_friction(pm);
        if (pm->water_level >= 2) {
            if (!q2_water_move(pm)) return false;
        } else {
            qa_vec3 angles = pm->angles;
            if (angles.x > 180.0f) angles.x -= 360.0f;
            angles.x /= 3.0f;
            qa_move_angles(angles, &pm->forward, &pm->right, &pm->up);
            if (!q2_air_move(pm)) return false;
        }
    }
    if (!q2_categorize(pm) || !q2_snap(pm)) return false;
    if (pm->state->wide_coordinates) {
        if (pm->ladder) pm->state->flags |= UINT32_C(1) << 8;
        else pm->state->flags &= ~(UINT32_C(1) << 8);
    }
    return true;
}

bool qa_move_q2(qa_move_context *context)
{
    const qa_movement_input *input = context->input;
    const qa_movement_environment *environment = &input->environment;
    qa_q2_movement_state *state = &context->state->data.q2;
    int32_t original_type = state->type;
    bool mode_override = environment->has_mode && environment->health > 0;
    if (mode_override) state->type = qa_move_mode_type(QA_MOVEMENT_Q2_CLASSIC, environment->mode);
    if (environment->fixed_pose) {
        state->type = Q2_FREEZE;
        for (unsigned i = 0; i < 3; ++i) qa_q2_movement_coordinate_set(state, true, i, 0);
    }
    q2_classic_move pm = {0};
    pm.context = context;
    pm.state = state;
    pm.command = context->command;
    pm.dt = (float)context->command.milliseconds * 0.001f;
    pm.speed_multiplier = environment->speed_multiplier;
    pm.character = environment->fixed_pose ? environment->pose.bounds :
        input->shape.kind == QA_SHAPE_POINT ? (qa_bounds){0} : input->shape.bounds;
    pm.bounds = pm.character;
    pm.origin = q2_unpack(state, false);
    pm.velocity = q2_unpack(state, true);
    for (unsigned i = 0; i < 3; ++i) pm.previous_origin[i] = qa_q2_movement_coordinate(state, false, i);
    bool success = q2_run(&pm);
    if (environment->fixed_pose) {
        pm.view_height = environment->pose.view_height;
        state->flags = environment->fixed_crouched ? state->flags | Q2_DUCKED : state->flags & ~(uint32_t)Q2_DUCKED;
    }
    if (mode_override || environment->fixed_pose) state->type = original_type;
    if (!success) return false;
    qa_movement_result *result = context->result;
    result->bounds = pm.bounds;
    result->view_angles = pm.angles;
    result->view_height = pm.view_height;
    result->ground = pm.ground;
    result->water_level = pm.water_level;
    result->water_type = pm.water_type;
    qa_vec3 velocity = q2_unpack(state, true);
    result->horizontal_speed = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
    return true;
}
