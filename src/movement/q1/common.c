#include "common.h"

static int32_t *q1_mode(q1_move *m) {
    return m->qw ? &m->c->state->data.qw.spectator : &m->c->state->data.nq.move_type;
}

static void q1_project(q1_move *m) {
    const qa_movement_environment *e = &m->c->input->environment;
    if (e->has_mode && e->health > 0) {
        m->projected = true;
        *q1_mode(m) = qa_move_mode_type(m->c->state->kind, e->mode);
    }
}

static void q1_flight(q1_move *m) {
    const qa_movement_environment *e = &m->c->input->environment;
    if (!m->qw && e->flight && e->health > 0 && *q1_mode(m) == Q1_MOVE_WALK)
        *q1_mode(m) = Q1_MOVE_FLY;
}

void q1_init(q1_move *m, qa_move_context *c, bool qw) {
    *m = (q1_move){ .c = c, .qw = qw };
    m->source_mode = *q1_mode(m);
    m->has_starting_bounds = c->input->has_current_bounds;
    m->starting_bounds = c->input->current_bounds;
    const qa_movement_environment *e = &c->input->environment;
    if (e->fixed_pose) {
        c->result->bounds = e->pose.bounds;
        c->result->view_height = e->pose.view_height;
    }
    if (!qw) { q1_flight(m); q1_project(m); }
}

void q1_restore_mode(q1_move *m) {
    if (m->projected) *q1_mode(m) = m->source_mode;
}

void q1_resume(q1_move *m) {
    if (q1_stopped(m)) return;
    m->source_mode = *q1_mode(m);
    q1_project(m);
    q1_flight(m);
}

bool q1_phase(q1_move *m, qa_movement_phase phase) {
    if (q1_stopped(m)) return false;
    q1_restore_mode(m);
    if (!qa_move_phase(m->c, phase)) return false;
    q1_resume(m);
    if (phase == QA_MOVE_PRETHINK && !m->defer_body_shape) return q1_body_shape(m);
    return true;
}

bool q1_body_shape(q1_move *m) {
    const qa_movement_environment *e = &m->c->input->environment;
    if (e->fixed_pose) {
        m->owned_bounds = false;
        m->c->result->bounds = e->pose.bounds;
        m->c->result->view_height = e->pose.view_height;
    } else if (e->has_body_bounds) {
        qa_trace_shape shape = q1_shape(m);
        if (shape.kind == QA_SHAPE_POINT) {
            qa_error_set(m->c->error, QA_ERROR_ARGUMENT, 0,
                         "Q1 player body output requires a collision hull");
            m->c->failed = true;
            return false;
        }
        qa_bounds previous = m->owned_bounds ? m->body_bounds :
            m->has_starting_bounds ? m->starting_bounds : m->c->result->bounds;
        qa_bounds requested = e->body_bounds;
        bool expands = requested.mins.x < previous.mins.x || requested.mins.y < previous.mins.y ||
            requested.mins.z < previous.mins.z || requested.maxs.x > previous.maxs.x ||
            requested.maxs.y > previous.maxs.y || requested.maxs.z > previous.maxs.z;
        if (expands) {
            qa_vec3 origin = qa_movement_origin(m->c->state);
            qa_trace_result trace;
            shape.bounds = requested;
            if (!qa_move_trace_q1(m->c, origin, origin, shape, QA_Q1_MOVE_NORMAL, &trace)) return false;
            if (trace.start_solid || trace.all_solid) requested = previous;
        }
        m->c->result->bounds = m->body_bounds = requested;
        m->owned_bounds = true;
        return q1_phase(m, QA_MOVE_POSTURE);
    } else m->owned_bounds = false;
    return true;
}

bool q1_touch(q1_move *m, const qa_trace_result *trace, bool record) {
    if (q1_stopped(m)) return false;
    q1_restore_mode(m);
    bool ok = record ? qa_move_contact(m->c, trace, true, false) : qa_move_touch(m->c, trace);
    q1_resume(m);
    return ok;
}

static bool q1_effect(q1_move *m, qa_movement_effect effect) {
    if (q1_stopped(m)) return false;
    q1_restore_mode(m);
    bool ok = qa_move_emit(m->c, effect);
    q1_resume(m);
    return ok;
}

bool q1_move_sound(q1_move *m, const char *sound) {
    return q1_effect(m, (qa_movement_effect){ .kind = QA_MOVE_EFFECT_SOUND, .sound = sound });
}

bool q1_action(q1_move *m, qa_movement_locomotion action) {
    return q1_effect(m, (qa_movement_effect){ .kind = QA_MOVE_EFFECT_PLAYER_ACTION, .value = (int32_t)action });
}

qa_trace_shape q1_shape(const q1_move *m) {
    qa_trace_shape shape = m->c->input->shape;
    if (m->force_box || m->c->input->environment.fixed_pose) shape.kind = QA_SHAPE_BOX;
    shape.bounds = m->c->result->bounds;
    return shape;
}

bool q1_move_trace(q1_move *m, qa_vec3 start, qa_vec3 end, qa_trace_result *trace) {
    return qa_move_trace_q1(m->c, start, end, q1_shape(m), QA_Q1_MOVE_NORMAL, trace);
}

bool q1_point_trace(q1_move *m, qa_vec3 start, qa_vec3 end, qa_trace_result *trace) {
    return qa_move_trace_q1(m->c, start, end, (qa_trace_shape){ .kind = QA_SHAPE_POINT },
                            QA_Q1_MOVE_NO_MONSTERS, trace);
}

bool q1_contents(q1_move *m, qa_vec3 point, int32_t *contents) {
    return qa_move_contents(m->c, point, contents);
}

bool q1_position_free(q1_move *m, qa_vec3 origin, bool *free_position) {
    qa_trace_result trace;
    if (!q1_move_trace(m, origin, origin, &trace)) return false;
    *free_position = !trace.start_solid && !trace.all_solid;
    return true;
}

float q1_speed(const q1_move *m, float speed) {
    return speed * m->c->input->environment.speed_multiplier;
}

bool q1_fly(q1_move *m, double dt, q1_fly_result *out) {
    qa_move_context *c = m->c;
    qa_vec3 *velocity = m->qw ? &c->state->data.qw.velocity : &c->state->data.nq.velocity;
    qa_vec3 primal = *velocity, original = *velocity, planes[5];
    size_t plane_count = 0;
    double remaining = dt;
    *out = (q1_fly_result){0};
    for (unsigned bump = 0; bump < 4; ++bump) {
        if (q1_stopped(m)) return false;
        if (!m->qw && velocity->x == 0 && velocity->y == 0 && velocity->z == 0) break;
        qa_trace_result trace;
        qa_vec3 start, end;
        if (m->qw) {
            qa_qw_origin origin = c->state->data.qw.origin;
            start = qa_qw_origin_to_vec3(origin);
            end = qa_qw_origin_to_vec3(q1_qw_ma(origin, remaining, *velocity));
        } else {
            start = c->state->data.nq.origin;
            end = q1_ma(start, (float)remaining, *velocity);
        }
        if (!q1_move_trace(m, start, end, &trace)) return false;
        if (trace.all_solid || (m->qw && trace.start_solid)) {
            *velocity = qa_v3(0, 0, 0);
            out->blocked = 3;
            return true;
        }
        if (trace.fraction > 0) {
            if (m->qw) c->state->data.qw.origin = qa_qw_origin_from_vec3(trace.end);
            else c->state->data.nq.origin = trace.end;
            if (!m->qw) original = *velocity;
            plane_count = 0;
        }
        if (trace.fraction == 1) break;
        if (!m->qw && trace.hit == QA_TRACE_HIT_NONE) {
            qa_error_set(c->error, QA_ERROR_FORMAT, 0, "NetQuake slide trace blocked without a hit");
            c->failed = true;
            return false;
        }
        qa_vec3 normal = trace.plane.normal;
        if (normal.z > 0.7f) {
            out->blocked |= 1;
            bool bsp = trace.hit == QA_TRACE_HIT_WORLD;
            if (!m->qw && !bsp && c->services->is_bsp &&
                !c->services->is_bsp(c->services->context, &trace, &bsp, c->error)) {
                c->failed = true;
                return false;
            }
            if (!m->qw && bsp) {
                c->state->data.nq.flags |= Q1_FLAG_ONGROUND;
                c->state->data.nq.ground = qa_move_ground(&trace);
            }
        }
        if (normal.z == 0) {
            out->blocked |= 2;
            out->has_step_trace = true;
            out->step_trace = trace;
        }
        if (m->qw) {
            if (!qa_move_contact(c, &trace, false, false)) return false;
        } else if (!q1_touch(m, &trace, true)) return false;
        remaining = m->qw ? remaining - remaining * trace.fraction :
            (float)((float)remaining - (float)((float)remaining * trace.fraction));
        if (plane_count >= 5) {
            *velocity = qa_v3(0, 0, 0);
            if (!m->qw) out->blocked = 3;
            break;
        }
        planes[plane_count++] = normal;
        bool found = false;
        qa_vec3 candidate = *velocity;
        for (size_t i = 0; i < plane_count; ++i) {
            candidate = qa_move_clip(original, planes[i], 1, 0.1f);
            bool clear = true;
            for (size_t j = 0; j < plane_count; ++j) {
                if (i != j && qa_vec_dot(candidate, planes[j]) < 0) { clear = false; break; }
            }
            if (clear) { found = true; break; }
        }
        if (!found) {
            if (plane_count != 2) {
                *velocity = qa_v3(0, 0, 0);
                if (!m->qw) out->blocked = 7;
                break;
            }
            qa_vec3 direction = qa_vec_cross(planes[0], planes[1]);
            candidate = qa_vec_scale(direction, qa_vec_dot(direction, m->qw ? candidate : *velocity));
        }
        if (qa_vec_dot(candidate, primal) <= 0) { *velocity = qa_v3(0, 0, 0); break; }
        *velocity = candidate;
    }
    if (m->qw && c->state->data.qw.water_jump_time_seconds != 0) *velocity = primal;
    return true;
}

bool q1_finish(q1_move *m, qa_movement_ground ground, int32_t water_level, int32_t water_type) {
    qa_move_context *c = m->c;
    c->result->ground = ground;
    c->result->water_level = water_level;
    c->result->water_type = water_type;
    q1_restore_mode(m);
    m->projected = false;
    if (!qa_move_phase(c, QA_MOVE_WEAPON)) return false;
    qa_vec3 velocity;
    if (m->qw) {
        velocity = c->state->data.qw.velocity;
        c->result->view_angles = c->state->data.qw.angles;
        if (c->phase_state_replaced) c->result->ground = c->state->data.qw.ground;
    } else {
        velocity = c->state->data.nq.velocity;
        c->result->view_angles = c->state->data.nq.view_angles;
        if (c->phase_state_replaced) {
            c->result->ground = c->state->data.nq.ground;
            c->result->water_level = c->state->data.nq.water_level;
            c->result->water_type = c->state->data.nq.water_type;
        }
    }
    c->result->horizontal_speed = sqrtf(velocity.x * velocity.x + velocity.y * velocity.y);
    bool backwards = c->command.forward_move < 0;
    qa_movement_locomotion locomotion = c->result->water_level >= 2 ? QA_MOVE_SWIM :
        c->result->ground.hit == QA_TRACE_HIT_NONE ? QA_MOVE_JUMP :
        c->result->horizontal_speed > 0 ? (backwards ? QA_MOVE_BACKWARD : QA_MOVE_RUN) : QA_MOVE_IDLE;
    if (m->crouch_animation) locomotion = QA_MOVE_CROUCH;
    return qa_move_animation(c, QA_MOVE_ANIMATION_LOCOMOTION, (int32_t)locomotion, false, backwards);
}
