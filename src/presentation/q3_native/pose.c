#include "pose.h"

#include <limits.h>
#include <string.h>

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
        int32_t offset = integer(((float)quotient * speed));
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
        : (1 + -((float)difference(time, state->old_frame_time) / (float)difference(state->frame_time, state->old_frame_time)));
    return true;
}

static float angle_mod(float angle) {
    return ((float)((uint32_t)integer((angle * (65536.0f / 360.0f))) & 65535u) * (360.0f / 65536.0f));
}
static float angle_subtract(float first, float second) {
    float angle = (first + -second);
    while (angle > 180) angle = (angle + -360);
    while (angle < -180) angle = (angle + 360);
    return angle;
}
void q3n_angles_axis(qa_vec3 angles, qa_vec3 axis[3]) {
    const float radians = 0.01745329251994329577f;
    float yaw = (angles.y * radians), pitch = (angles.x * radians), roll = (angles.z * radians);
    float sy = (float)sin((double)yaw), cy = (float)cos((double)yaw);
    float sp = (float)sin((double)pitch), cp = (float)cos((double)pitch);
    float sr = (float)sin((double)roll), cr = (float)cos((double)roll);
    axis[0] = qa_v3((cp * cy), (cp * sy), -sp);
    float rp = (-sr * sp);
    axis[1] = qa_v3(-((rp * cy) + (-cr * -sy)),
        -((rp * sy) + (-cr * cy)), -(-sr * cp));
    rp = (cr * sp);
    axis[2] = qa_v3(((rp * cy) + (-sr * -sy)),
        ((rp * sy) + (-sr * cy)), (cr * cp));
}
static void swing(float destination, float tolerance, float clamp, float speed,
    int32_t milliseconds, float *angle, bool *swinging) {
    if (!*swinging) {
        float delta = angle_subtract(*angle, destination);
        if (delta > tolerance || delta < -tolerance) *swinging = true;
    }
    if (!*swinging) return;
    float delta = angle_subtract(destination, *angle), distance = fabsf(delta);
    float scale = distance < (tolerance * 0.5f) ? 0.5f : distance < tolerance ? 1 : 2;
    float move = (((float)milliseconds * scale) * (delta >= 0 ? speed : -speed));
    if ((delta >= 0 && move >= delta) || (delta < 0 && move <= delta)) {
        move = delta; *swinging = false;
    }
    *angle = angle_mod((*angle + move));
    delta = angle_subtract(destination, *angle);
    if (delta > clamp) *angle = angle_mod((destination + -(clamp + -1)));
    else if (delta < -clamp) *angle = angle_mod((destination + (clamp + -1)));
}
static qa_vec3 subtract_angles(qa_vec3 a, qa_vec3 b) {
    return qa_v3(angle_subtract(a.x, b.x), angle_subtract(a.y, b.y), angle_subtract(a.z, b.z));
}
static float dot(qa_vec3 a, qa_vec3 b) {
    return (((a.x * b.x) + (a.y * b.y)) + (a.z * b.z));
}

bool q3n_player_angles_pose(q3n_player_pose *state, const qa_player_animation_config *config,
    const q3n_pose_entity *entity, qa_vec3 angles, int32_t time, int32_t milliseconds,
    float speed, q3n_pose_axes *out, qa_error *error) {
    qa_vec3 source_velocity = entity->velocity;
    if (milliseconds < 0 || !isfinite(speed) || speed < 0 || !qa_vec_finite(angles) ||
        !qa_vec_finite(source_velocity)) return fail(error, "Invalid native Q3 player pose input");
    static const float offsets[8] = {0, 22, 45, -22, 0, 22, -45, -22};
    int32_t direction = entity->flags & 1 ? 0 : entity->movement_direction;
    if (direction < 0 || direction >= 8) return fail(error, "Bad player movement angle");
    qa_vec3 head = qa_v3(angles.x, angle_mod(angles.y), angles.z), legs = {0}, torso = {0};
    if ((entity->legs_animation & ~128) != 22 || (entity->torso_animation & ~128) != 11) {
        state->torso.yawing = state->torso.pitching = state->legs.yawing = true;
    }
    swing((head.y + (0.25f * offsets[direction])), 25, 90, speed,
        milliseconds, &state->torso.yaw_angle, &state->torso.yawing);
    swing((head.y + offsets[direction]), 40, 90, speed,
        milliseconds, &state->legs.yaw_angle, &state->legs.yawing);
    torso.y = state->torso.yaw_angle; legs.y = state->legs.yaw_angle;
    float pitch = ((head.x > 180 ? (-360 + head.x) : head.x) * 0.75f);
    swing(pitch, 15, 30, 0.1f, milliseconds, &state->torso.pitch_angle, &state->torso.pitching);
    torso.x = config->fixed_torso ? 0 : state->torso.pitch_angle;
    float velocity_length = (float)sqrt((double)dot(source_velocity, source_velocity));
    if (velocity_length != 0) {
        float inverse_length = (1 / velocity_length);
        qa_vec3 velocity = qa_v3((source_velocity.x * inverse_length),
            (source_velocity.y * inverse_length), (source_velocity.z * inverse_length));
        float lean = (velocity_length * 0.05f); qa_vec3 axis[3]; q3n_angles_axis(legs, axis);
        legs.x = (legs.x + (lean * dot(velocity, axis[0])));
        legs.z = (legs.z + -(lean * dot(velocity, axis[1])));
    }
    if (config->fixed_legs) legs = qa_v3(0, torso.y, 0);
    int32_t elapsed = difference(time, state->pain_time);
    if (elapsed < 200) {
        float roll = (20 * (1 + -((float)elapsed / 200)));
        torso.z = (torso.z + (state->pain_direction ? roll : -roll));
    }
    q3n_angles_axis(legs, out->legs);
    q3n_angles_axis(subtract_angles(torso, legs), out->torso);
    q3n_angles_axis(subtract_angles(head, torso), out->head);
    return true;
}

bool q3n_player_angles(q3n_player_pose *state, const qa_player_animation_config *config,
    const qa_q3_entity *entity, qa_vec3 angles, int32_t time, int32_t milliseconds,
    float speed, q3n_pose_axes *out, qa_error *error)
{
    q3n_pose_entity pose = {.flags = (uint32_t)entity->eFlags,
        .velocity = qa_v3(entity->pos.delta[0], entity->pos.delta[1], entity->pos.delta[2]),
        .movement_direction = integer(entity->angles2[1]),
        .legs_animation = entity->legsAnim, .torso_animation = entity->torsoAnim};
    return q3n_player_angles_pose(state, config, &pose, angles, time, milliseconds, speed, out, error);
}
