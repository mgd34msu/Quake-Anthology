#include "local.h"
#include "qa/text.h"

static void q3_legs(qa_q3_step *step, int32_t animation, bool force) {
    qa_move_animation(step->context, QA_MOVE_ANIMATION_LEGS, animation, force,
                      (q3_state(step)->movement_flags & Q3_BACKWARDS_JUMP) != 0);
}

static void q3_jump_animation(qa_q3_step *step) {
    qa_q3_movement_state *state = q3_state(step);
    bool backwards = step->context->command.forward_move < 0;
    q3_legs(step, backwards ? Q3_LEGS_JUMPB : Q3_LEGS_JUMP, true);
    if (!q3_active(step)) return;
    if (backwards) state->movement_flags |= Q3_BACKWARDS_JUMP;
    else state->movement_flags &= ~(uint32_t)Q3_BACKWARDS_JUMP;
}

static void q3_view(qa_q3_step *step) {
    int32_t type = q3_type(step);
    if (type == Q3_INTERMISSION || type == Q3_SPINTERMISSION ||
        (type != Q3_SPECTATOR && step->context->input->environment.health <= 0)) return;
    qa_move_q3_view(q3_state(step), &step->context->command);
}

static void q3_friction(qa_q3_step *step) {
    qa_q3_movement_state *state = q3_state(step);
    qa_vec3 velocity = state->velocity, flat = velocity;
    if (step->walking) flat.z = 0;
    float speed = qa_vec_length(flat);
    if (speed < 1) {
        state->velocity = qa_v3(0, 0, velocity.z);
        return;
    }
    float drop = 0;
    if (step->water_level <= 1 && step->walking &&
        !(step->ground_surface_flags & Q3_SURF_SLICK) &&
        !(state->movement_flags & Q3_TIME_KNOCKBACK))
        drop = fmaxf(speed, 100) * 6 * step->dt;
    if (step->water_level) drop += speed * (float)step->water_level * step->dt;
    if (step->context->input->environment.flight) drop += speed * 3 * step->dt;
    if (q3_type(step) == Q3_SPECTATOR) drop += speed * 5 * step->dt;
    state->velocity = qa_vec_scale(velocity, fmaxf(0, speed - drop) / speed);
}

static void q3_accelerate(qa_q3_step *step, qa_vec3 direction, float speed, float acceleration) {
    qa_q3_movement_state *state = q3_state(step);
    float add = speed - qa_vec_dot(state->velocity, direction);
    if (add <= 0) return;
    float amount = fminf(acceleration * step->dt * speed, add);
    state->velocity = qa_vec_add(state->velocity, qa_vec_scale(direction, amount));
}

static float q3_command_scale(qa_q3_step *step) {
    const qa_movement_command *command = &step->context->command;
    float forward = (float)command->forward_move;
    float right = (float)command->side_move;
    float up = (float)command->up_move;
    float maximum = fmaxf(fabsf(forward), fmaxf(fabsf(right), fabsf(up)));
    float total = sqrtf(forward * forward + right * right + up * up);
    return maximum == 0 ? 0 : q3_speed(step) * maximum / (127 * total);
}

static void q3_direction(qa_q3_step *step) {
    qa_q3_movement_state *state = q3_state(step);
    float forward = step->context->command.forward_move;
    float right = step->context->command.side_move;
    if (forward != 0 || right != 0)
        state->movement_direction = forward > 0 ? (right < 0 ? 1 : right > 0 ? 7 : 0) :
            forward < 0 ? (right < 0 ? 3 : right > 0 ? 5 : 4) : right < 0 ? 2 : 6;
    else if (state->movement_direction == 2) state->movement_direction = 1;
    else if (state->movement_direction == 6) state->movement_direction = 7;
}

static bool q3_jump(qa_q3_step *step) {
    qa_q3_movement_state *state = q3_state(step);
    qa_movement_command *command = &step->context->command;
    if ((state->movement_flags & Q3_RESPAWNED) || command->up_move < 10) return false;
    if (state->movement_flags & Q3_JUMP_HELD) {
        command->up_move = 0;
        return false;
    }
    step->ground_plane = false;
    step->walking = false;
    state->movement_flags |= Q3_JUMP_HELD;
    state->ground = (qa_movement_ground){0};
    state->velocity.z = 270;
    if (qa_move_event(step->context, Q3_EV_JUMP, 0)) q3_jump_animation(step);
    return true;
}

static bool q3_water_jump(qa_q3_step *step) {
    qa_q3_movement_state *state = q3_state(step);
    if (state->movement_time_ms != 0 || step->water_level != 2) return false;
    qa_vec3 flat = qa_vec_normalize(qa_v3(step->forward.x, step->forward.y, 0));
    qa_vec3 point = qa_vec_add(state->origin, qa_vec_scale(flat, 30));
    int32_t contents;
    if (!qa_move_contents(step->context, qa_vec_add(point, qa_v3(0, 0, 4)), &contents) ||
        !(contents & 1)) return false;
    if (!qa_move_contents(step->context, qa_vec_add(point, qa_v3(0, 0, 20)), &contents) ||
        contents != 0) return false;
    state->velocity = qa_vec_scale(step->forward, 200);
    state->velocity.z = 350;
    state->movement_flags |= Q3_TIME_WATERJUMP;
    state->movement_time_ms = 2000;
    return true;
}

static void q3_water_jump_move(qa_q3_step *step) {
    q3_step_slide(step, true);
    if (!q3_active(step)) return;
    qa_q3_movement_state *state = q3_state(step);
    state->velocity.z -= q3_gravity(step) * step->dt;
    if (state->velocity.z < 0) {
        state->movement_flags &= ~(uint32_t)Q3_ALL_TIMES;
        state->movement_time_ms = 0;
    }
}

static qa_vec3 q3_wish(qa_q3_step *step, float scale) {
    const qa_movement_command *command = &step->context->command;
    qa_vec3 wish = qa_vec_add(
        qa_vec_scale(qa_vec_scale(step->forward, scale), (float)command->forward_move),
        qa_vec_scale(qa_vec_scale(step->right, scale), (float)command->side_move));
    wish.z += scale * (float)command->up_move;
    return wish;
}

static void q3_water_move(qa_q3_step *step) {
    if (q3_water_jump(step)) {
        q3_water_jump_move(step);
        return;
    }
    if (!q3_active(step)) return;
    q3_friction(step);
    float scale = q3_command_scale(step);
    qa_vec3 wish = scale == 0 ? qa_v3(0, 0, -60) : q3_wish(step, scale);
    q3_accelerate(step, qa_vec_normalize(wish), fminf(qa_vec_length(wish), q3_speed(step) * 0.5f), 4);
    qa_q3_movement_state *state = q3_state(step);
    if (step->ground_plane && qa_vec_dot(state->velocity, step->ground_normal) < 0) {
        float speed = qa_vec_length(state->velocity);
        state->velocity = qa_vec_scale(qa_vec_normalize(q3_clip(state->velocity, step->ground_normal)), speed);
    }
    q3_slide(step, false);
}

static void q3_fly_move(qa_q3_step *step) {
    q3_friction(step);
    qa_vec3 wish = q3_wish(step, q3_command_scale(step));
    q3_accelerate(step, qa_vec_normalize(wish), qa_vec_length(wish), 8);
    q3_step_slide(step, false);
}

static void q3_air_move(qa_q3_step *step) {
    q3_friction(step);
    float scale = q3_command_scale(step);
    q3_direction(step);
    step->forward = qa_vec_normalize(qa_v3(step->forward.x, step->forward.y, 0));
    step->right = qa_vec_normalize(qa_v3(step->right.x, step->right.y, 0));
    qa_vec3 wish = qa_vec_add(qa_vec_scale(step->forward, (float)step->context->command.forward_move),
                              qa_vec_scale(step->right, (float)step->context->command.side_move));
    q3_accelerate(step, qa_vec_normalize(wish), qa_vec_length(wish) * scale, 1);
    qa_q3_movement_state *state = q3_state(step);
    if (step->ground_plane) state->velocity = q3_clip(state->velocity, step->ground_normal);
    q3_step_slide(step, true);
}

static void q3_grapple_move(qa_q3_step *step) {
    qa_q3_movement_state *state = q3_state(step);
    qa_vec3 pull = qa_vec_sub(qa_vec_add(state->grapple_point, qa_vec_scale(step->forward, -16)), state->origin);
    float distance = qa_vec_length(pull);
    state->velocity = qa_vec_scale(qa_vec_normalize(pull), distance <= 100 ? 10 * distance : 800);
    step->ground_plane = false;
}

static void q3_walk_move(qa_q3_step *step) {
    qa_vec3 normal = step->ground_normal;
    if (step->water_level > 2 && qa_vec_dot(step->forward, normal) > 0) {
        q3_water_move(step);
        return;
    }
    if (q3_jump(step)) {
        if (!q3_active(step)) return;
        if (step->water_level > 1) q3_water_move(step);
        else q3_air_move(step);
        return;
    }
    q3_friction(step);
    float scale = q3_command_scale(step);
    q3_direction(step);
    step->forward = qa_vec_normalize(q3_clip(qa_v3(step->forward.x, step->forward.y, 0), normal));
    step->right = qa_vec_normalize(q3_clip(qa_v3(step->right.x, step->right.y, 0), normal));
    qa_vec3 wish = qa_vec_add(qa_vec_scale(step->forward, (float)step->context->command.forward_move),
                              qa_vec_scale(step->right, (float)step->context->command.side_move));
    float wish_speed = qa_vec_length(wish) * scale;
    qa_q3_movement_state *state = q3_state(step);
    if (state->movement_flags & Q3_DUCKED) wish_speed = fminf(wish_speed, q3_speed(step) * 0.25f);
    if (step->water_level) {
        float water_scale = 1 - 0.5f * ((float)step->water_level / 3);
        wish_speed = fminf(wish_speed, q3_speed(step) * water_scale);
    }
    bool sliding = (step->ground_surface_flags & Q3_SURF_SLICK) != 0 ||
                   (state->movement_flags & Q3_TIME_KNOCKBACK) != 0;
    q3_accelerate(step, qa_vec_normalize(wish), wish_speed, sliding ? 1 : 10);
    if (sliding) state->velocity.z -= q3_gravity(step) * step->dt;
    float speed = qa_vec_length(state->velocity);
    state->velocity = qa_vec_scale(qa_vec_normalize(q3_clip(state->velocity, normal)), speed);
    if (state->velocity.x != 0 || state->velocity.y != 0) q3_step_slide(step, false);
}

static void q3_noclip_move(qa_q3_step *step) {
    qa_q3_movement_state *state = q3_state(step);
    state->view_height = step->context->input->standing.view_height;
    float speed = qa_vec_length(state->velocity);
    state->velocity = speed < 1 ? qa_v3(0, 0, 0) : qa_vec_scale(state->velocity,
        fmaxf(0, speed - fmaxf(100, speed) * 9 * step->dt) / speed);
    qa_vec3 wish = q3_wish(step, 1);
    q3_accelerate(step, qa_vec_normalize(wish), qa_vec_length(wish) * q3_command_scale(step), 10);
    state->origin = qa_vec_add(state->origin, qa_vec_scale(state->velocity, step->dt));
}

static int32_t q3_surface_footstep(const qa_q3_step *step) {
    return step->ground_surface_flags & Q3_SURF_NOSTEPS ? 0 :
           step->ground_surface_flags & Q3_SURF_METALSTEPS ? Q3_EV_FOOTSTEP_METAL : Q3_EV_FOOTSTEP;
}

static void q3_crash_land(qa_q3_step *step) {
    qa_q3_movement_state *state = q3_state(step);
    q3_legs(step, state->movement_flags & Q3_BACKWARDS_JUMP ? Q3_LEGS_LANDB : Q3_LEGS_LAND, true);
    if (!q3_active(step) || !qa_move_animation(step->context, QA_MOVE_ANIMATION_LEGS_TIMER, 130, false, false)) return;
    float distance = state->origin.z - step->previous_origin.z;
    float velocity = step->previous_velocity.z, acceleration = -q3_gravity(step);
    float a = acceleration / 2;
    float discriminant = velocity * velocity - (4 * a) * -distance;
    if (discriminant < 0) return;
    float time = (-velocity - sqrtf(discriminant)) / (2 * a);
    float delta = velocity + time * acceleration;
    delta = delta * delta * 0.0001f;
    if (state->movement_flags & Q3_DUCKED) delta *= 2;
    if (step->water_level == 3) return;
    if (step->water_level == 2) delta *= 0.25f;
    if (step->water_level == 1) delta *= 0.5f;
    if (delta < 1) return;
    if (!(step->ground_surface_flags & Q3_SURF_NODAMAGE)) {
        if (delta > 60) qa_move_event(step->context, Q3_EV_FALL_FAR, 0);
        else if (delta > 40) {
            if (step->context->input->environment.health > 0)
                qa_move_event(step->context, Q3_EV_FALL_MEDIUM, 0);
        } else if (delta > 7) qa_move_event(step->context, Q3_EV_FALL_SHORT, 0);
        else qa_move_event(step->context, q3_surface_footstep(step), 0);
    }
    if (q3_active(step)) state->bob_cycle = 0;
}

static void q3_leave_ground(qa_q3_step *step) {
    q3_state(step)->ground = (qa_movement_ground){0};
    step->ground_plane = false;
    step->walking = false;
}

static void q3_ground_trace(qa_q3_step *step) {
    qa_q3_movement_state *state = q3_state(step);
    qa_vec3 down = qa_vec_add(state->origin, qa_v3(0, 0, -0.25f));
    qa_trace_result trace;
    if (!q3_move_trace(step, state->origin, down, &trace)) return;
    if (trace.all_solid) {
        bool corrected = false;
        for (int i = -1; i <= 1 && !corrected; ++i) {
            for (int j = -1; j <= 1 && !corrected; ++j) {
                for (int k = -1; k <= 1; ++k) {
                    qa_vec3 point = qa_vec_add(state->origin, qa_v3((float)i, (float)j, (float)k));
                    qa_trace_result test;
                    if (!q3_move_trace(step, point, point, &test)) return;
                    if (!test.all_solid) {
                        if (!q3_move_trace(step, state->origin, down, &trace)) return;
                        corrected = true;
                        break;
                    }
                }
            }
        }
        if (!corrected) {
            q3_leave_ground(step);
            return;
        }
    }
    step->ground_surface_flags = trace.family == QA_COLLISION_Q3 ? trace.surface_flags :
        trace.family == QA_COLLISION_Q2 && trace.has_surface ? trace.surface.flags : 0;
    if (trace.fraction == 1) {
        if (state->ground.hit != QA_TRACE_HIT_NONE) {
            qa_trace_result farther;
            if (!q3_move_trace(step, state->origin, qa_vec_add(state->origin, qa_v3(0, 0, -64)), &farther)) return;
            if (farther.fraction == 1) q3_jump_animation(step);
        }
        if (q3_active(step)) q3_leave_ground(step);
        return;
    }
    if (!trace.contact) {
        q3_leave_ground(step);
        return;
    }
    qa_vec3 normal = trace.contact_plane.normal;
    if (state->velocity.z > 0 && qa_vec_dot(state->velocity, normal) > 10) {
        q3_jump_animation(step);
        if (q3_active(step)) q3_leave_ground(step);
        return;
    }
    step->ground_normal = normal;
    step->ground_plane = true;
    if (normal.z < 0.7f) {
        state->ground = (qa_movement_ground){0};
        step->walking = false;
        return;
    }
    step->walking = true;
    if (state->movement_flags & Q3_TIME_WATERJUMP) {
        state->movement_flags &= ~(uint32_t)(Q3_TIME_WATERJUMP | Q3_TIME_LAND);
        state->movement_time_ms = 0;
    }
    if (state->ground.hit == QA_TRACE_HIT_NONE) {
        q3_crash_land(step);
        if (!q3_active(step)) return;
        if (step->previous_velocity.z < -200) {
            state->movement_flags |= Q3_TIME_LAND;
            state->movement_time_ms = 250;
        }
    }
    state->ground = qa_move_ground(&trace);
    q3_contact(step, &trace);
}

static qa_bounds q3_standing_bounds(const qa_q3_step *step) {
    return step->context->input->shape.kind == QA_SHAPE_POINT
        ? (qa_bounds){0} : step->context->input->shape.bounds;
}

static void q3_water_level(qa_q3_step *step) {
    step->water_level = step->water_type = 0;
    step->context->result->water_level = step->context->result->water_type = 0;
    qa_q3_movement_state *state = q3_state(step);
    float bottom = q3_standing_bounds(step).mins.z;
    int32_t contents;
    qa_vec3 sample = state->origin;
    sample.z += bottom + 1;
    if (!qa_move_contents(step->context, sample, &contents) || !(contents & Q3_MASK_WATER)) return;
    step->water_type = contents;
    step->water_level = 1;
    step->context->result->water_type = contents;
    step->context->result->water_level = 1;
    float second = state->view_height - bottom;
    float first = truncf(second / 2);
    sample.z = state->origin.z + bottom + first;
    if (!qa_move_contents(step->context, sample, &contents) || !(contents & Q3_MASK_WATER)) return;
    step->water_level = 2;
    step->context->result->water_level = 2;
    sample.z = state->origin.z + bottom + second;
    if (!qa_move_contents(step->context, sample, &contents)) return;
    if (contents & Q3_MASK_WATER) {
        step->water_level = 3;
        step->context->result->water_level = 3;
    }
}

static bool q3_bounds_equal(qa_bounds a, qa_bounds b) {
    return a.mins.x == b.mins.x && a.mins.y == b.mins.y && a.mins.z == b.mins.z &&
           a.maxs.x == b.maxs.x && a.maxs.y == b.maxs.y && a.maxs.z == b.maxs.z;
}

static void q3_duck(qa_q3_step *step) {
    qa_move_context *context = step->context;
    const qa_movement_input *input = context->input;
    const qa_movement_environment *environment = &input->environment;
    qa_q3_movement_state *state = q3_state(step);
    uint32_t previous_duck = state->movement_flags & Q3_DUCKED;
    qa_bounds previous_bounds = context->result->bounds;
    if (environment->invulnerable || environment->fixed_pose) {
        qa_movement_posture pose = environment->fixed_pose ? environment->pose : input->crouched;
        if (!environment->fixed_pose && (state->movement_flags & Q3_INVULEXPAND))
            pose.bounds = input->invulnerability_bounds;
        bool crouched = !environment->fixed_pose || environment->fixed_crouched;
        context->result->bounds = pose.bounds;
        if (crouched) state->movement_flags |= Q3_DUCKED;
        else state->movement_flags &= ~(uint32_t)Q3_DUCKED;
        state->view_height = pose.view_height;
        return;
    }
    state->movement_flags &= ~(uint32_t)Q3_INVULEXPAND;
    if (q3_type(step) == Q3_DEAD) {
        context->result->bounds = input->dead.bounds;
        state->view_height = input->dead.view_height;
        return;
    }
    if (context->command.up_move < 0) state->movement_flags |= Q3_DUCKED;
    else if (state->movement_flags & Q3_DUCKED) {
        context->result->bounds = environment->has_body_bounds ? environment->body_bounds : q3_standing_bounds(step);
        qa_trace_result trace;
        if (!q3_move_trace(step, state->origin, state->origin, &trace)) return;
        if (!trace.all_solid) state->movement_flags &= ~(uint32_t)Q3_DUCKED;
    }
    qa_bounds requested = state->movement_flags & Q3_DUCKED ? input->crouched.bounds : q3_standing_bounds(step);
    state->view_height = state->movement_flags & Q3_DUCKED ? input->crouched.view_height : input->standing.view_height;
    if (environment->has_body_bounds) requested = environment->body_bounds;
    context->result->bounds = previous_bounds;
    qa_bounds accepted;
    if (!qa_move_bounds(context, requested, step->mask, &accepted)) return;
    context->result->bounds = accepted;
    if (!q3_bounds_equal(accepted, requested)) {
        state->movement_flags = (state->movement_flags & ~(uint32_t)Q3_DUCKED) | previous_duck;
        state->view_height = previous_duck ? input->crouched.view_height : input->standing.view_height;
    }
}

static void q3_footsteps(qa_q3_step *step) {
    qa_q3_movement_state *state = q3_state(step);
    qa_movement_command *command = &step->context->command;
    step->horizontal_speed = sqrtf(state->velocity.x * state->velocity.x + state->velocity.y * state->velocity.y);
    if (state->ground.hit == QA_TRACE_HIT_NONE) {
        if (step->context->input->environment.invulnerable) q3_legs(step, Q3_LEGS_IDLECR, false);
        if (q3_active(step) && step->water_level > 1) q3_legs(step, Q3_LEGS_SWIM, false);
        return;
    }
    if (command->forward_move == 0 && command->side_move == 0) {
        if (step->horizontal_speed < 5) {
            state->bob_cycle = 0;
            q3_legs(step, state->movement_flags & Q3_DUCKED ? Q3_LEGS_IDLECR : Q3_LEGS_IDLE, false);
        }
        return;
    }
    float bob;
    bool footstep = false, backwards = (state->movement_flags & Q3_BACKWARDS_RUN) != 0;
    if (state->movement_flags & Q3_DUCKED) {
        bob = 0.5f;
        q3_legs(step, backwards ? Q3_LEGS_BACKCR : Q3_LEGS_WALKCR, false);
    } else if (!(command->buttons & Q3_WALKING)) {
        bob = 0.4f;
        footstep = true;
        q3_legs(step, backwards ? Q3_LEGS_BACK : Q3_LEGS_RUN, false);
    } else {
        bob = 0.3f;
        q3_legs(step, backwards ? Q3_LEGS_BACKWALK : Q3_LEGS_WALK, false);
    }
    if (!q3_active(step)) return;
    int32_t old = state->bob_cycle;
    float cycle = (float)old + bob * (float)step->milliseconds;
    state->bob_cycle = (int32_t)((uint32_t)qa_source_float_to_i32(cycle) & 255u);
    if ((((uint32_t)old + 64) ^ ((uint32_t)state->bob_cycle + 64)) & 128) {
        if (step->water_level == 0 && footstep && !step->context->input->profile.data.q3.no_footsteps)
            qa_move_event(step->context, q3_surface_footstep(step), 0);
        else if (step->water_level == 1) qa_move_event(step->context, Q3_EV_FOOTSPLASH, 0);
        else if (step->water_level == 2) qa_move_event(step->context, Q3_EV_SWIM, 0);
    }
}

static void q3_water_events(qa_q3_step *step, int32_t previous) {
    if (!previous && step->water_level && !qa_move_event(step->context, Q3_EV_WATER_TOUCH, 0)) return;
    if (previous && !step->water_level && !qa_move_event(step->context, Q3_EV_WATER_LEAVE, 0)) return;
    if (previous != 3 && step->water_level == 3 && !qa_move_event(step->context, Q3_EV_WATER_UNDER, 0)) return;
    if (previous == 3 && step->water_level != 3) qa_move_event(step->context, Q3_EV_WATER_CLEAR, 0);
}

static void q3_drop_timers(qa_q3_step *step) {
    qa_q3_movement_state *state = q3_state(step);
    if (state->movement_time_ms) {
        if ((int64_t)step->milliseconds >= state->movement_time_ms) {
            state->movement_flags &= ~(uint32_t)Q3_ALL_TIMES;
            state->movement_time_ms = 0;
        } else state->movement_time_ms -= (int32_t)step->milliseconds;
    }
    qa_move_phase(step->context, QA_MOVE_DROP_TIMERS);
}

static float q3_snap(float value) {
    float lower = floorf(value), fraction = value - lower;
    if (fraction < 0.5f) return lower;
    if (fraction > 0.5f) return lower + 1;
    return fmodf(lower, 2) == 0 ? lower : lower + 1;
}

static void q3_run_step(qa_q3_step *step) {
    qa_move_context *context = step->context;
    qa_q3_movement_state *state = q3_state(step);
    qa_movement_command *command = &context->command;
    if (command->forward_move > 64 || command->forward_move < -64 ||
        command->side_move > 64 || command->side_move < -64) command->buttons &= ~(uint32_t)Q3_WALKING;
    if (command->buttons & Q3_TALK) state->flags |= 0x1000;
    else state->flags &= ~UINT32_C(0x1000);
    if (!(state->movement_flags & Q3_RESPAWNED) && q3_type(step) != Q3_INTERMISSION &&
        (command->buttons & Q3_ATTACK) && qa_move_firing(context)) state->flags |= 0x100;
    else state->flags &= ~UINT32_C(0x100);
    if (!q3_active(step)) return;
    if (context->input->environment.health > 0 && !(command->buttons & (Q3_ATTACK | Q3_USE_HOLDABLE)))
        state->movement_flags &= ~(uint32_t)Q3_RESPAWNED;
    if (command->buttons & Q3_TALK) {
        command->buttons = Q3_TALK;
        command->forward_move = command->side_move = command->up_move = 0;
    }
    state->command_time_ms = command->server_time_ms;
    q3_view(step);
    qa_move_angles(state->view_angles, &step->forward, &step->right, NULL);
    if (command->up_move < 10) state->movement_flags &= ~(uint32_t)Q3_JUMP_HELD;
    if (command->forward_move < 0) state->movement_flags |= Q3_BACKWARDS_RUN;
    else if (command->forward_move > 0 || (command->forward_move == 0 && command->side_move != 0))
        state->movement_flags &= ~(uint32_t)Q3_BACKWARDS_RUN;
    if (q3_type(step) >= Q3_DEAD) command->forward_move = command->side_move = command->up_move = 0;
    if (q3_type(step) == Q3_SPECTATOR) {
        q3_duck(step);
        if (!q3_active(step)) return;
        q3_fly_move(step);
        if (q3_active(step)) q3_drop_timers(step);
        return;
    }
    if (q3_type(step) == Q3_NOCLIP) {
        q3_noclip_move(step);
        q3_drop_timers(step);
        return;
    }
    if (q3_type(step) == Q3_FREEZE || q3_type(step) == Q3_INTERMISSION || q3_type(step) == Q3_SPINTERMISSION) return;
    q3_water_level(step);
    if (!q3_active(step)) return;
    int32_t previous_water_level = step->water_level;
    q3_duck(step);
    if (!q3_active(step)) return;
    q3_ground_trace(step);
    if (!q3_active(step)) return;
    if (q3_type(step) == Q3_DEAD && step->walking) {
        float speed = qa_vec_length(state->velocity) - 20;
        state->velocity = speed <= 0 ? qa_v3(0, 0, 0) : qa_vec_scale(qa_vec_normalize(state->velocity), speed);
    }
    q3_drop_timers(step);
    if (!q3_active(step)) return;
    if (context->input->environment.fixed_pose ||
        (context->input->profile.data.q3.missionpack && context->input->environment.invulnerable)) {
        command->forward_move = command->side_move = command->up_move = 0;
        state->velocity = qa_v3(0, 0, 0);
    } else if (context->input->environment.flight) q3_fly_move(step);
    else if (state->movement_flags & Q3_GRAPPLE_PULL) {
        q3_grapple_move(step);
        q3_air_move(step);
    } else if (state->movement_flags & Q3_TIME_WATERJUMP) q3_water_jump_move(step);
    else if (step->water_level > 1) q3_water_move(step);
    else if (step->walking) q3_walk_move(step);
    else q3_air_move(step);
    if (!q3_active(step) || !qa_move_phase(context, QA_MOVE_GESTURE)) return;
    q3_ground_trace(step);
    if (!q3_active(step)) return;
    q3_water_level(step);
    if (!q3_active(step) || !qa_move_phase(context, QA_MOVE_WEAPON)) return;
    if (!qa_move_phase(context, QA_MOVE_TORSO)) return;
    q3_footsteps(step);
    if (!q3_active(step)) return;
    q3_water_events(step, previous_water_level);
    if (!q3_active(step)) return;
    state->velocity = qa_v3(q3_snap(state->velocity.x), q3_snap(state->velocity.y), q3_snap(state->velocity.z));
}

static uint32_t q3_elapsed(int32_t command, int32_t previous) {
    int64_t elapsed = (int64_t)command - previous;
    return elapsed < 1 ? 1 : elapsed > 200 ? 200 : (uint32_t)elapsed;
}

bool qa_move_q3(qa_move_context *context) {
    qa_q3_movement_state *state = &context->state->data.q3;
    qa_movement_result *result = context->result;
    int32_t final_time = context->command.server_time_ms;
    uint32_t fixed = context->input->profile.data.q3.fixed_ms;
    if (fixed == 0) fixed = 66;
    if (fixed > INT32_MAX || context->command.forward_move < -128 || context->command.forward_move > 127 ||
        context->command.side_move < -128 || context->command.side_move > 127 ||
        context->command.up_move < -128 || context->command.up_move > 127 ||
        context->command.forward_move != truncf(context->command.forward_move) ||
        context->command.side_move != truncf(context->command.side_move) ||
        context->command.up_move != truncf(context->command.up_move)) {
        qa_error_set(context->error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 movement command or fixed subdivision");
        context->failed = true;
        return false;
    }
    result->bounds = (qa_bounds){0};
    result->contact_count = 0;
    result->water_level = result->water_type = 0;
    result->horizontal_speed = 0;
    result->view_height = state->view_height;
    result->view_angles = state->view_angles;
    result->ground = state->ground;
    if (final_time < state->command_time_ms) return true;
    if ((int64_t)final_time > (int64_t)state->command_time_ms + 1000)
        state->command_time_ms = final_time - 1000;
    state->movement_frame = (int32_t)(((uint32_t)state->movement_frame + 1) & 63);
    uint32_t substep = 0;
    while (state->command_time_ms != final_time) {
        int64_t remaining = (int64_t)final_time - state->command_time_ms;
        int64_t next = (int64_t)state->command_time_ms + (remaining < fixed ? remaining : fixed);
        context->command.server_time_ms = (int32_t)next;
        context->milliseconds = q3_elapsed(context->command.server_time_ms, state->command_time_ms);
        context->dt = (float)context->milliseconds * 0.001f;
        context->time_ns = context->command.server_time_ms < 0 ? 0 :
            (uint64_t)context->command.server_time_ms * UINT64_C(1000000);
        context->substep = substep++;
        if (!qa_move_phase(context, QA_MOVE_INPUT_BEGIN)) break;
        if (!qa_move_apply_stance(context)) break;
        qa_q3_step step = { .context = context,
            .previous_origin = state->origin, .previous_velocity = state->velocity,
            .milliseconds = q3_elapsed(context->command.server_time_ms, state->command_time_ms) };
        step.dt = (float)step.milliseconds * 0.001f;
        step.mask = context->input->has_trace_policy ? context->input->trace_policy.contents_mask :
            UINT32_C(0x10001) | (context->input->state.data.q3.movement_type == Q3_SPECTATOR ? 0 : (uint32_t)Q3_CONTENTS_BODY);
        if (context->input->environment.health <= 0) step.mask &= ~(uint32_t)Q3_CONTENTS_BODY;
        if (substep == 1) result->bounds = context->input->has_current_bounds
            ? context->input->current_bounds : q3_standing_bounds(&step);
        result->contact_count = 0;
        q3_run_step(&step);
        result->water_level = step.water_level;
        result->water_type = step.water_type;
        result->horizontal_speed = step.horizontal_speed;
        result->view_height = state->view_height;
        result->view_angles = state->view_angles;
        result->ground = state->ground;
        if (context->failed) break;
        if (!qa_move_phase(context, QA_MOVE_INPUT_END)) break;
        if (state->movement_flags & Q3_JUMP_HELD) context->command.up_move = 20;
    }
    result->view_height = state->view_height;
    result->view_angles = state->view_angles;
    result->ground = state->ground;
    return !context->failed;
}
