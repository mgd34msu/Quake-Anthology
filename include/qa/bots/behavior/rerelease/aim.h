/* Port of src/bots/behavior/rerelease/aim.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_AIM_H
#define QA_BOTS_BEHAVIOR_RERELEASE_AIM_H
#include "qa/bots/behavior/rerelease/data/botdata.h"
#include "qa/bots/behavior/rerelease/math.h"
typedef struct qa_bot_aim_state { double pitch, yaw, pitch_velocity, yaw_velocity, modifier_until; } qa_bot_aim_state;
qa_bot_aim_state qa_new_aim_state(double pitch, double yaw);
qa_bot_vec3 qa_aim_lead_point(qa_bot_vec3 target, qa_bot_vec3 target_velocity, qa_bot_aiming_settings settings);
qa_bot_pitch_yaw qa_aim_step(qa_bot_aim_state *state, qa_bot_vec3 ideal_dir, qa_bot_aiming_settings settings, double dt, double now);
double qa_aim_error(const qa_bot_aim_state *state, qa_bot_vec3 ideal_dir);
#endif
