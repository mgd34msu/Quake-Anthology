/* Port of src/content/q3/foundation/player-pose.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_FOUNDATION_PLAYER_POSE_H
#define QA_CONTENT_Q3_FOUNDATION_PLAYER_POSE_H
#include <stdbool.h>
#include "qa/contracts/math.h"
#include "qa/content/q3/foundation/animation.h"
#include "qa/content/q3/foundation/animation_config.h"

typedef struct qa_q3_pose_lerp_frame {
  qa_q3_lerp_frame base;
  double yaw_angle; bool yawing; double pitch_angle; bool pitching;
} qa_q3_pose_lerp_frame;
typedef struct qa_q3_player_pose_state {
  qa_q3_pose_lerp_frame legs, torso; double pain_time; bool pain_direction;
} qa_q3_player_pose_state;
typedef struct qa_q3_swing_angles_input {
  double destination, swing_tolerance, clamp_tolerance, speed, frame_time_ms, angle; bool swinging;
} qa_q3_swing_angles_input;
typedef struct qa_q3_swing_angles_result { double angle; bool swinging; } qa_q3_swing_angles_result;
typedef struct qa_q3_pain_twitch_input { double time_ms, pain_time; bool pain_direction; } qa_q3_pain_twitch_input;
typedef struct qa_q3_pose_entity_state {
  double e_flags; qa_vec3 velocity; double movement_direction, legs_anim, torso_anim;
} qa_q3_pose_entity_state;
typedef struct qa_q3_calculate_player_pose_input {
  qa_q3_pose_entity_state entity; bool fixed_legs, fixed_torso;
  qa_vec3 lerp_angles; double time_ms, frame_time_ms, swing_speed;
} qa_q3_calculate_player_pose_input;
typedef struct qa_q3_player_pose { qa_axis legs, torso, head; } qa_q3_player_pose;

qa_q3_player_pose_state qa_q3_create_player_pose_state(void);
qa_q3_swing_angles_result qa_q3_swing_angles(qa_q3_swing_angles_input input);
qa_vec3 qa_q3_add_pain_twitch(qa_vec3 torso_angles, qa_q3_pain_twitch_input input);
qa_q3_player_pose qa_q3_calculate_player_pose(qa_q3_player_pose_state *state, qa_q3_calculate_player_pose_input input);
#endif
