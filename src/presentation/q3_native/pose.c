#include "pose.h"
#include "qa/binary.h"
#include "qa/text.h"

#include <limits.h>
#include <string.h>

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
    state->animation_time = qa_i32_add(state->frame_time, a->initial_lerp); return true;
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
            : qa_i32_add(state->old_frame_time, a->frame_lerp);
        double quotient = trunc((double)qa_i32_sub(state->frame_time, state->animation_time) / a->frame_lerp);
        int32_t offset = qa_source_float_to_i32(((float)quotient * speed));
        int32_t count = a->flipflop ? qa_i32_from_u32((uint32_t)a->num_frames * 2u) : a->num_frames;
        if (offset >= count) {
            offset = qa_i32_sub(offset, count);
            if (a->loop_frames) {
                offset = (int32_t)((int64_t)offset % a->loop_frames);
                offset = qa_i32_add(offset, qa_i32_sub(a->num_frames, a->loop_frames));
            } else { offset = qa_i32_sub(count, 1); state->frame_time = time; }
        }
        if (a->reversed)
            state->frame = qa_i32_sub(qa_i32_sub(qa_i32_add(a->first_frame, a->num_frames), 1), offset);
        else if (a->flipflop && offset >= a->num_frames) {
            if (!a->num_frames) return fail(error, "Native Q3 flipflop animation divides by zero");
            int32_t reflected = (int32_t)((int64_t)offset % a->num_frames);
            state->frame = qa_i32_sub(qa_i32_sub(qa_i32_add(a->first_frame, a->num_frames), 1), reflected);
        }
        else state->frame = qa_i32_add(a->first_frame, offset);
        if (time > state->frame_time) state->frame_time = time;
    }
    if (state->frame_time > qa_i32_add(time, 200)) state->frame_time = time;
    if (state->old_frame_time > time) state->old_frame_time = time;
    state->back_lerp = state->frame_time == state->old_frame_time ? 0
        : (1 + -((float)qa_i32_sub(time, state->old_frame_time) / (float)qa_i32_sub(state->frame_time, state->old_frame_time)));
    return true;
}

static void swing(float destination, float tolerance, float clamp, float speed,
    int32_t milliseconds, float *angle, bool *swinging) {
    if (!*swinging) {
        float delta = qa_angle_subtract(*angle, destination);
        if (delta > tolerance || delta < -tolerance) *swinging = true;
    }
    if (!*swinging) return;
    float delta = qa_angle_subtract(destination, *angle), distance = fabsf(delta);
    float scale = distance < (tolerance * 0.5f) ? 0.5f : distance < tolerance ? 1 : 2;
    float move = (((float)milliseconds * scale) * (delta >= 0 ? speed : -speed));
    if ((delta >= 0 && move >= delta) || (delta < 0 && move <= delta)) {
        move = delta; *swinging = false;
    }
    *angle = qa_angle_mod((*angle + move),QA_ANGLE_WORD_VM);
    delta = qa_angle_subtract(destination, *angle);
    if (delta > clamp) *angle = qa_angle_mod((destination + -(clamp + -1)),QA_ANGLE_WORD_VM);
    else if (delta < -clamp) *angle = qa_angle_mod((destination + (clamp + -1)),QA_ANGLE_WORD_VM);
}
static qa_vec3 subtract_angles(qa_vec3 a, qa_vec3 b) {
    return qa_v3(qa_angle_subtract(a.x, b.x), qa_angle_subtract(a.y, b.y), qa_angle_subtract(a.z, b.z));
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
    qa_vec3 head = qa_v3(angles.x, qa_angle_mod(angles.y,QA_ANGLE_WORD_VM), angles.z), legs = {0}, torso = {0};
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
    float velocity_length = (float)sqrt((double)qa_vec_dot(source_velocity, source_velocity));
    if (velocity_length != 0) {
        float inverse_length = (1 / velocity_length);
        qa_vec3 velocity = qa_v3((source_velocity.x * inverse_length),
            (source_velocity.y * inverse_length), (source_velocity.z * inverse_length));
        float lean = (velocity_length * 0.05f); qa_vec3 axis[3]; qa_angle_axis(legs, axis,QA_ANGLE_VM_MATH);
        legs.x = (legs.x + (lean * qa_vec_dot(velocity, axis[0])));
        legs.z = (legs.z + -(lean * qa_vec_dot(velocity, axis[1])));
    }
    if (config->fixed_legs) legs = qa_v3(0, torso.y, 0);
    int32_t elapsed = qa_i32_sub(time, state->pain_time);
    if (elapsed < 200) {
        float roll = (20 * (1 + -((float)elapsed / 200)));
        torso.z = (torso.z + (state->pain_direction ? roll : -roll));
    }
    qa_angle_axis(legs, out->legs,QA_ANGLE_VM_MATH);
    qa_angle_axis(subtract_angles(torso, legs), out->torso,QA_ANGLE_VM_MATH);
    qa_angle_axis(subtract_angles(head, torso), out->head,QA_ANGLE_VM_MATH);
    return true;
}

bool q3n_player_angles(q3n_player_pose *state, const qa_player_animation_config *config,
    const qa_q3_entity *entity, qa_vec3 angles, int32_t time, int32_t milliseconds,
    float speed, q3n_pose_axes *out, qa_error *error)
{
    q3n_pose_entity pose = {.flags = (uint32_t)entity->eFlags,
        .velocity = qa_v3(entity->pos.delta[0], entity->pos.delta[1], entity->pos.delta[2]),
        .movement_direction = qa_source_float_to_i32(entity->angles2[1]),
        .legs_animation = entity->legsAnim, .torso_animation = entity->torsoAnim};
    return q3n_player_angles_pose(state, config, &pose, angles, time, milliseconds, speed, out, error);
}
