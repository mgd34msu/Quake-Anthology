/* Port of src/content/q3/foundation/weapon-pose.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_FOUNDATION_WEAPON_POSE_H
#define QA_CONTENT_Q3_FOUNDATION_WEAPON_POSE_H
#include <stdbool.h>
#include "qa/contracts/math.h"
#include "qa/content/q3/foundation/animation_config.h"
typedef struct qa_q3_weapon_view_motion {
  qa_vec3 origin, angles; double time_milliseconds, horizontal_speed, bob_cycle, bob_fraction_sine, land_time, land_change;
} qa_q3_weapon_view_motion;
typedef struct qa_q3_weapon_view_pose_result { qa_vec3 origin, angles; } qa_q3_weapon_view_pose_result;
qa_q3_weapon_view_pose_result qa_q3_weapon_view_pose(qa_q3_weapon_view_motion input);
double qa_q3_torso_weapon_frame(const qa_q3_player_animation_config *config, double frame);
typedef struct qa_q3_weapon_barrel { double time, angle; bool spinning; } qa_q3_weapon_barrel;
typedef struct qa_q3_weapon_barrel_step_result { double angle; bool stopped; } qa_q3_weapon_barrel_step_result;
void qa_q3_weapon_barrel_init(qa_q3_weapon_barrel *barrel);
qa_q3_weapon_barrel_step_result qa_q3_weapon_barrel_step(qa_q3_weapon_barrel *barrel, double time_milliseconds, bool firing);
#endif
