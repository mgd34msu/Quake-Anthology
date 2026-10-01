#include "pose.h"

#include <limits.h>
#include <string.h>

static float add(float a, float b) { volatile float value = a + b; return value; }
static float mul(float a, float b) { volatile float value = a * b; return value; }
static float divide(float a, float b) { volatile float value = a / b; return value; }
static int32_t word(uint32_t value) { int32_t result; memcpy(&result, &value, sizeof(result)); return result; }
static int32_t sum(int32_t a, int32_t b) { return word((uint32_t)a + (uint32_t)b); }
static int32_t difference(int32_t a, int32_t b) { return word((uint32_t)a - (uint32_t)b); }
static int32_t integer(float value) {
    return isfinite(value) && value >= -2147483648.0f && value < 2147483648.0f
        ? (int32_t)truncf(value) : INT32_MIN;
}
static bool fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message); return false;
}
static const qa_player_animation *animation(const qa_player_animation_config *config,
    int32_t number, qa_error *error) {
    int32_t index = number & ~128;
    if (index < 0 || index >= QA_PLAYER_ANIMATION_COUNT || !config->animations[index].present) {
        fail(error, "Bad native Q3 player animation number"); return NULL;
    }
    return &config->animations[index];
}
static bool select_animation(const qa_player_animation_config *config,
    q3n_lerp_frame *state, int32_t number, qa_error *error) {
    const qa_player_animation *a = animation(config, number, error);
    if (!a) return false;
    state->animation_number = number; state->selected = true;
    state->animation_time = sum(state->frame_time, a->initial_lerp); return true;
}

bool q3n_lerp_clear(const qa_player_animation_config *config, q3n_lerp_frame *state,
    int32_t number, int32_t time, qa_error *error) {
    const qa_player_animation *a = animation(config, number, error);
    if (!a) return false;
    state->frame_time = state->old_frame_time = time;
    if (!select_animation(config, state, number, error)) return false;
    state->old_frame = state->frame = a->first_frame; return true;
}

bool q3n_lerp_run(const qa_player_animation_config *config, q3n_lerp_frame *state,
    int32_t number, int32_t time, float speed, bool disabled, qa_error *error) {
    if (disabled) { state->old_frame = state->frame = 0; state->back_lerp = 0; return true; }
    if (!isfinite(speed) || speed < 0) return fail(error, "Invalid native Q3 animation speed");
    if ((number != state->animation_number || !state->selected) &&
        !select_animation(config, state, number, error)) return false;
    const qa_player_animation *a = animation(config, state->animation_number, error);
    if (!a) return false;
    if (time >= state->frame_time) {
        state->old_frame = state->frame; state->old_frame_time = state->frame_time;
        if (!a->frame_lerp) return true;
        state->frame_time = time < state->animation_time ? state->animation_time
            : sum(state->old_frame_time, a->frame_lerp);
        double quotient = trunc((double)difference(state->frame_time, state->animation_time) / a->frame_lerp);
        int32_t offset = integer(mul((float)quotient, speed));
        int32_t count = a->flipflop ? word((uint32_t)a->num_frames * 2u) : a->num_frames;
        if (offset >= count) {
            offset = difference(offset, count);
            if (a->loop_frames) {
                offset = (int32_t)((int64_t)offset % a->loop_frames);
                offset = sum(offset, difference(a->num_frames, a->loop_frames));
            } else { offset = difference(count, 1); state->frame_time = time; }
        }
        if (a->reversed)
            state->frame = difference(difference(sum(a->first_frame, a->num_frames), 1), offset);
        else if (a->flipflop && offset >= a->num_frames) {
            if (!a->num_frames) return fail(error, "Native Q3 flipflop animation divides by zero");
            int32_t reflected = (int32_t)((int64_t)offset % a->num_frames);
            state->frame = difference(difference(sum(a->first_frame, a->num_frames), 1), reflected);
        }
        else state->frame = sum(a->first_frame, offset);
        if (time > state->frame_time) state->frame_time = time;
    }
    if (state->frame_time > sum(time, 200)) state->frame_time = time;
    if (state->old_frame_time > time) state->old_frame_time = time;
    state->back_lerp = state->frame_time == state->old_frame_time ? 0
        : add(1, -divide((float)difference(time, state->old_frame_time),
                        (float)difference(state->frame_time, state->old_frame_time)));
    return true;
}

static float angle_mod(float angle) {
    return mul((float)((uint32_t)integer(mul(angle, 65536.0f / 360.0f)) & 65535u),
        360.0f / 65536.0f);
}
static float angle_subtract(float first, float second) {
    float angle = add(first, -second);
    while (angle > 180) angle = add(angle, -360);
    while (angle < -180) angle = add(angle, 360);
    return angle;
}
void q3n_angles_axis(qa_vec3 angles, qa_vec3 axis[3]) {
    const float radians = 0.01745329251994329577f;
    float yaw = mul(angles.y, radians), pitch = mul(angles.x, radians), roll = mul(angles.z, radians);
    float sy = (float)sin((double)yaw), cy = (float)cos((double)yaw);
    float sp = (float)sin((double)pitch), cp = (float)cos((double)pitch);
    float sr = (float)sin((double)roll), cr = (float)cos((double)roll);
    axis[0] = qa_v3(mul(cp, cy), mul(cp, sy), -sp);
    float rp = mul(-sr, sp);
    axis[1] = qa_v3(-add(mul(rp, cy), mul(-cr, -sy)),
        -add(mul(rp, sy), mul(-cr, cy)), -mul(-sr, cp));
    rp = mul(cr, sp);
    axis[2] = qa_v3(add(mul(rp, cy), mul(-sr, -sy)),
        add(mul(rp, sy), mul(-sr, cy)), mul(cr, cp));
}
static void swing(float destination, float tolerance, float clamp, float speed,
    int32_t milliseconds, float *angle, bool *swinging) {
    if (!*swinging) {
        float delta = angle_subtract(*angle, destination);
        if (delta > tolerance || delta < -tolerance) *swinging = true;
    }
    if (!*swinging) return;
    float delta = angle_subtract(destination, *angle), distance = fabsf(delta);
    float scale = distance < mul(tolerance, 0.5f) ? 0.5f : distance < tolerance ? 1 : 2;
    float move = mul(mul((float)milliseconds, scale), delta >= 0 ? speed : -speed);
    if ((delta >= 0 && move >= delta) || (delta < 0 && move <= delta)) {
        move = delta; *swinging = false;
    }
    *angle = angle_mod(add(*angle, move));
    delta = angle_subtract(destination, *angle);
    if (delta > clamp) *angle = angle_mod(add(destination, -add(clamp, -1)));
    else if (delta < -clamp) *angle = angle_mod(add(destination, add(clamp, -1)));
}
static qa_vec3 subtract_angles(qa_vec3 a, qa_vec3 b) {
    return qa_v3(angle_subtract(a.x, b.x), angle_subtract(a.y, b.y), angle_subtract(a.z, b.z));
}
static float dot(qa_vec3 a, qa_vec3 b) {
    return add(add(mul(a.x, b.x), mul(a.y, b.y)), mul(a.z, b.z));
}

bool q3n_player_angles(q3n_player_pose *state, const qa_player_animation_config *config,
    const qa_q3_entity *entity, qa_vec3 angles, int32_t time, int32_t milliseconds,
    float speed, q3n_pose_axes *out, qa_error *error) {
    qa_vec3 source_velocity = qa_v3(entity->pos.delta[0], entity->pos.delta[1], entity->pos.delta[2]);
    if (milliseconds < 0 || !isfinite(speed) || speed < 0 || !qa_vec_finite(angles) ||
        !qa_vec_finite(source_velocity)) return fail(error, "Invalid native Q3 player pose input");
    static const float offsets[8] = {0, 22, 45, -22, 0, 22, -45, -22};
    int32_t direction = entity->eFlags & 1 ? 0 : integer(entity->angles2[1]);
    if (direction < 0 || direction >= 8) return fail(error, "Bad player movement angle");
    qa_vec3 head = qa_v3(angles.x, angle_mod(angles.y), angles.z), legs = {0}, torso = {0};
    if ((entity->legsAnim & ~128) != 22 || (entity->torsoAnim & ~128) != 11) {
        state->torso.yawing = state->torso.pitching = state->legs.yawing = true;
    }
    swing(add(head.y, mul(0.25f, offsets[direction])), 25, 90, speed,
        milliseconds, &state->torso.yaw_angle, &state->torso.yawing);
    swing(add(head.y, offsets[direction]), 40, 90, speed,
        milliseconds, &state->legs.yaw_angle, &state->legs.yawing);
    torso.y = state->torso.yaw_angle; legs.y = state->legs.yaw_angle;
    float pitch = mul(head.x > 180 ? add(-360, head.x) : head.x, 0.75f);
    swing(pitch, 15, 30, 0.1f, milliseconds, &state->torso.pitch_angle, &state->torso.pitching);
    torso.x = config->fixed_torso ? 0 : state->torso.pitch_angle;
    float velocity_length = (float)sqrt((double)dot(source_velocity, source_velocity));
    if (velocity_length != 0) {
        float inverse_length = divide(1, velocity_length);
        qa_vec3 velocity = qa_v3(mul(source_velocity.x, inverse_length),
            mul(source_velocity.y, inverse_length), mul(source_velocity.z, inverse_length));
        float lean = mul(velocity_length, 0.05f); qa_vec3 axis[3]; q3n_angles_axis(legs, axis);
        legs.x = add(legs.x, mul(lean, dot(velocity, axis[0])));
        legs.z = add(legs.z, -mul(lean, dot(velocity, axis[1])));
    }
    if (config->fixed_legs) legs = qa_v3(0, torso.y, 0);
    int32_t elapsed = difference(time, state->pain_time);
    if (elapsed < 200) {
        float roll = mul(20, add(1, -divide((float)elapsed, 200)));
        torso.z = add(torso.z, state->pain_direction ? roll : -roll);
    }
    q3n_angles_axis(legs, out->legs);
    q3n_angles_axis(subtract_angles(torso, legs), out->torso);
    q3n_angles_axis(subtract_angles(head, torso), out->head);
    return true;
}
