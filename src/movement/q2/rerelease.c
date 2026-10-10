/* Quake II rerelease movement, derived from the Anthology movement provider. */
#include "../internal.h"
#include <math.h>

enum {
    RR_NORMAL, RR_GRAPPLE, RR_NOCLIP, RR_SPECTATOR, RR_DEAD, RR_GIB, RR_FREEZE
};
enum {
    RR_DUCKED = 1, RR_JUMP_HELD = 2, RR_ON_GROUND = 4,
    RR_WATERJUMP = 8, RR_LAND = 16, RR_TELEPORT = 32,
    RR_LADDER = 128, RR_IGNORE_PLAYERS = 512, RR_TRICK = 1024,
    RR_TIMERS = RR_WATERJUMP | RR_LAND | RR_TELEPORT | RR_TRICK,
    RR_JUMP_BUTTON = 8, RR_CROUCH_BUTTON = 16,
    RR_SOLID = 1, RR_WINDOW = 2, RR_LAVA = 8, RR_SLIME = 16, RR_WATER = 32,
    RR_NO_WATERJUMP = 1 << 13, RR_PLAYERCLIP = 1 << 16,
    RR_CURRENT_0 = 1 << 18, RR_CURRENT_90 = 1 << 19,
    RR_CURRENT_180 = 1 << 20, RR_CURRENT_270 = 1 << 21,
    RR_CURRENT_UP = 1 << 22, RR_CURRENT_DOWN = 1 << 23,
    RR_MONSTER = 1 << 25, RR_CONTENTS_LADDER = 1 << 29, RR_PLAYER = 1 << 30,
    RR_MASK_SOLID = RR_SOLID | RR_WINDOW,
    RR_MASK_DEAD = RR_MASK_SOLID | RR_PLAYERCLIP,
    RR_MASK_PLAYER = RR_MASK_DEAD | RR_MONSTER | RR_PLAYER,
    RR_MASK_WATER = RR_LAVA | RR_SLIME | RR_WATER,
    RR_MASK_CURRENT = RR_CURRENT_0 | RR_CURRENT_90 | RR_CURRENT_180 |
                      RR_CURRENT_270 | RR_CURRENT_UP | RR_CURRENT_DOWN,
    RR_MAX_TOUCH = 32
};

typedef struct rr_touches {
    qa_trace_result traces[RR_MAX_TOUCH];
    size_t count;
} rr_touches;
typedef struct rr_move {
    qa_move_context *move;
    qa_q2r_movement_state *state;
    qa_usercmd command;
    qa_movement_result *result;
    qa_vec3 local_origin, *origin, velocity, previous_origin, start_velocity, forward, right, up;
    qa_q2r_slide *generic;
    qa_bounds character, bounds, accepted_bounds;
    uint32_t accepted_duck;
    float accepted_height, dt, speed, max_speed, duck_speed;
    qa_collision_plane ground_plane;
    int32_t ground_contents;
    int32_t ground_surface_flags;
    bool ground_surface, n64;
    rr_touches touches;
} rr_move;

static float rr_normalize(qa_vec3 *vector)
{
    float length = qa_vec_length(*vector);
    if (length != 0.0f) *vector = qa_vec_scale(*vector, 1.0f / length);
    return length;
}

static bool rr_grounded(const rr_move *p)
{
    return p->result->ground.hit != QA_TRACE_HIT_NONE;
}

static qa_trace_result rr_trace(rr_move *p, qa_vec3 start, qa_vec3 end,
                                qa_bounds bounds, uint32_t mask)
{
    qa_trace_result trace = {0};
    trace.end = start;
    trace.all_solid = trace.start_solid = true;
    if (p->move->failed) return trace;
    if (p->generic) {
        if (!p->generic->trace(p->generic->context, start, end, bounds, &trace, p->move->error))
            p->move->failed = true;
        else if (trace.family != QA_COLLISION_Q2 || !isfinite(trace.fraction) ||
                 trace.fraction < 0.0f || trace.fraction > 1.0f || !qa_vec_finite(trace.end)) {
            qa_error_set(p->move->error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 rerelease trace result");
            p->move->failed = true;
        }
        return trace;
    }
    bool world_only = p->state->type == RR_SPECTATOR;
    if (world_only) mask = RR_MASK_SOLID;
    else if (mask == 0) {
        mask = p->state->type == RR_DEAD || p->state->type == RR_GIB
            ? RR_MASK_DEAD : RR_MASK_PLAYER;
        if (p->state->flags & RR_IGNORE_PLAYERS) mask &= ~(uint32_t)RR_PLAYER;
    }
    qa_move_trace(p->move, start, end, bounds, qa_collision_contents_mask(mask,QA_COLLISION_Q2), world_only, &trace);
    return trace;
}

static int32_t rr_contents(rr_move *p, qa_vec3 point)
{
    int32_t contents = 0;
    if (!p->move->failed) qa_move_contents(p->move, point, &contents);
    return contents;
}

static void rr_record(rr_touches *touches, const qa_trace_result *trace)
{
    if (touches->count == RR_MAX_TOUCH) return;
    qa_movement_ground hit = qa_move_ground(trace);
    for (size_t i = 0; i < touches->count; ++i)
        if (qa_move_same_ground(qa_move_ground(&touches->traces[i]), hit)) return;
    touches->traces[touches->count] = *trace;
    if (trace->contact) touches->traces[touches->count].contact_plane = trace->plane;
    ++touches->count;
}

static qa_vec3 rr_clip(qa_vec3 velocity, qa_vec3 normal)
{
    return qa_move_clip(velocity, normal, 1.01f, 0.1f);
}

/* The source searches face slabs in this order and excludes the final
 * candidate from its distance sort. Retain that choice when several fit. */
static qa_q2r_stuck_result rr_fix_stuck(rr_move *p, qa_vec3 *origin, qa_bounds bounds)
{
    static const qa_vec3 normals[6] = {
        {0,0,1}, {0,0,-1}, {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}
    };
    qa_trace_result trace = rr_trace(p, *origin, *origin, bounds, 0);
    if (!trace.start_solid) return QA_Q2R_GOOD_POSITION;
    qa_vec3 good[6];
    float distances[6];
    size_t count = 0;
    for (unsigned side = 0; side < 6 && !p->move->failed; ++side) {
        qa_vec3 normal = normals[side], start = *origin;
        qa_bounds slab = bounds;
        unsigned axis = side < 2 ? 2 : side < 4 ? 0 : 1;
        float sign = qa_move_component(normal, axis);
        float edge = qa_move_component(sign < 0.0f ? bounds.mins : bounds.maxs, axis);
        qa_move_set_component(&start, axis, qa_move_component(start, axis) + edge);
        qa_move_set_component(&slab.mins, axis, 0.0f);
        qa_move_set_component(&slab.maxs, axis, 0.0f);
        trace = rr_trace(p, start, start, slab, 0);
        int epsilon_axis = -1;
        float epsilon_direction = 0.0f;
        if (trace.start_solid) {
            for (unsigned e = 0; e < 3; ++e) {
                if (e == axis) continue;
                qa_vec3 probe = start;
                qa_move_set_component(&probe, e, qa_move_component(probe, e) + 1.0f);
                trace = rr_trace(p, probe, probe, slab, 0);
                if (!trace.start_solid) {
                    start = probe; epsilon_axis = (int)e; epsilon_direction = 1.0f; break;
                }
                qa_move_set_component(&probe, e, qa_move_component(probe, e) - 2.0f);
                trace = rr_trace(p, probe, probe, slab, 0);
                if (!trace.start_solid) {
                    start = probe; epsilon_axis = (int)e; epsilon_direction = -1.0f; break;
                }
            }
        }
        if (trace.start_solid) continue;
        qa_vec3 opposite = *origin;
        edge = qa_move_component(sign > 0.0f ? bounds.mins : bounds.maxs, axis);
        qa_move_set_component(&opposite, axis, qa_move_component(opposite, axis) + edge);
        if (epsilon_axis >= 0) {
            unsigned e = (unsigned)epsilon_axis;
            qa_move_set_component(&opposite, e, qa_move_component(opposite, e) + epsilon_direction);
        }
        trace = rr_trace(p, start, opposite, slab, 0);
        if (trace.start_solid) continue;
        qa_vec3 end = qa_vec_add(trace.end, qa_vec_scale(normal, 0.125f));
        qa_vec3 delta = qa_vec_sub(end, opposite);
        qa_vec3 next = qa_vec_add(*origin, delta);
        if (epsilon_axis >= 0) {
            unsigned e = (unsigned)epsilon_axis;
            qa_move_set_component(&next, e, qa_move_component(next, e) + epsilon_direction);
        }
        trace = rr_trace(p, next, next, bounds, 0);
        if (trace.start_solid) continue;
        good[count] = next;
        distances[count++] = qa_vec_dot(delta, delta);
    }
    if (p->move->failed || count == 0) return QA_Q2R_NO_GOOD_POSITION;
    size_t best = 0;
    for (size_t i = 1; i + 1 < count; ++i)
        if (distances[i] < distances[best]) best = i;
    *origin = good[best];
    return QA_Q2R_FIXED_POSITION;
}

static void rr_slide(rr_move *p, qa_vec3 *origin, qa_vec3 *velocity,
                     float elapsed, rr_touches *touches, bool has_time)
{
    qa_vec3 primal = *velocity, planes[5];
    unsigned plane_count = 0;
    float remaining = elapsed;
    for (unsigned bump = 0; bump < 4 && !p->move->failed; ++bump) {
        qa_vec3 end = qa_vec_add(*origin, qa_vec_scale(*velocity, remaining));
        qa_trace_result trace = rr_trace(p, *origin, end, p->bounds, 0);
        if (p->move->failed) return;
        if (trace.all_solid) {
            velocity->z = 0.0f;
            rr_record(touches, &trace);
            return;
        }
        if (trace.has_secondary && trace.secondary_has_surface) {
            qa_vec3 first = rr_clip(*velocity, trace.plane.normal);
            qa_vec3 second = rr_clip(*velocity, trace.secondary_plane.normal);
            if (fabsf(first.x) < fabsf(second.x) || fabsf(first.y) < fabsf(second.y) ||
                fabsf(first.z) < fabsf(second.z)) {
                trace.plane = trace.secondary_plane;
                trace.surface = trace.secondary_surface;
                trace.has_surface = true;
                trace.surface_flags = trace.surface.flags;
                if (trace.contact) trace.contact_plane = trace.plane;
            }
        }
        if (trace.fraction > 0.0f) {
            *origin = trace.end;
            plane_count = 0;
        }
        if (trace.fraction == 1.0f) break;
        rr_record(touches, &trace);
        remaining -= remaining * trace.fraction;
        if (plane_count == 5) { *velocity = qa_v3(0,0,0); break; }
        qa_vec3 normal = trace.plane.normal;
        bool duplicate = false;
        for (unsigned i = 0; i < plane_count; ++i) {
            if (qa_vec_dot(normal, planes[i]) > 0.99f) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            /* This aliases the live player origin even during waterjump probes. */
            p->origin->x += normal.x * 0.01f;
            p->origin->y += normal.y * 0.01f;
            rr_fix_stuck(p, p->origin, p->bounds);
            continue;
        }
        planes[plane_count++] = normal;
        qa_vec3 candidate = *velocity;
        bool accepted = false;
        for (unsigned i = 0; i < plane_count; ++i) {
            candidate = rr_clip(candidate, planes[i]);
            unsigned j;
            for (j = 0; j < plane_count; ++j)
                if (i != j && qa_vec_dot(candidate, planes[j]) < 0.0f) break;
            if (j == plane_count) { accepted = true; break; }
        }
        if (!accepted) {
            if (plane_count != 2) { *velocity = qa_v3(0,0,0); break; }
            qa_vec3 direction = qa_vec_cross(planes[0], planes[1]);
            candidate = qa_vec_scale(direction, qa_vec_dot(direction, candidate));
        }
        if (qa_vec_dot(candidate, primal) <= 0.0f) { *velocity = qa_v3(0,0,0); break; }
        *velocity = candidate;
    }
    if (!p->move->failed && has_time) *velocity = primal;
}

static void rr_step_slide(rr_move *p)
{
    qa_vec3 start = *p->origin, start_velocity = p->velocity;
    rr_slide(p, p->origin, &p->velocity, p->dt, &p->touches, p->state->time_ms != 0);
    if (p->move->failed) return;
    qa_vec3 down_origin = *p->origin, down_velocity = p->velocity;
    qa_vec3 up = start; up.z += 18.0f;
    qa_trace_result trace = rr_trace(p, start, up, p->bounds, 0);
    if (p->move->failed || trace.all_solid) return;
    float step = trace.end.z - start.z;
    *p->origin = trace.end;
    p->velocity = start_velocity;
    rr_slide(p, p->origin, &p->velocity, p->dt, &p->touches, p->state->time_ms != 0);
    if (p->move->failed) return;
    qa_vec3 down = *p->origin; down.z -= step;
    qa_vec3 original_down = down;
    if (start.z < down.z) down.z = start.z - 1.0f;
    trace = rr_trace(p, *p->origin, down, p->bounds, 0);
    if (p->move->failed) return;
    if (!trace.all_solid) {
        qa_trace_result real_trace = rr_trace(p, *p->origin, original_down, p->bounds, 0);
        if (p->move->failed) return;
        *p->origin = real_trace.end;
        if (p->velocity.z > 0.0f) p->result->step_clip = true;
    }
    float dx = down_origin.x - start.x, dy = down_origin.y - start.y;
    float ux = p->origin->x - start.x, uy = p->origin->y - start.y;
    if (dx * dx + dy * dy > ux * ux + uy * uy || trace.plane.normal.z < 0.7f) {
        *p->origin = down_origin;
        p->velocity = down_velocity;
    } else p->velocity.z = down_velocity.z;
    if ((p->state->flags & RR_ON_GROUND) && !(p->state->flags & RR_LADDER) &&
        (p->result->water_level < 2 ||
         (!(p->command.buttons & RR_JUMP_BUTTON) && p->velocity.z <= 0.0f))) {
        down = *p->origin; down.z -= 18.0f;
        trace = rr_trace(p, *p->origin, down, p->bounds, 0);
        if (p->move->failed) return;
        if (trace.fraction < 1.0f) *p->origin = trace.end;
    }
}

static void rr_accelerate(rr_move *p, qa_vec3 direction, float speed, float accel,
                          bool air)
{
    float target = air ? fminf(speed, 30.0f) : speed;
    float add = target - qa_vec_dot(p->velocity, direction);
    if (add <= 0.0f) return;
    float amount = air ? accel * speed * p->dt : accel * p->dt * speed;
    if (amount > add) amount = add;
    p->velocity = qa_vec_add(p->velocity, qa_vec_scale(direction, amount));
}

static void rr_friction(rr_move *p)
{
    float speed = qa_vec_length(p->velocity);
    if (speed < 1.0f) { p->velocity.x = p->velocity.y = 0.0f; return; }
    float drop = 0.0f;
    if ((rr_grounded(p) && p->ground_surface && !(p->ground_surface_flags & 2)) ||
        (p->state->flags & RR_LADDER))
        drop += fmaxf(speed, 100.0f) * 6.0f * p->dt;
    if (p->result->water_level != 0 && !(p->state->flags & RR_LADDER))
        drop += speed * (float)p->result->water_level * p->dt;
    p->velocity = qa_vec_scale(p->velocity, fmaxf(0.0f, speed - drop) / speed);
}

static qa_vec3 rr_current(int32_t contents)
{
    qa_vec3 current = {0};
    if (contents & RR_CURRENT_0) current.x += 1.0f;
    if (contents & RR_CURRENT_90) current.y += 1.0f;
    if (contents & RR_CURRENT_180) current.x -= 1.0f;
    if (contents & RR_CURRENT_270) current.y -= 1.0f;
    if (contents & RR_CURRENT_UP) current.z += 1.0f;
    if (contents & RR_CURRENT_DOWN) current.z -= 1.0f;
    return current;
}

static void rr_add_currents(rr_move *p, qa_vec3 *wish)
{
    const qa_usercmd *cmd = &p->command;
    if (p->state->flags & RR_LADDER) {
        if (cmd->buttons & (RR_JUMP_BUTTON | RR_CROUCH_BUTTON)) {
            float speed = p->result->water_level >= 2 ? p->max_speed : 200.0f;
            wish->z = cmd->buttons & RR_JUMP_BUTTON ? speed : -speed;
        } else if (cmd->forward_move != 0) {
            float speed = fminf(fmaxf((float)cmd->forward_move, -200.0f), 200.0f);
            if (cmd->forward_move > 0) wish->z = p->result->view_angles.x < 15.0f ? speed : -speed;
            else {
                if (!rr_grounded(p)) wish->x = wish->y = 0.0f;
                wish->z = speed;
            }
        } else wish->z = 0.0f;
        if (!rr_grounded(p)) {
            if (cmd->side_move != 0) {
                float speed = fminf(fmaxf((float)cmd->side_move, -150.0f), 150.0f);
                if (p->result->water_level < 2) speed *= 0.5f;
                qa_vec3 flat = p->forward; flat.z = 0.0f; rr_normalize(&flat);
                qa_trace_result trace = rr_trace(p, *p->origin, qa_vec_add(*p->origin, flat),
                                                 p->bounds, RR_CONTENTS_LADDER);
                if (trace.fraction != 1.0f &&
                    (qa_collision_contents_export(trace.contents,QA_COLLISION_Q2,trace.q1_opaque_token) & RR_CONTENTS_LADDER)) {
                    qa_vec3 right = qa_vec_cross(trace.plane.normal, qa_v3(0,0,1));
                    wish->x = wish->y = 0.0f;
                    *wish = qa_vec_add(*wish, qa_vec_scale(right, -speed));
                }
            } else {
                wish->x = fminf(fmaxf(wish->x, -25.0f), 25.0f);
                wish->y = fminf(fmaxf(wish->y, -25.0f), 25.0f);
            }
        }
    }
    if (p->result->water_type & RR_MASK_CURRENT) {
        float speed = p->result->water_level == 1 && rr_grounded(p) ? 200.0f : 400.0f;
        *wish = qa_vec_add(*wish, qa_vec_scale(rr_current(p->result->water_type), speed));
    }
    if (rr_grounded(p))
        *wish = qa_vec_add(*wish, qa_vec_scale(rr_current(p->ground_contents), 100.0f));
}

static qa_vec3 rr_wish(rr_move *p)
{
    float forward = (float)p->command.forward_move * p->speed;
    float side = (float)p->command.side_move * p->speed;
    return qa_vec_add(qa_vec_scale(p->forward, forward), qa_vec_scale(p->right, side));
}

static void rr_water_move(rr_move *p)
{
    qa_vec3 wish = rr_wish(p);
    const qa_usercmd *cmd = &p->command;
    if (cmd->forward_move == 0 && cmd->side_move == 0 && !(cmd->buttons & (RR_JUMP_BUTTON | RR_CROUCH_BUTTON))) {
        if (!rr_grounded(p)) wish.z -= 60.0f;
    } else if (cmd->buttons & RR_CROUCH_BUTTON) wish.z -= 200.0f * p->speed;
    else if (cmd->buttons & RR_JUMP_BUTTON) wish.z += 200.0f * p->speed;
    rr_add_currents(p, &wish);
    if (p->move->failed) return;
    float speed = rr_normalize(&wish);
    if (speed > p->max_speed) speed = p->max_speed;
    speed *= 0.5f;
    if ((p->state->flags & RR_DUCKED) && speed > p->duck_speed) speed = p->duck_speed;
    rr_accelerate(p, wish, speed, 10.0f, false);
    rr_step_slide(p);
}

static void rr_air_move(rr_move *p)
{
    qa_vec3 wish = rr_wish(p); wish.z = 0.0f;
    rr_add_currents(p, &wish);
    if (p->move->failed) return;
    float vertical = wish.z;
    float speed = rr_normalize(&wish);
    float limit = p->state->flags & RR_DUCKED ? p->duck_speed : p->max_speed;
    if (speed > limit) {
        vertical *= limit / speed;
        speed = limit;
    }
    float gravity = (float)p->state->gravity * p->dt;
    if (p->state->flags & RR_LADDER) {
        rr_accelerate(p, wish, speed, 10.0f, false);
        if (vertical == 0.0f) {
            if (p->velocity.z > 0.0f) p->velocity.z = fmaxf(0.0f, p->velocity.z - gravity);
            else p->velocity.z = fminf(0.0f, p->velocity.z + gravity);
        }
        rr_step_slide(p);
    } else if (rr_grounded(p)) {
        p->velocity.z = 0.0f;
        rr_accelerate(p, wish, speed, 10.0f, false);
        if (p->state->gravity > 0) p->velocity.z = 0.0f;
        else p->velocity.z -= gravity;
        if (p->velocity.x == 0.0f && p->velocity.y == 0.0f) return;
        rr_step_slide(p);
    } else {
        float accel = p->move->input->profile.data.q2r.air_accelerate;
        rr_accelerate(p, wish, speed, accel != 0.0f ? accel : 1.0f, accel != 0.0f);
        if (p->state->type != RR_GRAPPLE) p->velocity.z -= gravity;
        rr_step_slide(p);
    }
}

static int rr_water_level(rr_move *p, qa_vec3 position, int32_t *water_type)
{
    *water_type = 0;
    float sample2 = truncf(p->state->view_height - p->bounds.mins.z);
    float sample1 = truncf(sample2 / 2.0f);
    qa_vec3 point = position; point.z += p->bounds.mins.z + 1.0f;
    int32_t contents = rr_contents(p, point);
    if (!(contents & RR_MASK_WATER)) return 0;
    *water_type = contents;
    /* Source upper samples use pml.origin, including hypothetical landing probes. */
    point.z = p->origin->z + p->bounds.mins.z + sample1;
    if (!(rr_contents(p, point) & RR_MASK_WATER)) return 1;
    point.z = p->origin->z + p->bounds.mins.z + sample2;
    return rr_contents(p, point) & RR_MASK_WATER ? 3 : 2;
}

static void rr_categorize(rr_move *p)
{
    qa_movement_ground none = {0};
    if (p->velocity.z > 180.0f || p->state->type == RR_GRAPPLE) {
        p->state->flags &= ~(uint32_t)RR_ON_GROUND;
        p->result->ground = none;
    } else {
        qa_vec3 point = *p->origin; point.z -= 0.25f;
        qa_trace_result trace = rr_trace(p, *p->origin, point, p->bounds, 0);
        if (p->move->failed) return;
        p->ground_plane = trace.plane;
        p->ground_surface = trace.has_surface;
        p->ground_surface_flags = qa_collision_surface_export(trace.surface.flags,QA_COLLISION_Q2);
        p->ground_contents = qa_collision_contents_export(trace.contents,QA_COLLISION_Q2,trace.q1_opaque_token);
        bool slanted = trace.fraction < 1.0f && trace.plane.normal.z < 0.7f;
        if (slanted) {
            qa_trace_result slant = rr_trace(p, *p->origin,
                qa_vec_add(*p->origin, trace.plane.normal), p->bounds, 0);
            if (p->move->failed) return;
            if (slant.fraction < 1.0f && !slant.start_solid) slanted = false;
        }
        if (trace.fraction == 1.0f || (slanted && !trace.start_solid)) {
            p->result->ground = none;
            p->state->flags &= ~(uint32_t)RR_ON_GROUND;
        } else {
            p->result->ground = qa_move_ground(&trace);
            if (p->state->flags & RR_WATERJUMP) {
                p->state->flags &= ~(uint32_t)RR_TIMERS;
                p->state->time_ms = 0;
            }
            if (!(p->state->flags & RR_ON_GROUND)) {
                if (!p->n64 && p->velocity.z >= 100.0f && p->ground_plane.normal.z >= 0.9f &&
                    !(p->state->flags & RR_DUCKED)) {
                    p->state->flags |= RR_TRICK;
                    p->state->time_ms = 64;
                }
                qa_vec3 clipped = rr_clip(p->velocity, p->ground_plane.normal);
                p->result->impact_delta = p->start_velocity.z - clipped.z;
                p->state->flags |= RR_ON_GROUND;
                if (p->n64 || (p->state->flags & RR_DUCKED)) {
                    p->state->flags |= RR_LAND;
                    p->state->time_ms = 128;
                }
            }
        }
        rr_record(&p->touches, &trace);
    }
    p->result->water_level = rr_water_level(p, *p->origin, &p->result->water_type);
}

static void rr_check_jump(rr_move *p)
{
    if (p->state->flags & RR_LAND) return;
    if (!(p->command.buttons & RR_JUMP_BUTTON)) {
        p->state->flags &= ~(uint32_t)RR_JUMP_HELD;
        return;
    }
    if ((p->state->flags & RR_JUMP_HELD) || p->state->type == RR_DEAD) return;
    if (p->result->water_level >= 2) { p->result->ground = (qa_movement_ground){0}; return; }
    if (!rr_grounded(p)) return;
    p->state->flags |= RR_JUMP_HELD;
    p->result->jump_sound = true;
    p->result->ground = (qa_movement_ground){0};
    p->state->flags &= ~(uint32_t)RR_ON_GROUND;
    p->velocity.z = fmaxf(p->velocity.z + 270.0f, 270.0f);
}

static void rr_special(rr_move *p)
{
    if (p->state->time_ms != 0) return;
    p->state->flags &= ~(uint32_t)RR_LADDER;
    qa_vec3 flat = p->forward; flat.z = 0.0f; rr_normalize(&flat);
    qa_trace_result trace = rr_trace(p, *p->origin, qa_vec_add(*p->origin, flat),
                                     p->bounds, RR_CONTENTS_LADDER);
    if (trace.fraction < 1.0f &&
        (qa_collision_contents_export(trace.contents,QA_COLLISION_Q2,trace.q1_opaque_token) & RR_CONTENTS_LADDER) &&
        p->result->water_level < 2)
        p->state->flags |= RR_LADDER;
    if (p->state->gravity == 0 ||
        (!(p->command.buttons & RR_JUMP_BUTTON) && p->command.forward_move <= 0) ||
        p->result->water_level != 2 || (p->result->water_type & RR_NO_WATERJUMP)) return;
    trace = rr_trace(p, *p->origin, qa_vec_add(*p->origin, qa_vec_scale(flat, 40.0f)),
                     p->bounds, RR_MASK_SOLID);
    if (trace.fraction == 1.0f || trace.plane.normal.z >= 0.7f) return;
    qa_vec3 velocity = qa_vec_scale(flat, 50.0f); velocity.z = 350.0f;
    qa_vec3 origin = *p->origin;
    rr_touches touches = {0};
    bool has_time = true;
    int count = (int)fminf(50.0f, truncf(10.0f * (800.0f / (float)p->state->gravity)));
    for (int i = 0; i < count && !p->move->failed; ++i) {
        velocity.z -= (float)p->state->gravity * 0.1f;
        if (velocity.z < 0.0f) has_time = false;
        rr_slide(p, &origin, &velocity, 0.1f, &touches, has_time);
    }
    qa_vec3 down = origin; down.z -= 2.0f;
    trace = rr_trace(p, origin, down, p->bounds, RR_MASK_SOLID);
    if (trace.fraction == 1.0f || trace.plane.normal.z < 0.7f || trace.end.z < p->origin->z) return;
    if (rr_grounded(p) && fabsf(p->origin->z - trace.end.z) <= 18.0f) return;
    int32_t water_type;
    if (rr_water_level(p, trace.end, &water_type) >= 2) return;
    p->velocity = qa_vec_scale(flat, 50.0f); p->velocity.z = 350.0f;
    p->state->flags |= RR_WATERJUMP;
    p->state->time_ms = 2048;
}

static void rr_fly(rr_move *p, bool clip)
{
    p->state->view_height = clip ? 0.0f : 22.0f;
    float speed = qa_vec_length(p->velocity);
    if (speed < 1.0f) p->velocity = qa_v3(0,0,0);
    else {
        float drop = fmaxf(speed, 100.0f) * 9.0f * p->dt;
        p->velocity = qa_vec_scale(p->velocity, fmaxf(0.0f, speed - drop) / speed);
    }
    rr_normalize(&p->forward); rr_normalize(&p->right);
    qa_vec3 wish = rr_wish(p);
    if (p->command.buttons & RR_JUMP_BUTTON) wish.z += 200.0f * p->speed;
    if (p->command.buttons & RR_CROUCH_BUTTON) wish.z -= 200.0f * p->speed;
    speed = rr_normalize(&wish);
    if (speed > p->max_speed) speed = p->max_speed;
    rr_accelerate(p, wish, speed * 2.0f, 10.0f, false);
    if (clip) rr_step_slide(p);
    else *p->origin = qa_vec_add(*p->origin, qa_vec_scale(p->velocity, p->dt));
}

static float rr_height(const rr_move *p, float source)
{
    return p->character.mins.z + ((source + 24.0f) / 56.0f) *
           (p->character.maxs.z - p->character.mins.z);
}

static bool rr_expands(qa_bounds from, qa_bounds to)
{
    return to.mins.x < from.mins.x || to.mins.y < from.mins.y || to.mins.z < from.mins.z ||
           to.maxs.x > from.maxs.x || to.maxs.y > from.maxs.y || to.maxs.z > from.maxs.z;
}

static void rr_dimensions(rr_move *p)
{
    p->bounds = p->character;
    if (p->state->type == RR_GIB) {
        p->bounds.mins.z = rr_height(p, 0.0f);
        p->bounds.maxs.z = rr_height(p, 16.0f);
        p->state->view_height = rr_height(p, 8.0f);
        return;
    }
    p->bounds.mins.z = rr_height(p, -24.0f);
    if ((p->state->flags & RR_DUCKED) || p->state->type == RR_DEAD) {
        p->bounds.maxs.z = rr_height(p, 4.0f);
        p->state->view_height = rr_height(p, -2.0f);
    } else {
        p->bounds.maxs.z = rr_height(p, 32.0f);
        p->state->view_height = rr_height(p, 22.0f);
    }
    if (rr_expands(p->accepted_bounds, p->bounds)) {
        qa_trace_result trace = rr_trace(p, *p->origin, *p->origin, p->bounds, 0);
        if (trace.all_solid) {
            p->bounds = p->accepted_bounds;
            p->state->flags = (p->state->flags & ~(uint32_t)RR_DUCKED) | p->accepted_duck;
            p->state->view_height = p->accepted_height;
        }
    }
    p->accepted_bounds = p->bounds;
    p->accepted_duck = p->state->flags & RR_DUCKED;
    p->accepted_height = p->state->view_height;
}

static bool rr_above_water(rr_move *p)
{
    qa_vec3 down = *p->origin; down.z -= 8.0f;
    qa_trace_result trace = rr_trace(p, *p->origin, down, p->bounds, RR_MASK_SOLID);
    if (trace.fraction < 1.0f) return false;
    trace = rr_trace(p, *p->origin, down, p->bounds, RR_MASK_WATER);
    return trace.fraction < 1.0f;
}

static bool rr_duck(rr_move *p)
{
    if (p->state->type == RR_GIB) return false;
    bool changed = false;
    if (p->state->type == RR_DEAD) {
        if (!(p->state->flags & RR_DUCKED)) { p->state->flags |= RR_DUCKED; changed = true; }
    } else {
        bool crouch = (p->command.buttons & RR_CROUCH_BUTTON) &&
            (rr_grounded(p) || (p->result->water_level <= 1 && !rr_above_water(p))) &&
            !(p->state->flags & RR_LADDER) && !p->n64;
        if (crouch != ((p->state->flags & RR_DUCKED) != 0)) {
            qa_bounds bounds = p->bounds;
            bounds.maxs.z = crouch ? rr_height(p, 4.0f) : p->character.maxs.z;
            qa_trace_result trace = rr_trace(p, *p->origin, *p->origin, bounds, 0);
            if (!trace.all_solid) {
                if (crouch) p->state->flags |= RR_DUCKED;
                else p->state->flags &= ~(uint32_t)RR_DUCKED;
                changed = true;
            }
        }
    }
    if (changed) rr_dimensions(p);
    return changed;
}

static bool rr_good_position(rr_move *p)
{
    if (p->state->type == RR_NOCLIP) return true;
    qa_trace_result trace = rr_trace(p, p->state->origin, p->state->origin, p->bounds, 0);
    return !trace.all_solid;
}

static void rr_snap(rr_move *p)
{
    p->state->velocity = p->velocity;
    p->state->origin = *p->origin;
    if (rr_good_position(p)) return;
    if (rr_fix_stuck(p, &p->state->origin, p->bounds) == QA_Q2R_NO_GOOD_POSITION)
        p->state->origin = p->previous_origin;
}

static void rr_initial_snap(rr_move *p)
{
    static const float offsets[3] = {0.0f, -1.0f, 1.0f};
    qa_vec3 base = p->state->origin;
    for (unsigned z = 0; z < 3 && !p->move->failed; ++z) {
        p->state->origin.z = base.z + offsets[z];
        for (unsigned y = 0; y < 3; ++y) {
            p->state->origin.y = base.y + offsets[y];
            for (unsigned x = 0; x < 3; ++x) {
                p->state->origin.x = base.x + offsets[x];
                if (rr_good_position(p)) {
                    *p->origin = p->previous_origin = p->state->origin;
                    return;
                }
            }
        }
    }
}

static void rr_screen(rr_move *p)
{
    qa_vec3 eye = qa_vec_add(*p->origin, p->move->input->view_offset);
    eye.z += p->state->view_height;
    int32_t contents = rr_contents(p, eye);
    p->result->render_flags = contents & RR_MASK_WATER ? 1 : 0;
    float *blend = p->result->screen_blend;
    if (contents & (RR_SOLID | RR_LAVA)) {
        blend[0] = 1.0f; blend[1] = 0.3f; blend[2] = 0.0f; blend[3] = 0.6f;
    } else if (contents & RR_SLIME) {
        blend[0] = 0.0f; blend[1] = 0.1f; blend[2] = 0.05f; blend[3] = 0.6f;
    } else if (contents & RR_WATER) {
        blend[0] = 0.5f; blend[1] = 0.3f; blend[2] = 0.2f; blend[3] = 0.4f;
    }
}

static void rr_pmove(rr_move *p)
{
    qa_vec3 angles = qa_vec_add(p->command.angles, p->state->delta_angles);
    if (p->state->flags & RR_TELEPORT) angles.x = angles.z = 0.0f;
    else if (angles.x > 89.0f && angles.x < 180.0f) angles.x = 89.0f;
    else if (angles.x < 271.0f && angles.x >= 180.0f) angles.x = 271.0f;
    p->result->view_angles = angles;
    qa_move_angles(angles, &p->forward, &p->right, &p->up);
    const qa_movement_environment *env = &p->move->input->environment;
    if (env->flight && env->health > 0 && p->state->type == RR_NORMAL) {
        p->state->flags &= ~(uint32_t)(RR_ON_GROUND | RR_DUCKED | RR_WATERJUMP);
        p->state->time_ms = 0;
        rr_fly(p, true);
        if (!p->move->failed) rr_snap(p);
        return;
    }
    if (p->state->type == RR_SPECTATOR || p->state->type == RR_NOCLIP) {
        p->state->flags = 0;
        if (p->state->type == RR_SPECTATOR) {
            p->bounds.mins.x = p->character.mins.x * 0.5f;
            p->bounds.mins.y = p->character.mins.y * 0.5f;
            p->bounds.maxs.x = p->character.maxs.x * 0.5f;
            p->bounds.maxs.y = p->character.maxs.y * 0.5f;
            p->bounds.mins.z = rr_height(p, -8.0f);
            p->bounds.maxs.z = rr_height(p, 8.0f);
        }
        rr_fly(p, p->state->type == RR_SPECTATOR);
        if (!p->move->failed) rr_snap(p);
        return;
    }
    if (p->state->type >= RR_DEAD) {
        p->command.forward_move = p->command.side_move = 0;
        p->command.buttons &= ~(uint32_t)(RR_JUMP_BUTTON | RR_CROUCH_BUTTON);
    }
    if (p->state->type == RR_FREEZE) return;
    rr_dimensions(p);
    if (p->move->failed) return;
    rr_categorize(p);
    if (p->move->failed) return;
    if (p->move->input->snap_initial) rr_initial_snap(p);
    if (p->move->failed) return;
    if (rr_duck(p)) rr_categorize(p);
    if (p->move->failed) return;
    if (p->state->type == RR_DEAD && rr_grounded(p)) {
        float speed = qa_vec_length(p->velocity) - 20.0f;
        if (speed <= 0.0f) p->velocity = qa_v3(0,0,0);
        else { rr_normalize(&p->velocity); p->velocity = qa_vec_scale(p->velocity, speed); }
    }
    rr_special(p);
    if (p->move->failed) return;
    if (p->state->time_ms != 0) {
        if (p->command.milliseconds >= p->state->time_ms) {
            p->state->flags &= ~(uint32_t)RR_TIMERS;
            p->state->time_ms = 0;
        } else p->state->time_ms -= p->command.milliseconds;
    }
    if (p->state->flags & RR_TELEPORT) {
        /* Teleport time holds the position while still updating presentation. */
    } else if (p->state->flags & RR_WATERJUMP) {
        p->velocity.z -= (float)p->state->gravity * p->dt;
        if (p->velocity.z < 0.0f) {
            p->state->flags &= ~(uint32_t)RR_TIMERS;
            p->state->time_ms = 0;
        }
        rr_step_slide(p);
    } else {
        rr_check_jump(p);
        rr_friction(p);
        if (p->result->water_level >= 2) rr_water_move(p);
        else {
            angles = p->result->view_angles;
            if (angles.x > 180.0f) angles.x -= 360.0f;
            angles.x /= 3.0f;
            qa_move_angles(angles, &p->forward, &p->right, &p->up);
            rr_air_move(p);
        }
    }
    if (p->move->failed) return;
    rr_categorize(p);
    if (p->move->failed) return;
    if (p->state->flags & RR_TRICK) rr_check_jump(p);
    rr_screen(p);
    if (!p->move->failed) rr_snap(p);
}

bool qa_move_q2r(qa_move_context *move)
{
    const qa_movement_input *input = move->input;
    const qa_movement_environment *env = &input->environment;
    qa_q2r_movement_state *state = &move->state->data.q2r;
    qa_movement_result *result = move->result;
    int32_t original_type = state->type;
    bool mode = env->has_mode && env->health > 0;
    if (mode) state->type = qa_move_mode_type(QA_RULESET_Q2_RERELEASE, env->mode);
    if (env->fixed_pose) { state->type = RR_FREEZE; state->velocity = qa_v3(0,0,0); }
    qa_bounds character = input->shape.kind == QA_SHAPE_POINT ? (qa_bounds){0} : input->shape.bounds;
    if (env->fixed_pose) character = env->pose.bounds;
    rr_move p = {0};
    p.move = move; p.state = state; p.result = result; p.command = move->command;
    p.origin = input->q2r_pml_origin ? input->q2r_pml_origin : &p.local_origin;
    *p.origin = p.previous_origin = state->origin;
    p.velocity = p.start_velocity = state->velocity;
    p.character = p.bounds = character;
    p.accepted_bounds = input->has_current_bounds ? input->current_bounds : character;
    p.accepted_duck = state->flags & RR_DUCKED;
    p.dt = (float)move->command.milliseconds * 0.001f;
    p.speed = env->speed_multiplier;
    p.max_speed = 300.0f * p.speed;
    p.duck_speed = 100.0f * p.speed;
    p.n64 = input->profile.data.q2r.n64_physics;
    state->view_height = 0.0f;
    result->ground = (qa_movement_ground){0};
    result->water_level = result->water_type = 0;
    result->jump_sound = result->step_clip = false;
    result->impact_delta = 0.0f; result->render_flags = 0;
    memset(result->screen_blend, 0, sizeof(result->screen_blend));
    result->contact_count = 0;
    result->view_offset = input->view_offset;
    move->dt = p.dt;
    move->milliseconds = move->command.milliseconds;
    rr_pmove(&p);
    if (env->fixed_pose) {
        state->view_height = env->pose.view_height;
        if (env->fixed_crouched) state->flags |= RR_DUCKED;
        else state->flags &= ~(uint32_t)RR_DUCKED;
    }
    if (mode || env->fixed_pose) state->type = original_type;
    if (move->failed) return false;
    result->bounds = p.bounds;
    result->view_height = state->view_height;
    result->horizontal_speed = sqrtf(state->velocity.x * state->velocity.x + state->velocity.y * state->velocity.y);
    for (size_t i = 0; i < p.touches.count; ++i)
        if (!qa_move_contact(move, &p.touches.traces[i], false, true)) return false;
    return !move->failed;
}

bool qa_move_q2r_slide(qa_q2r_slide *slide, qa_error *error)
{
    if (!slide || !slide->trace || !slide->origin || !slide->velocity || !slide->pml_origin ||
        !isfinite(slide->elapsed) || slide->elapsed < 0.0f) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 rerelease slide input");
        return false;
    }
    qa_move_context move = {.error = error};
    rr_move p = {.move = &move, .generic = slide, .origin = slide->pml_origin, .bounds = slide->bounds};
    rr_slide(&p, slide->origin, slide->velocity, slide->elapsed, &p.touches, slide->has_time);
    slide->contact_count = p.touches.count;
    memcpy(slide->contacts, p.touches.traces, p.touches.count * sizeof(*slide->contacts));
    return !move.failed;
}

bool qa_move_q2r_fix_stuck(qa_q2r_slide *query, qa_q2r_stuck_result *result, qa_error *error)
{
    if (!query || !query->trace || !query->origin || !result) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q2 rerelease stuck query");
        return false;
    }
    qa_move_context move = {.error = error};
    rr_move p = {.move = &move, .generic = query, .origin = query->origin, .bounds = query->bounds};
    *result = rr_fix_stuck(&p, query->origin, query->bounds);
    return !move.failed;
}
