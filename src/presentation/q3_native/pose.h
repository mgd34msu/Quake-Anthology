#ifndef QA_Q3_NATIVE_POSE_H
#define QA_Q3_NATIVE_POSE_H

#include "qa/model.h"
#include "qa/network_q3.h"

/* Native cgame owns these timers and swing angles. Animation records remain
 * in its retained client-info owner; no source entity or animation pointer is
 * duplicated here. */
typedef struct q3n_lerp_frame {
    int32_t old_frame, old_frame_time, frame, frame_time;
    float back_lerp;
    int32_t animation_number, animation_time;
    bool selected;
} q3n_lerp_frame;
typedef struct q3n_pose_frame {
    q3n_lerp_frame animation;
    float yaw_angle, pitch_angle;
    bool yawing, pitching;
} q3n_pose_frame;
typedef struct q3n_player_pose {
    q3n_pose_frame legs, torso;
    int32_t pain_time;
    bool pain_direction;
} q3n_player_pose;
typedef struct q3n_pose_axes { qa_vec3 legs[3], torso[3], head[3]; } q3n_pose_axes;
typedef struct q3n_pose_entity {
    uint32_t flags;
    qa_vec3 velocity;
    int32_t movement_direction, legs_animation, torso_animation;
} q3n_pose_entity;

bool q3n_lerp_clear(const qa_player_animation_config *, q3n_lerp_frame *,
    int32_t animation, int32_t time, qa_error *);
bool q3n_lerp_run(const qa_player_animation_config *, q3n_lerp_frame *,
    int32_t animation, int32_t time, float speed_scale, bool disabled, qa_error *);
bool q3n_player_angles(q3n_player_pose *, const qa_player_animation_config *,
    const qa_q3_entity *, qa_vec3 lerp_angles, int32_t time,
    int32_t frame_milliseconds, float swing_speed, q3n_pose_axes *, qa_error *);
bool q3n_player_angles_pose(q3n_player_pose *, const qa_player_animation_config *,
    const q3n_pose_entity *, qa_vec3 lerp_angles, int32_t time,
    int32_t frame_milliseconds, float swing_speed, q3n_pose_axes *, qa_error *);
void q3n_angles_axis(qa_vec3, qa_vec3 axis[3]);

#endif
