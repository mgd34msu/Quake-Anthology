#include "common.h"

typedef struct nq_move {
    q1_move base;
    float dt;
    double time;
} nq_move;

static void nq_velocity_bounds(nq_move *m) {
    qa_nq_movement_state *s = &m->base.c->state->data.nq;
    float maximum = m->base.c->input->profile.data.nq.max_velocity;
    for (unsigned i = 0; i < 3; ++i) {
        float origin = qa_move_component(s->origin, i);
        float velocity = qa_move_component(s->velocity, i);
        qa_move_set_component(&s->origin, i, isfinite(origin) ? origin : 0);
        if (!isfinite(velocity)) velocity = 0;
        qa_move_set_component(&s->velocity, i, fmaxf(-maximum, fminf(maximum, velocity)));
    }
}

static void nq_gravity(nq_move *m) {
    const qa_q1_movement_parameters *p = &m->base.c->input->profile.data.nq.parameters;
    float entity_gravity = p->entity_gravity == 0 ? 1 : p->entity_gravity;
    m->base.c->state->data.nq.velocity.z -= entity_gravity *
        m->base.c->input->environment.gravity_multiplier * p->gravity * m->dt;
}

static bool nq_push(nq_move *m, qa_vec3 push, qa_trace_result *trace) {
    qa_move_context *c = m->base.c;
    qa_nq_movement_state *s = &c->state->data.nq;
    qa_q1_move_kind kind = s->move_type == Q1_MOVE_FLYMISSILE ? QA_Q1_MOVE_MISSILE :
        c->input->q1_solid == QA_Q1_SOLID_NOT || c->input->q1_solid == QA_Q1_SOLID_TRIGGER ?
        QA_Q1_MOVE_NO_MONSTERS : QA_Q1_MOVE_NORMAL;
    if (!qa_move_trace_q1(c, s->origin, qa_vec_add(s->origin, push), q1_shape(&m->base), kind, trace)) return false;
    s->origin = trace->end;
    if (!q1_phase(&m->base, QA_MOVE_LINK_TRIGGERS)) return false;
    return trace->hit == QA_TRACE_HIT_NONE || q1_touch(&m->base, trace, true);
}

static bool nq_check_stuck(nq_move *m) {
    qa_nq_movement_state *s = &m->base.c->state->data.nq;
    bool free_position;
    if (!q1_position_free(&m->base, s->origin, &free_position)) return false;
    if (free_position) { s->old_origin = s->origin; return true; }
    qa_vec3 original = s->origin;
    s->origin = s->old_origin;
    if (!q1_position_free(&m->base, s->origin, &free_position)) return false;
    if (free_position) return q1_phase(&m->base, QA_MOVE_LINK_TRIGGERS);
    for (int z = 0; z < 18; ++z) {
        for (int x = -1; x <= 1; ++x) {
            for (int y = -1; y <= 1; ++y) {
                s->origin = qa_vec_add(original, qa_v3((float)x, (float)y, (float)z));
                if (!q1_position_free(&m->base, s->origin, &free_position)) return false;
                if (free_position) return q1_phase(&m->base, QA_MOVE_LINK_TRIGGERS);
            }
        }
    }
    s->origin = original;
    return true;
}

static bool nq_check_water(nq_move *m) {
    qa_move_context *c = m->base.c;
    qa_nq_movement_state *s = &c->state->data.nq;
    qa_vec3 point = s->origin;
    point.z += c->result->bounds.mins.z + 1;
    s->water_level = 0;
    s->water_type = Q1_CONTENTS_EMPTY;
    int32_t contents;
    if (!q1_contents(&m->base, point, &contents)) return false;
    if (contents <= Q1_CONTENTS_WATER) {
        s->water_type = contents;
        s->water_level = 1;
        point.z = s->origin.z + (c->result->bounds.mins.z + c->result->bounds.maxs.z) * 0.5f;
        if (!q1_contents(&m->base, point, &contents)) return false;
        if (contents <= Q1_CONTENTS_WATER) {
            s->water_level = 2;
            point.z = s->origin.z + c->result->view_height;
            if (!q1_contents(&m->base, point, &contents)) return false;
            if (contents <= Q1_CONTENTS_WATER) s->water_level = 3;
        }
    }
    c->result->water_level = s->water_level;
    c->result->water_type = s->water_type;
    return true;
}

static void nq_wall_friction(nq_move *m, const qa_trace_result *trace) {
    qa_nq_movement_state *s = &m->base.c->state->data.nq;
    qa_vec3 forward;
    qa_move_angles(s->view_angles, &forward, NULL, NULL);
    float d = qa_vec_dot(trace->plane.normal, forward) + 0.5f;
    if (d >= 0) return;
    qa_vec3 into = qa_vec_scale(trace->plane.normal, qa_vec_dot(trace->plane.normal, s->velocity));
    qa_vec3 side = qa_vec_sub(s->velocity, into);
    s->velocity.x = side.x * (1 + d);
    s->velocity.y = side.y * (1 + d);
}

static bool nq_unstick(nq_move *m, qa_vec3 old_velocity, q1_fly_result *out) {
    const qa_vec3 directions[8] = {
        {2, 0, 0}, {0, 2, 0}, {-2, 0, 0}, {0, -2, 0},
        {2, 2, 0}, {-2, 2, 0}, {2, -2, 0}, {-2, -2, 0}
    };
    qa_nq_movement_state *s = &m->base.c->state->data.nq;
    qa_vec3 original = s->origin;
    for (size_t i = 0; i < 8; ++i) {
        qa_trace_result trace;
        if (!nq_push(m, directions[i], &trace)) return false;
        s->velocity = qa_v3(old_velocity.x, old_velocity.y, 0);
        if (!q1_fly(&m->base, 0.1f, out)) return false;
        if (fabsf(original.y - s->origin.y) > 4 || fabsf(original.x - s->origin.x) > 4) return true;
        s->origin = original;
    }
    s->velocity = qa_v3(0, 0, 0);
    *out = (q1_fly_result){ .blocked = 7 };
    return true;
}

static bool nq_walk(nq_move *m) {
    qa_move_context *c = m->base.c;
    qa_nq_movement_state *s = &c->state->data.nq;
    bool old_on_ground = (s->flags & Q1_FLAG_ONGROUND) != 0;
    s->flags &= ~(uint32_t)Q1_FLAG_ONGROUND;
    qa_vec3 original = s->origin, old_velocity = s->velocity;
    q1_fly_result clip;
    if (!q1_fly(&m->base, m->dt, &clip)) return false;
    if (!(clip.blocked & 2) || (!old_on_ground && s->water_level == 0) ||
        s->move_type != Q1_MOVE_WALK || c->input->profile.data.nq.no_step ||
        (s->flags & Q1_FLAG_WATERJUMP)) return true;
    qa_vec3 no_step_origin = s->origin, no_step_velocity = s->velocity;
    s->origin = original;
    qa_trace_result trace;
    if (!nq_push(m, qa_v3(0, 0, 18), &trace)) return false;
    s->velocity = qa_v3(old_velocity.x, old_velocity.y, 0);
    if (!q1_fly(&m->base, m->dt, &clip)) return false;
    if (clip.blocked && fabsf(original.y - s->origin.y) < 0.03125f &&
        fabsf(original.x - s->origin.x) < 0.03125f) {
        q1_fly_result unstick;
        if (!nq_unstick(m, old_velocity, &unstick)) return false;
        clip.blocked = unstick.blocked;
    }
    if ((clip.blocked & 2) && clip.has_step_trace) nq_wall_friction(m, &clip.step_trace);
    if (!nq_push(m, qa_v3(0, 0, -18 + old_velocity.z * m->dt), &trace)) return false;
    if (trace.plane.normal.z > 0.7f) {
        /* SV_WalkMove checks the player's solid type here. */
        if (c->input->q1_solid == QA_Q1_SOLID_BSP) {
            s->flags |= Q1_FLAG_ONGROUND;
            s->ground = qa_move_ground(&trace);
        }
    } else {
        s->origin = no_step_origin;
        s->velocity = no_step_velocity;
    }
    return true;
}

static bool nq_friction(nq_move *m) {
    qa_move_context *c = m->base.c;
    qa_nq_movement_state *s = &c->state->data.nq;
    const qa_q1_movement_parameters *p = &c->input->profile.data.nq.parameters;
    float speed = sqrtf(s->velocity.x * s->velocity.x + s->velocity.y * s->velocity.y);
    if (speed == 0) return true;
    qa_vec3 start = qa_v3(s->origin.x + s->velocity.x / speed * 16,
                           s->origin.y + s->velocity.y / speed * 16,
                           s->origin.z + c->result->bounds.mins.z);
    qa_trace_result trace;
    if (!q1_point_trace(&m->base, start, qa_vec_add(start, qa_v3(0, 0, -34)), &trace)) return false;
    float friction = trace.fraction == 1 ? p->friction * c->input->profile.data.nq.edge_friction : p->friction;
    float new_speed = fmaxf(0, speed - m->dt * fmaxf(speed, p->stop_speed) * friction);
    s->velocity = qa_vec_scale(s->velocity, new_speed / speed);
    return true;
}

static void nq_accelerate(nq_move *m, qa_vec3 direction, float speed, bool air) {
    qa_nq_movement_state *s = &m->base.c->state->data.nq;
    const qa_q1_movement_parameters *p = &m->base.c->input->profile.data.nq.parameters;
    float wish_speed = air ? fminf(qa_vec_length(direction), 30) : speed;
    if (air) direction = qa_vec_normalize(direction);
    float add = wish_speed - qa_vec_dot(s->velocity, direction);
    if (add <= 0) return;
    float acceleration = fminf(add, air ? p->accelerate * speed * m->dt : p->accelerate * m->dt * speed);
    s->velocity = q1_ma(s->velocity, acceleration, direction);
}

static void nq_water_move(nq_move *m) {
    qa_move_context *c = m->base.c;
    qa_nq_movement_state *s = &c->state->data.nq;
    const qa_q1_movement_parameters *p = &c->input->profile.data.nq.parameters;
    qa_vec3 forward, right;
    qa_move_angles(s->view_angles, &forward, &right, NULL);
    qa_vec3 wish = qa_vec_add(qa_vec_scale(forward, q1_speed(&m->base, (float)c->command.forward_move)),
                               qa_vec_scale(right, q1_speed(&m->base, (float)c->command.side_move)));
    wish.z += c->command.forward_move == 0 && c->command.side_move == 0 && c->command.up_move == 0 ?
        -60 : q1_speed(&m->base, (float)c->command.up_move);
    float wish_speed = qa_vec_length(wish), maximum = q1_speed(&m->base, p->max_speed);
    if (wish_speed > maximum) { wish = qa_vec_scale(wish, maximum / wish_speed); wish_speed = maximum; }
    wish_speed *= 0.7f;
    float speed = qa_vec_length(s->velocity), new_speed = 0;
    if (speed != 0) {
        new_speed = fmaxf(0, speed - m->dt * speed * p->friction);
        s->velocity = qa_vec_scale(s->velocity, new_speed / speed);
    }
    if (wish_speed == 0) return;
    float add = wish_speed - new_speed;
    if (add <= 0) return;
    s->velocity = q1_ma(s->velocity, fminf(add, p->accelerate * wish_speed * m->dt), qa_vec_normalize(wish));
}

static bool nq_air_move(nq_move *m) {
    qa_move_context *c = m->base.c;
    qa_nq_movement_state *s = &c->state->data.nq;
    const qa_q1_movement_parameters *p = &c->input->profile.data.nq.parameters;
    qa_vec3 forward, right;
    qa_move_angles(s->angles, &forward, &right, NULL);
    float forward_move = m->time < s->teleport_time_seconds && c->command.forward_move < 0 ? 0 : c->command.forward_move;
    qa_vec3 wish = qa_vec_add(qa_vec_scale(forward, q1_speed(&m->base, (float)forward_move)),
                               qa_vec_scale(right, q1_speed(&m->base, (float)c->command.side_move)));
    wish.z = s->move_type == Q1_MOVE_WALK ? 0 : q1_speed(&m->base, (float)c->command.up_move);
    qa_vec3 direction = qa_vec_normalize(wish);
    float wish_speed = qa_vec_length(wish), maximum = q1_speed(&m->base, p->max_speed);
    if (wish_speed > maximum) { wish = qa_vec_scale(wish, maximum / wish_speed); wish_speed = maximum; }
    if (s->move_type == Q1_MOVE_NOCLIP) s->velocity = wish;
    else if (s->flags & Q1_FLAG_ONGROUND) {
        if (!nq_friction(m)) return false;
        nq_accelerate(m, direction, wish_speed, false);
    } else nq_accelerate(m, wish, wish_speed, true);
    return true;
}

static void nq_alternate_noclip(nq_move *m) {
    qa_move_context *c = m->base.c;
    qa_vec3 forward, right;
    qa_move_angles(c->state->data.nq.view_angles, &forward, &right, NULL);
    qa_vec3 wish = qa_vec_add(qa_vec_scale(forward, q1_speed(&m->base, (float)c->command.forward_move)),
                               qa_vec_scale(right, q1_speed(&m->base, (float)c->command.side_move)));
    wish.z += q1_speed(&m->base, (float)c->command.up_move) * 2;
    float maximum = q1_speed(&m->base, c->input->profile.data.nq.parameters.max_speed);
    c->state->data.nq.velocity = qa_vec_length(wish) > maximum ? qa_vec_scale(qa_vec_normalize(wish), maximum) : wish;
}

static bool nq_client_think(nq_move *m) {
    qa_move_context *c = m->base.c;
    qa_nq_movement_state *s = &c->state->data.nq;
    if (s->move_type == Q1_MOVE_NONE) return true;
    if (c->input->has_source_punch_angles) s->punch_angles = c->input->source_punch_angles;
    else {
        float length = qa_vec_length(s->punch_angles);
        s->punch_angles = qa_vec_scale(qa_vec_normalize(s->punch_angles), fmaxf(0, length - 10 * m->dt));
    }
    if (s->health <= 0) return true;
    qa_vec3 angles = qa_vec_add(s->view_angles, s->punch_angles), right;
    qa_move_angles(s->angles, NULL, &right, NULL);
    float side = qa_vec_dot(s->velocity, right), sign = side < 0 ? -1 : 1;
    float roll_speed = c->input->profile.data.nq.roll_speed, roll_angle = c->input->profile.data.nq.roll_angle;
    float roll = (fabsf(side) < roll_speed ? fabsf(side) * roll_angle / roll_speed : roll_angle) * sign * 4;
    if (!s->fix_angle) s->angles = qa_v3(-angles.x / 3, angles.y, roll);
    else if (!c->input->profile.data.nq.preserve_fixangle_roll) s->angles.z = roll;
    if (s->flags & Q1_FLAG_WATERJUMP) {
        if (m->time > s->teleport_time_seconds || s->water_level == 0) {
            s->flags &= ~(uint32_t)Q1_FLAG_WATERJUMP;
            s->teleport_time_seconds = 0;
        }
        s->velocity.x = s->water_jump_direction.x;
        s->velocity.y = s->water_jump_direction.y;
        return true;
    }
    if (s->move_type == Q1_MOVE_NOCLIP && c->input->profile.data.nq.no_clip_angle_hack) nq_alternate_noclip(m);
    else if (s->water_level >= 2 && s->move_type != Q1_MOVE_NOCLIP) nq_water_move(m);
    else return nq_air_move(m);
    return true;
}

static bool nq_check_water_jump(nq_move *m) {
    qa_move_context *c = m->base.c;
    qa_nq_movement_state *s = &c->state->data.nq;
    qa_vec3 forward;
    qa_move_angles(s->angles, &forward, NULL, NULL);
    /* Original QC discards normalize's result after clearing forward.z. */
    forward.z = 0;
    qa_vec3 start = qa_vec_add(s->origin, qa_v3(0, 0, 8));
    qa_trace_result low, high;
    if (!q1_point_trace(&m->base, start, q1_ma(start, 24, forward), &low)) return false;
    if (low.fraction == 1) return true;
    start.z += c->result->bounds.maxs.z - 8;
    qa_vec3 direction = qa_vec_scale(low.plane.normal, -50);
    if (!q1_point_trace(&m->base, start, q1_ma(start, 24, forward), &high)) return false;
    s->water_jump_direction = direction;
    if (high.fraction != 1) return true;
    s->flags = (s->flags | Q1_FLAG_WATERJUMP) & ~(uint32_t)Q1_FLAG_JUMPRELEASED;
    s->velocity.z = 225;
    s->teleport_time_seconds = m->time + 2;
    return true;
}

static bool nq_player_actions(nq_move *m) {
    qa_move_context *c = m->base.c;
    qa_nq_movement_state *s = &c->state->data.nq;
    if (c->input->profile.data.nq.source_jump_authority || s->health <= 0 ||
        c->result->view_height == 0 || s->move_type != Q1_MOVE_WALK) return true;
    if (s->water_level == 2 && !nq_check_water_jump(m)) return false;
    if (!(c->command.buttons & 2)) { s->flags |= Q1_FLAG_JUMPRELEASED; return true; }
    if (s->flags & Q1_FLAG_WATERJUMP) return true;
    if (s->water_level >= 2) {
        s->velocity.z = s->water_type == Q1_CONTENTS_WATER ? 100 : s->water_type == Q1_CONTENTS_SLIME ? 80 : 50;
        return q1_action(&m->base, QA_MOVE_SWIM);
    }
    if (!(s->flags & Q1_FLAG_ONGROUND) || !(s->flags & Q1_FLAG_JUMPRELEASED)) return true;
    s->flags &= ~(uint32_t)(Q1_FLAG_ONGROUND | Q1_FLAG_JUMPRELEASED);
    s->velocity.z += 270;
    return q1_action(&m->base, QA_MOVE_JUMP);
}

static bool nq_ideal_pitch(nq_move *m) {
    qa_move_context *c = m->base.c;
    qa_nq_movement_state *s = &c->state->data.nq;
    if (!(s->flags & Q1_FLAG_ONGROUND)) return true;
    float radians = s->angles.y * 0.01745329251994329577f;
    float sine = sinf(radians), cosine = cosf(radians), heights[6];
    for (unsigned i = 0; i < 6; ++i) {
        qa_vec3 top = qa_v3(s->origin.x + cosine * (float)(i + 3) * 12,
                             s->origin.y + sine * (float)(i + 3) * 12,
                             s->origin.z + c->result->view_height);
        qa_vec3 bottom = qa_vec_add(top, qa_v3(0, 0, -160));
        qa_trace_result trace;
        if (!q1_point_trace(&m->base, top, bottom, &trace)) return false;
        if (trace.all_solid || trace.fraction == 1) return true;
        heights[i] = top.z + trace.fraction * (bottom.z - top.z);
    }
    float direction = 0;
    unsigned steps = 0;
    for (unsigned i = 1; i < 6; ++i) {
        float step = heights[i] - heights[i - 1];
        if (step > -0.1f && step < 0.1f) continue;
        if (direction != 0 && fabsf(step - direction) > 0.1f) return true;
        ++steps;
        direction = step;
    }
    if (direction == 0) s->ideal_pitch = 0;
    else if (steps >= 2) s->ideal_pitch = -direction * c->input->profile.data.nq.ideal_pitch_scale;
    return true;
}

static bool nq_water_transition(nq_move *m) {
    qa_nq_movement_state *s = &m->base.c->state->data.nq;
    int32_t contents;
    if (!q1_contents(&m->base, s->origin, &contents))
        return false;
    qa_q1_water_transition_result transition = qa_q1_water_transition(s->water_type, contents);
    if (transition.splash && !q1_sound(&m->base, "misc/h2ohit1.wav"))
        return false;
    s->water_type = transition.water_type;
    s->water_level = transition.water_level;
    return true;
}

static bool nq_toss(nq_move *m) {
    qa_nq_movement_state *s = &m->base.c->state->data.nq;
    if (s->flags & Q1_FLAG_ONGROUND) return true;
    nq_velocity_bounds(m);
    if (s->move_type != Q1_MOVE_FLY && s->move_type != Q1_MOVE_FLYMISSILE) nq_gravity(m);
    s->angles = q1_ma(s->angles, m->dt, s->angular_velocity);
    qa_trace_result trace;
    if (!nq_push(m, qa_vec_scale(s->velocity, m->dt), &trace)) return false;
    if (trace.fraction == 1) return true;
    bool bounces = s->move_type == Q1_MOVE_BOUNCE ||
        (s->move_type == Q1_MOVE_GIB && m->base.c->input->profile.data.nq.edition == QA_Q1_RERELEASE);
    s->velocity = qa_move_clip(s->velocity, trace.plane.normal, bounces ? 1.5f : 1, 0.1f);
    if (trace.plane.normal.z > 0.7f && (s->velocity.z < 60 || !bounces)) {
        s->flags |= Q1_FLAG_ONGROUND;
        s->ground = qa_move_ground(&trace);
        s->velocity = qa_v3(0, 0, 0);
        s->angular_velocity = qa_v3(0, 0, 0);
    }
    return nq_water_transition(m);
}

static bool nq_physics(nq_move *m) {
    qa_move_context *c = m->base.c;
    qa_nq_movement_state *s = &c->state->data.nq;
    if (!q1_phase(&m->base, QA_MOVE_PRETHINK)) return false;
    if (!c->input->environment.fixed_pose && !nq_player_actions(m)) return false;
    bool extension_think = s->move_type == Q1_MOVE_STEP || s->move_type == Q1_MOVE_FLYMISSILE || s->move_type == Q1_MOVE_GIB;
    if (extension_think && !q1_phase(&m->base, QA_MOVE_THINK)) return false;
    nq_velocity_bounds(m);
    if (c->input->environment.fixed_pose) {
        if (!extension_think && !q1_phase(&m->base, QA_MOVE_THINK)) return false;
        s->velocity = qa_v3(0, 0, 0);
        if (!nq_check_water(m)) return false;
    } else {
        /* Choose the source branch before think; think may change move_type. */
        switch (s->move_type) {
        case Q1_MOVE_NONE:
            if (!extension_think && !q1_phase(&m->base, QA_MOVE_THINK)) return false;
            break;
        case Q1_MOVE_WALK:
            if (!extension_think && !q1_phase(&m->base, QA_MOVE_THINK)) return false;
            if (!nq_check_water(m)) return false;
            if (s->water_level <= 1 && !(s->flags & Q1_FLAG_WATERJUMP)) nq_gravity(m);
            if (!nq_check_stuck(m) || !nq_walk(m)) return false;
            break;
        case Q1_MOVE_FLY: {
            if (!extension_think && !q1_phase(&m->base, QA_MOVE_THINK)) return false;
            q1_fly_result clip;
            if (!q1_fly(&m->base, m->dt, &clip)) return false;
            break;
        }
        case Q1_MOVE_NOCLIP:
            if (!extension_think && !q1_phase(&m->base, QA_MOVE_THINK)) return false;
            s->origin = q1_ma(s->origin, m->dt, s->velocity);
            break;
        case Q1_MOVE_TOSS:
        case Q1_MOVE_BOUNCE:
            if (!extension_think && !q1_phase(&m->base, QA_MOVE_THINK)) return false;
            if (!nq_toss(m)) return false;
            break;
        case Q1_MOVE_FLYMISSILE:
            if (!nq_toss(m)) return false;
            break;
        case Q1_MOVE_GIB:
            if (c->input->profile.data.nq.edition != QA_Q1_RERELEASE) {
                qa_error_set(c->error, QA_ERROR_UNSUPPORTED, 0, "MOVETYPE_GIB requires Quake rerelease behavior");
                c->failed = true;
                return false;
            }
            if (!nq_toss(m)) return false;
            break;
        case Q1_MOVE_STEP:
            if (!(s->flags & (Q1_FLAG_ONGROUND | Q1_FLAG_FLY | Q1_FLAG_SWIM))) {
                bool hit_sound = s->velocity.z < c->input->profile.data.nq.parameters.gravity * -0.1f;
                nq_gravity(m);
                nq_velocity_bounds(m);
                q1_fly_result clip;
                if (!q1_fly(&m->base, m->dt, &clip) || !q1_phase(&m->base, QA_MOVE_LINK_TRIGGERS)) return false;
                if (hit_sound && (s->flags & Q1_FLAG_ONGROUND) && !q1_sound(&m->base, "demon/dland2.wav")) return false;
            }
            if (!nq_water_transition(m)) return false;
            break;
        default:
            qa_error_set(c->error, QA_ERROR_UNSUPPORTED, 0, "Unsupported NetQuake player movetype %d", s->move_type);
            c->failed = true;
            return false;
        }
    }
    if (!q1_phase(&m->base, QA_MOVE_LINK_TRIGGERS) || !q1_phase(&m->base, QA_MOVE_POSTTHINK)) return false;
    if (!nq_ideal_pitch(m)) return false;
    return q1_finish(&m->base, (s->flags & Q1_FLAG_ONGROUND) ? s->ground : q1_no_ground(), s->water_level, s->water_type);
}

static bool nq_profile(qa_move_context *c) {
    if (c->input->has_source_seconds &&
        (!c->input->prediction || !isfinite(c->input->source_seconds))) {
        qa_error_set(c->error, QA_ERROR_ARGUMENT, 0, "NQ prediction requires a finite actual source clock");
        c->failed = true;
        return false;
    }
    if (c->input->profile.data.nq.edition == QA_Q1_QUAKE64) {
        qa_error_set(c->error, QA_ERROR_UNSUPPORTED, 0, "Quake64 movement requires a qualified source physics profile");
        c->failed = true;
        return false;
    }
    return true;
}

bool qa_move_nq_prepare(qa_move_context *c) {
    if (!nq_profile(c)) return false;
    nq_move move = { .dt = c->dt, .time = c->input->has_source_seconds
        ? c->input->source_seconds : (double)c->time_ns / 1000000000.0 };
    q1_init(&move.base, c, false);
    c->state->data.nq.view_angles = c->command.angles;
    bool ok = nq_client_think(&move);
    if (!c->removed) q1_restore_mode(&move.base);
    const qa_nq_movement_state *s = &c->state->data.nq;
    c->result->view_angles = s->view_angles;
    c->result->ground = (s->flags & Q1_FLAG_ONGROUND) ? s->ground : q1_no_ground();
    c->result->water_level = s->water_level;
    c->result->water_type = s->water_type;
    c->result->horizontal_speed = hypotf(s->velocity.x, s->velocity.y);
    return ok;
}

bool qa_move_nq_physics(qa_move_context *c) {
    if (!nq_profile(c)) return false;
    nq_move move = { .dt = c->dt, .time = c->input->has_source_seconds
        ? c->input->source_seconds : (double)c->time_ns / 1000000000.0 };
    q1_init(&move.base, c, false);
    bool ok = nq_physics(&move);
    if (!c->removed) q1_restore_mode(&move.base);
    return ok;
}

bool qa_move_nq(qa_move_context *c) {
    if (!nq_profile(c)) return false;
    c->result->bounds = c->input->shape.kind == QA_SHAPE_POINT ? (qa_bounds){0} : c->input->shape.bounds;
    if (c->input->environment.fixed_pose) {
        c->result->bounds = c->input->environment.pose.bounds;
        c->result->view_height = c->input->environment.pose.view_height;
    }
    qa_movement_state input_state = *c->state;
    if (!c->input->prediction && !qa_move_phase(c, QA_MOVE_INPUT_BEGIN)) {
        if (c->failed) *c->state = input_state;
        qa_move_phase(c, c->failed ? QA_MOVE_INPUT_ABORT : QA_MOVE_INPUT_END);
        return !c->failed;
    }
    input_state = *c->state;
    if (qa_move_nq_prepare(c)) qa_move_nq_physics(c);
    if (!c->input->prediction) {
        if (c->failed || c->removed) *c->state = input_state;
        qa_move_phase(c, c->failed ? QA_MOVE_INPUT_ABORT : QA_MOVE_INPUT_END);
    }
    return !c->failed;
}
