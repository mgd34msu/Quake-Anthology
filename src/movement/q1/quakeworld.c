#include "common.h"
#include <stdlib.h>

typedef struct qw_move {
    q1_move base;
    qa_vec3 forward, right;
    int32_t water_level, water_type;
    bool shared_controls, posture_published;
    qa_movement_ground *touched;
    size_t touched_count, touched_capacity;
} qw_move;

static bool qw_bounds_equal(qa_bounds a, qa_bounds b) {
    return a.mins.x == b.mins.x && a.mins.y == b.mins.y && a.mins.z == b.mins.z &&
        a.maxs.x == b.maxs.x && a.maxs.y == b.maxs.y && a.maxs.z == b.maxs.z;
}

static bool qw_posture(qw_move *m) {
    qa_move_context *c = m->base.c;
    const qa_movement_environment *e = &c->input->environment;
    qa_bounds previous = c->result->bounds, standing = c->input->standing.bounds;
    float up_move = c->command.up_move;
    bool wants_crouch = e->has_stance ? e->crouched : up_move < 0;
    bool crouched = !e->flight && wants_crouch;
    qa_trace_shape shape = q1_shape(&m->base);
    qa_vec3 origin = c->state->data.qw.origin;
    qa_trace_result trace;
    if (!crouched && previous.maxs.z < standing.maxs.z) {
        shape.bounds = standing;
        if (!qa_move_trace_q1(c, origin, origin, shape, QA_Q1_MOVE_NORMAL, &trace)) return false;
        crouched = trace.start_solid || trace.all_solid;
    }
    qa_bounds requested = e->has_body_bounds ? e->body_bounds : crouched ? c->input->crouched.bounds : standing;
    bool expands = requested.mins.x < previous.mins.x || requested.mins.y < previous.mins.y ||
        requested.mins.z < previous.mins.z || requested.maxs.x > previous.maxs.x ||
        requested.maxs.y > previous.maxs.y || requested.maxs.z > previous.maxs.z;
    if (expands) {
        shape.bounds = requested;
        if (!qa_move_trace_q1(c, origin, origin, shape, QA_Q1_MOVE_NORMAL, &trace)) return false;
        if (trace.start_solid || trace.all_solid) {
            requested = previous;
            crouched = previous.maxs.z < standing.maxs.z;
        }
    }
    float height = crouched ? c->input->crouched.view_height : c->input->standing.view_height;
    bool publish = !m->posture_published || !qw_bounds_equal(previous, requested) || height != c->result->view_height;
    m->base.crouch_animation = crouched;
    c->result->bounds = requested;
    c->result->view_height = height;
    if (publish) {
        m->posture_published = true;
        return q1_phase(&m->base, QA_MOVE_POSTURE);
    }
    return true;
}

static bool qw_fly(qw_move *m) {
    q1_fly_result result;
    return q1_fly(&m->base, m->base.c->dt, &result);
}

static bool qw_ground_move(qw_move *m) {
    qa_move_context *c = m->base.c;
    qa_qw_movement_state *s = &c->state->data.qw;
    s->velocity.z = 0;
    if (s->velocity.x == 0 && s->velocity.y == 0) return true;
    qa_vec3 destination = q1_ma(s->origin, c->dt, s->velocity);
    qa_trace_result trace;
    if (!q1_trace(&m->base, s->origin, destination, &trace)) return false;
    if (trace.fraction == 1) { s->origin = trace.end; return true; }
    qa_vec3 original = s->origin, original_velocity = s->velocity;
    if (!qw_fly(m)) return false;
    qa_vec3 down = s->origin, down_velocity = s->velocity;
    s->origin = original;
    s->velocity = original_velocity;
    if (!q1_trace(&m->base, s->origin, qa_vec_add(s->origin, qa_v3(0, 0, 18)), &trace)) return false;
    if (!trace.start_solid && !trace.all_solid) s->origin = trace.end;
    if (!qw_fly(m)) return false;
    if (!q1_trace(&m->base, s->origin, qa_vec_add(s->origin, qa_v3(0, 0, -18)), &trace)) return false;
    bool use_down = trace.plane.normal.z < 0.7f;
    if (!use_down) {
        if (!trace.start_solid && !trace.all_solid) s->origin = trace.end;
        qa_vec3 down_delta = qa_vec_sub(down, original), up_delta = qa_vec_sub(s->origin, original);
        float down_distance = down_delta.x * down_delta.x + down_delta.y * down_delta.y;
        float up_distance = up_delta.x * up_delta.x + up_delta.y * up_delta.y;
        use_down = down_distance > up_distance;
    }
    if (use_down) { s->origin = down; s->velocity = down_velocity; }
    else s->velocity.z = down_velocity.z;
    return true;
}

static bool qw_friction(qw_move *m) {
    qa_move_context *c = m->base.c;
    qa_qw_movement_state *s = &c->state->data.qw;
    const qa_q1_movement_parameters *p = &c->input->profile.data.qw.parameters;
    if (s->water_jump_time_seconds != 0) return true;
    float speed = qa_vec_length(s->velocity);
    if (speed < 1) { s->velocity.x = 0; s->velocity.y = 0; return true; }
    float friction = p->friction;
    if (s->ground.hit != QA_TRACE_HIT_NONE) {
        qa_vec3 start = qa_v3(s->origin.x + s->velocity.x / speed * 16,
                               s->origin.y + s->velocity.y / speed * 16,
                               s->origin.z + c->result->bounds.mins.z);
        qa_trace_result trace;
        if (!q1_trace(&m->base, start, qa_vec_add(start, qa_v3(0, 0, -34)), &trace)) return false;
        if (trace.fraction == 1) friction *= 2;
    }
    float drop = 0;
    if (m->water_level >= 2) drop = speed * p->water_friction * (float)m->water_level * c->dt;
    else if (s->ground.hit != QA_TRACE_HIT_NONE) drop = fmaxf(speed, p->stop_speed) * friction * c->dt;
    s->velocity = qa_vec_scale(s->velocity, fmaxf(0, speed - drop) / speed);
    return true;
}

static void qw_accelerate(qw_move *m, qa_vec3 direction, float speed, float acceleration, bool air) {
    qa_qw_movement_state *s = &m->base.c->state->data.qw;
    if (s->dead || s->water_jump_time_seconds != 0) return;
    float add = (air ? fminf(speed, 30) : speed) - qa_vec_dot(s->velocity, direction);
    if (add <= 0) return;
    float amount = fminf(add, air ? acceleration * speed * m->base.c->dt : acceleration * m->base.c->dt * speed);
    s->velocity = q1_ma(s->velocity, amount, direction);
}

static qa_vec3 qw_wish_velocity(const qw_move *m) {
    const qa_movement_command *command = &m->base.c->command;
    return qa_vec_add(qa_vec_scale(m->forward, q1_speed(&m->base, (float)command->forward_move)),
                        qa_vec_scale(m->right, q1_speed(&m->base, (float)command->side_move)));
}

static bool qw_water_move(qw_move *m) {
    qa_move_context *c = m->base.c;
    qa_qw_movement_state *s = &c->state->data.qw;
    const qa_q1_movement_parameters *p = &c->input->profile.data.qw.parameters;
    qa_vec3 wish = qw_wish_velocity(m);
    wish.z += c->command.forward_move == 0 && c->command.side_move == 0 && c->command.up_move == 0 ?
        -60 : q1_speed(&m->base, (float)c->command.up_move);
    float speed = fminf(qa_vec_length(wish), q1_speed(&m->base, p->max_speed)) * 0.7f;
    qw_accelerate(m, qa_vec_normalize(wish), speed, p->water_accelerate, false);
    qa_vec3 destination = q1_ma(s->origin, c->dt, s->velocity);
    qa_vec3 start = qa_vec_add(destination, qa_v3(0, 0, 19));
    qa_trace_result trace;
    if (!q1_trace(&m->base, start, destination, &trace)) return false;
    if (!trace.start_solid && !trace.all_solid) { s->origin = trace.end; return true; }
    return qw_fly(m);
}

static bool qw_air_move(qw_move *m) {
    qa_move_context *c = m->base.c;
    qa_qw_movement_state *s = &c->state->data.qw;
    const qa_q1_movement_parameters *p = &c->input->profile.data.qw.parameters;
    m->forward.z = 0;
    m->right.z = 0;
    m->forward = qa_vec_normalize(m->forward);
    m->right = qa_vec_normalize(m->right);
    qa_vec3 wish = qw_wish_velocity(m);
    float speed = fminf(qa_vec_length(wish), q1_speed(&m->base, p->max_speed));
    float gravity = p->entity_gravity * c->input->environment.gravity_multiplier * p->gravity * c->dt;
    if (s->ground.hit != QA_TRACE_HIT_NONE) {
        s->velocity.z = 0;
        qw_accelerate(m, qa_vec_normalize(wish), speed, p->accelerate, false);
        s->velocity.z -= gravity;
        return qw_ground_move(m);
    }
    /* Source transmits air_accelerate, but PM_AirMove uses accelerate. */
    qw_accelerate(m, qa_vec_normalize(wish), speed, p->accelerate, true);
    s->velocity.z -= gravity;
    return qw_fly(m);
}

static bool qw_categorize(qw_move *m) {
    qa_move_context *c = m->base.c;
    qa_qw_movement_state *s = &c->state->data.qw;
    if (s->velocity.z > 180) s->ground = q1_no_ground();
    else {
        qa_trace_result trace;
        if (!q1_trace(&m->base, s->origin, qa_vec_add(s->origin, qa_v3(0, 0, -1)), &trace)) return false;
        s->ground = trace.plane.normal.z < 0.7f ? q1_no_ground() : qa_move_ground(&trace);
        if (s->ground.hit != QA_TRACE_HIT_NONE) {
            s->water_jump_time_seconds = 0;
            if (!trace.start_solid && !trace.all_solid) s->origin = trace.end;
        }
        if (trace.hit == QA_TRACE_HIT_ACTOR && !qa_move_contact(c, &trace, false, false)) return false;
    }
    m->water_level = 0;
    m->water_type = Q1_CONTENTS_EMPTY;
    qa_vec3 point = s->origin;
    point.z += c->result->bounds.mins.z + 1;
    int32_t contents;
    if (!q1_contents(&m->base, point, &contents)) return false;
    if (contents > Q1_CONTENTS_WATER) return true;
    m->water_type = contents;
    m->water_level = 1;
    point.z = s->origin.z + (c->result->bounds.mins.z + c->result->bounds.maxs.z) * 0.5f;
    if (!q1_contents(&m->base, point, &contents)) return false;
    if (contents > Q1_CONTENTS_WATER) return true;
    m->water_level = 2;
    point.z = s->origin.z + c->result->view_height;
    if (!q1_contents(&m->base, point, &contents)) return false;
    if (contents <= Q1_CONTENTS_WATER) m->water_level = 3;
    return true;
}

static void qw_jump(qw_move *m) {
    qa_move_context *c = m->base.c;
    qa_qw_movement_state *s = &c->state->data.qw;
    if (s->dead) { s->old_buttons |= 2; return; }
    if (s->water_jump_time_seconds != 0) {
        s->water_jump_time_seconds = fmaxf(0, s->water_jump_time_seconds - c->dt);
        return;
    }
    if (m->water_level >= 2) {
        s->ground = q1_no_ground();
        s->velocity.z = m->water_type == Q1_CONTENTS_WATER ? 100 : m->water_type == Q1_CONTENTS_SLIME ? 80 : 50;
        return;
    }
    if (s->ground.hit == QA_TRACE_HIT_NONE || (s->old_buttons & 2)) return;
    s->ground = q1_no_ground();
    s->velocity.z += 270;
    s->old_buttons |= 2;
}

static bool qw_check_water_jump(qw_move *m) {
    qa_qw_movement_state *s = &m->base.c->state->data.qw;
    if (s->water_jump_time_seconds != 0 || s->velocity.z < -180) return true;
    qa_vec3 forward = qa_vec_normalize(qa_v3(m->forward.x, m->forward.y, 0));
    qa_vec3 spot = qa_vec_add(q1_ma(s->origin, 24, forward), qa_v3(0, 0, 8));
    int32_t contents;
    if (!q1_contents(&m->base, spot, &contents)) return false;
    if (contents != Q1_CONTENTS_SOLID) return true;
    if (!q1_contents(&m->base, qa_vec_add(spot, qa_v3(0, 0, 24)), &contents)) return false;
    if (contents != Q1_CONTENTS_EMPTY) return true;
    s->velocity = qa_vec_scale(forward, 50);
    s->velocity.z = 310;
    s->water_jump_time_seconds = 2;
    s->old_buttons |= 2;
    return true;
}

static bool qw_nudge(qw_move *m) {
    const int offsets[3] = {0, -1, 1};
    qa_qw_movement_state *s = &m->base.c->state->data.qw;
    qa_vec3 base = s->origin;
    for (unsigned z = 0; z < 3; ++z) {
        for (unsigned x = 0; x < 3; ++x) {
            for (unsigned y = 0; y < 3; ++y) {
                s->origin = qa_vec_add(base, qa_v3((float)offsets[x] / 8, (float)offsets[y] / 8, (float)offsets[z] / 8));
                bool free_position;
                if (!q1_position_free(&m->base, s->origin, &free_position)) return false;
                if (free_position) return true;
            }
        }
    }
    s->origin = base;
    return true;
}

static bool qw_spectator_move(qw_move *m, bool collide) {
    qa_move_context *c = m->base.c;
    qa_qw_movement_state *s = &c->state->data.qw;
    const qa_q1_movement_parameters *p = &c->input->profile.data.qw.parameters;
    float speed = qa_vec_length(s->velocity);
    if (speed < 1) s->velocity = qa_v3(0, 0, 0);
    else {
        float drop = fmaxf(speed, p->stop_speed) * (p->friction * 1.5f) * c->dt;
        s->velocity = qa_vec_scale(s->velocity, fmaxf(0, speed - drop) / speed);
    }
    m->forward = qa_vec_normalize(m->forward);
    m->right = qa_vec_normalize(m->right);
    qa_vec3 wish = qw_wish_velocity(m);
    wish.z += q1_speed(&m->base, (float)c->command.up_move);
    qa_vec3 direction = qa_vec_normalize(wish);
    float maximum = q1_speed(&m->base, collide ? p->max_speed : p->spectator_max_speed);
    float wish_speed = fminf(qa_vec_length(wish), maximum);
    float add = wish_speed - qa_vec_dot(s->velocity, direction);
    /* Original SpectatorMove skips integration at this return. */
    if (add <= 0 && !collide) return true;
    float acceleration = fmaxf(0, fminf(add, p->accelerate * c->dt * wish_speed));
    s->velocity = q1_ma(s->velocity, acceleration, direction);
    if (collide) return qw_fly(m);
    s->origin = q1_ma(s->origin, c->dt, s->velocity);
    return true;
}

static bool qw_touch_once(qw_move *m, const qa_trace_result *trace) {
    if (trace->hit == QA_TRACE_HIT_NONE) return true;
    qa_movement_ground hit = qa_move_ground(trace);
    for (size_t i = 0; i < m->touched_count; ++i) {
        if (qa_move_same_ground(m->touched[i], hit)) return true;
    }
    if (m->touched_count == m->touched_capacity) {
        size_t capacity = m->touched_capacity ? m->touched_capacity * 2 : 16;
        if (capacity < m->touched_capacity || capacity > SIZE_MAX / sizeof(*m->touched)) {
            qa_error_set(m->base.c->error, QA_ERROR_MEMORY, 0, "QuakeWorld touched-target list is too large");
            m->base.c->failed = true;
            return false;
        }
        qa_movement_ground *touched = realloc(m->touched, capacity * sizeof(*touched));
        if (!touched) {
            qa_error_set(m->base.c->error, QA_ERROR_MEMORY, 0, "Allocating QuakeWorld touched-target list");
            m->base.c->failed = true;
            return false;
        }
        m->touched = touched;
        m->touched_capacity = capacity;
    }
    m->touched[m->touched_count++] = hit;
    return q1_touch(&m->base, trace, false);
}

static bool qw_step_physics(qw_move *m) {
    qa_move_context *c = m->base.c;
    const qa_movement_environment *e = &c->input->environment;
    c->milliseconds = c->command.milliseconds;
    c->dt = (float)c->milliseconds * 0.001f;
    if (!q1_phase(&m->base, QA_MOVE_PRETHINK)) return false;
    if (m->shared_controls) {
        if (!qw_posture(m) || !q1_body_shape(&m->base)) return false;
    }
    if (!qa_move_apply_stance(c)) return false;
    qa_qw_movement_state *s = &c->state->data.qw;
    if (c->input->prediction) s->angles = c->command.angles;
    qa_move_angles(s->angles, &m->forward, &m->right, NULL);
    if (e->has_mode && e->health > 0 && e->mode == QA_MOVEMENT_MODE_FREEZE) {
        s->velocity = qa_v3(0, 0, 0);
        return true;
    }
    if (s->spectator != 0) return qw_spectator_move(m, false);
    size_t contact_start = c->result->contact_count;
    if (!e->fixed_pose && !qw_nudge(m)) return false;
    s->angles = c->command.angles;
    if (!qw_categorize(m)) return false;
    if (e->fixed_pose) s->velocity = qa_v3(0, 0, 0);
    else if (e->flight && e->health > 0) {
        s->ground = q1_no_ground();
        s->water_jump_time_seconds = 0;
        if (!qw_spectator_move(m, true)) return false;
    } else {
        if (m->water_level == 2 && !qw_check_water_jump(m)) return false;
        if (s->velocity.z < 0) s->water_jump_time_seconds = 0;
        if (c->command.buttons & 2) qw_jump(m);
        else s->old_buttons &= ~UINT32_C(2);
        if (!qw_friction(m)) return false;
        if (m->water_level >= 2) { if (!qw_water_move(m)) return false; }
        else if (!qw_air_move(m)) return false;
    }
    if (!qw_categorize(m)) return false;
    c->result->water_level = m->water_level;
    c->result->water_type = m->water_type;
    if (!q1_phase(&m->base, QA_MOVE_LINK_TRIGGERS)) return false;
    size_t contact_end = c->result->contact_count;
    for (size_t i = contact_start; i < contact_end; ++i) {
        qa_trace_result trace = c->result->contacts[i].trace;
        if (!qw_touch_once(m, &trace)) return false;
    }
    return true;
}

static bool qw_step(qw_move *m, qa_movement_command command) {
    qa_move_context *c = m->base.c;
    c->command = command;
    c->milliseconds = command.milliseconds;
    c->dt = (float)command.milliseconds * 0.001f;
    bool proceed = true;
    if (!c->input->prediction) {
        q1_restore_mode(&m->base);
        proceed = qa_move_phase(c, QA_MOVE_INPUT_BEGIN);
        if (proceed) q1_resume(&m->base);
    }
    if (proceed) qw_step_physics(m);
    if (!c->input->prediction) {
        if (!c->removed) q1_restore_mode(&m->base);
        if (qa_move_phase(c, c->failed ? QA_MOVE_INPUT_ABORT : QA_MOVE_INPUT_END)) q1_resume(&m->base);
    }
    ++c->substep;
    return !q1_stopped(&m->base);
}

static bool qw_run_command(qw_move *m, qa_movement_command command, uint32_t maximum) {
    if (command.milliseconds > maximum) {
        command.milliseconds /= 2;
        if (!qw_run_command(m, command, maximum)) return false;
        command.impulse = 0;
        return qw_run_command(m, command, maximum);
    }
    return qw_step(m, command);
}

bool qa_move_qw(qa_move_context *c) {
    uint32_t maximum = c->input->profile.data.qw.maximum_command_ms;
    if (!maximum || c->command.milliseconds > 255) {
        qa_error_set(c->error, QA_ERROR_ARGUMENT, 0,
                     "QuakeWorld requires a positive command interval and byte-sized milliseconds");
        c->failed = true;
        return false;
    }
    qa_movement_command command = c->command;
    float dt = c->dt;
    uint32_t milliseconds = c->milliseconds;
    qw_move move = { .water_type = Q1_CONTENTS_EMPTY };
    c->result->bounds = c->input->shape.kind == QA_SHAPE_POINT ? (qa_bounds){0} : c->input->shape.bounds;
    q1_init(&move.base, c, true);
    move.shared_controls = c->input->profile.data.qw.shared_controls && !c->input->environment.fixed_pose &&
        !c->state->data.qw.dead && c->state->data.qw.spectator == 0;
    if (move.shared_controls) {
        move.base.defer_body_shape = true;
        move.base.force_box = c->input->shape.kind != QA_SHAPE_CAPSULE;
        c->result->bounds = c->input->has_current_bounds ? c->input->current_bounds :
            c->input->shape.kind == QA_SHAPE_POINT ? c->input->standing.bounds : c->input->shape.bounds;
        if (!qw_posture(&move)) return !c->failed;
        move.base.has_starting_bounds = true;
        move.base.starting_bounds = c->result->bounds;
        if (command.up_move > 0) command.buttons |= 2;
    }
    qw_run_command(&move, command, maximum);
    c->command = command;
    c->dt = dt;
    c->milliseconds = milliseconds;
    if (!q1_stopped(&move.base) && q1_phase(&move.base, QA_MOVE_POSTTHINK))
        q1_finish(&move.base, c->state->data.qw.ground, move.water_level, move.water_type);
    if (!c->removed) q1_restore_mode(&move.base);
    free(move.touched);
    return !c->failed;
}
