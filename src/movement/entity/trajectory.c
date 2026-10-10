#include "internal.h"
#include <limits.h>

static int32_t signed_word(uint32_t value) {
    return value <= INT32_MAX ? (int32_t)value :
        (int32_t)((int64_t)value - INT64_C(4294967296));
}

static int32_t integer(float value) {
    return isfinite(value) && value >= -2147483648.0f && value < 2147483648.0f ?
        (int32_t)truncf(value) : INT32_MIN;
}

static int32_t elapsed(int32_t at, int32_t from) {
    return signed_word((uint32_t)at - (uint32_t)from);
}

static bool valid_trajectory(const qa_trajectory *trajectory, float gravity,
                              const qa_vec3 *out, qa_error *error) {
    if (!trajectory || !out || trajectory->type < QA_TRAJECTORY_STATIONARY ||
        trajectory->type > QA_TRAJECTORY_GRAVITY || !qa_vec_finite(trajectory->base) ||
        !qa_vec_finite(trajectory->delta) || !isfinite(gravity) ||
        (trajectory->type == QA_TRAJECTORY_SINE && trajectory->duration_ms == 0)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 trajectory or output");
        return false;
    }
    return true;
}

static float radians(const qa_trajectory *trajectory, int32_t at) {
    float fraction = (float)elapsed(at, trajectory->time_ms) / (float)trajectory->duration_ms;
    return fraction * 3.14159265358979323846f * 2.0f;
}

bool qa_trajectory_position(const qa_trajectory *trajectory, int32_t at, float gravity,
                             qa_vec3 *out, qa_error *error) {
    if (!valid_trajectory(trajectory, gravity, out, error)) return false;
    float seconds;
    switch (trajectory->type) {
    case QA_TRAJECTORY_STATIONARY:
    case QA_TRAJECTORY_INTERPOLATE:
        *out = trajectory->base;
        return true;
    case QA_TRAJECTORY_LINEAR:
        seconds = (float)elapsed(at, trajectory->time_ms) * 0.001f;
        *out = qa_vec_add(trajectory->base, qa_vec_scale(trajectory->delta, seconds));
        return true;
    case QA_TRAJECTORY_LINEAR_STOP: {
        int32_t end = signed_word((uint32_t)trajectory->time_ms + (uint32_t)trajectory->duration_ms);
        if (at > end) at = end;
        seconds = fmaxf(0, (float)elapsed(at, trajectory->time_ms) * 0.001f);
        *out = qa_vec_add(trajectory->base, qa_vec_scale(trajectory->delta, seconds));
        return true;
    }
    case QA_TRAJECTORY_SINE:
        *out = qa_vec_add(trajectory->base, qa_vec_scale(trajectory->delta, sinf(radians(trajectory, at))));
        return true;
    case QA_TRAJECTORY_GRAVITY:
        seconds = (float)elapsed(at, trajectory->time_ms) * 0.001f;
        *out = qa_vec_add(trajectory->base, qa_vec_scale(trajectory->delta, seconds));
        out->z -= 0.5f * gravity * seconds * seconds;
        return true;
    }
    return false;
}

bool qa_trajectory_velocity(const qa_trajectory *trajectory, int32_t at, float gravity,
                             qa_vec3 *out, qa_error *error) {
    if (!valid_trajectory(trajectory, gravity, out, error)) return false;
    switch (trajectory->type) {
    case QA_TRAJECTORY_STATIONARY:
    case QA_TRAJECTORY_INTERPOLATE:
        *out = qa_v3(0, 0, 0);
        return true;
    case QA_TRAJECTORY_LINEAR:
        *out = trajectory->delta;
        return true;
    case QA_TRAJECTORY_LINEAR_STOP:
        *out = at > signed_word((uint32_t)trajectory->time_ms + (uint32_t)trajectory->duration_ms) ?
            qa_v3(0, 0, 0) : trajectory->delta;
        return true;
    case QA_TRAJECTORY_SINE:
        /* The original derivative uses half-amplitude cosine, independent of
         * the trajectory duration. Do not replace it with analytic scaling. */
        *out = qa_vec_scale(trajectory->delta, cosf(radians(trajectory, at)) * 0.5f);
        return true;
    case QA_TRAJECTORY_GRAVITY:
        *out = trajectory->delta;
        out->z -= gravity * ((float)elapsed(at, trajectory->time_ms) * 0.001f);
        return true;
    }
    return false;
}

int32_t qa_physics_q3_hit_time(int32_t previous, int32_t now, float fraction) {
    return integer((float)previous + (float)elapsed(now, previous) * fraction);
}

qa_vec3 qa_physics_q3_snap(qa_vec3 value) {
    return qa_v3((float)integer(value.x), (float)integer(value.y), (float)integer(value.z));
}

static float snap_towards(float value, float target) {
    uint32_t result = (uint32_t)integer(value);
    if (!(target <= value)) ++result;
    return (float)signed_word(result);
}

qa_vec3 qa_physics_q3_snap_towards(qa_vec3 value, qa_vec3 target) {
    return qa_v3(snap_towards(value.x, target.x), snap_towards(value.y, target.y),
                 snap_towards(value.z, target.z));
}

bool qa_physics_q3_missile_move(qa_physics *physics, qa_actor_id actor,
                                const qa_trajectory *trajectory, int32_t now,
                                qa_actor_id pass, qa_trace_result *out, qa_error *error) {
    if (!physics || !physics->world || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 missile movement context");
        return false;
    }
    qa_vec3 destination, velocity;
    /* Native Q3 trajectories use DEFAULT_GRAVITY independently of a selected
     * Q1/Q2 world's entity gravity. */
    if (!qa_trajectory_position(trajectory, now, 800, &destination, error) ||
        !qa_trajectory_velocity(trajectory, now, 800, &velocity, error)) return false;
    qa_body_state body;
    qa_physics_properties properties;
    int read = ph_read(physics, actor, &body, &properties, error);
    if (read <= 0) {
        if (read == 0) *out = (qa_trace_result){.family = QA_GAME_Q3, .fraction = 1, .end = destination};
        return read == 0;
    }
    qa_trace_query query = {.start = body.origin, .end = destination,
        .shape = {QA_SHAPE_BOX, body.bounds}, .pass_actor = pass,
        .policy = qa_collision_default_policy(QA_GAME_Q3)};
    query.policy.contents_mask = qa_collision_contents_mask(properties.clip_mask,QA_GAME_Q3);
    qa_trace_result trace;
    if (!qa_world_trace(physics->world, &query, &trace, error)) return false;
    if (trace.start_solid || trace.all_solid) {
        query.end = query.start;
        if (!qa_world_trace(physics->world, &query, &trace, error)) return false;
        trace.fraction = 0;
    } else {
        read = ph_read(physics, actor, &body, &properties, error);
        if (read < 0) return false;
        if (read) {
            body.origin = trace.end;
            body.velocity = velocity;
            if (!ph_write(physics, actor, &body, error)) return false;
        }
    }
    if (!ph_link(physics, actor, false, error)) return false;
    *out = trace;
    return true;
}

bool qa_physics_q3_bounce(qa_physics *physics, qa_actor_id actor, qa_trajectory *trajectory,
                          const qa_trace_result *trace, int32_t previous, int32_t now,
                          bool half, bool *stopped, qa_error *error) {
    if (!physics || !physics->world || !trace || !stopped || trace->family != QA_GAME_Q3 ||
        !isfinite(trace->fraction) || trace->fraction < 0 || trace->fraction > 1) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 missile bounce context");
        return false;
    }
    qa_vec3 velocity;
    int32_t hit_time = qa_physics_q3_hit_time(previous, now, trace->fraction);
    if (!qa_trajectory_velocity(trajectory, hit_time, 800, &velocity, error)) return false;
    qa_vec3 normal = trace->contact ? trace->contact_plane.normal : qa_v3(0, 0, 0);
    velocity = qa_vec_add(velocity, qa_vec_scale(normal, -2.0f * qa_vec_dot(velocity, normal)));
    if (half) velocity = qa_vec_scale(velocity, 0.65f);
    qa_body_state body;
    qa_physics_properties properties;
    int read = ph_read(physics, actor, &body, &properties, error);
    *stopped = false;
    if (read <= 0) return read == 0;
    trajectory->delta = velocity;
    if (half && normal.z > 0.2f && qa_vec_length(velocity) < 40) {
        body.origin = trace->end;
        body.velocity = qa_v3(0, 0, 0);
        *trajectory = (qa_trajectory){.type = QA_TRAJECTORY_STATIONARY, .base = trace->end};
        *stopped = true;
    } else {
        body.origin = qa_vec_add(body.origin, normal);
        body.velocity = velocity;
        trajectory->base = body.origin;
        trajectory->time_ms = now;
    }
    return ph_write(physics, actor, &body, error);
}
