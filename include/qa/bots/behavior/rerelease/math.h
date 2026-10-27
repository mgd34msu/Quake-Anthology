/* Port of src/bots/behavior/rerelease/math.ts. BotVec3 is qa_vec3 (double).
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_MATH_H
#define QA_BOTS_BEHAVIOR_RERELEASE_MATH_H
#include "qa/contracts/math.h"
typedef qa_vec3 qa_bot_vec3;
qa_bot_vec3 qa_bvec(double x, double y, double z);
qa_bot_vec3 qa_bvec_copy(qa_bot_vec3 v);
qa_bot_vec3 qa_bvec_add(qa_bot_vec3 a, qa_bot_vec3 b);
qa_bot_vec3 qa_bvec_sub(qa_bot_vec3 a, qa_bot_vec3 b);
qa_bot_vec3 qa_bvec_scale(qa_bot_vec3 v, double s);
qa_bot_vec3 qa_bvec_ma(qa_bot_vec3 base, double scale, qa_bot_vec3 dir);
double qa_bvec_dot(qa_bot_vec3 a, qa_bot_vec3 b);
double qa_bvec_length(qa_bot_vec3 v);
double qa_bvec_length2d(qa_bot_vec3 v);
double qa_bvec_distance(qa_bot_vec3 a, qa_bot_vec3 b);
double qa_bvec_distance2d(qa_bot_vec3 a, qa_bot_vec3 b);
qa_bot_vec3 qa_bvec_normalized(qa_bot_vec3 v);
double qa_bot_angle_mod(double a);
double qa_bot_angle_delta(double from, double to);
typedef struct qa_bot_pitch_yaw { double pitch, yaw; } qa_bot_pitch_yaw;
qa_bot_pitch_yaw qa_bot_vector_to_angles(qa_bot_vec3 dir);
qa_angle_vectors qa_bot_angle_vectors(double pitch, double yaw, double roll);
double qa_bot_angle_between(double pitch, double yaw, qa_bot_vec3 dir);
double qa_bot_clamp(double v, double lo, double hi);
#endif
