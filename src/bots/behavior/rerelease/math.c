/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/rerelease/math.h"
#include "qa/core/math.h"
#include <math.h>
qa_bot_vec3 qa_bvec(double x, double y, double z) { return (qa_bot_vec3){x, y, z}; }
qa_bot_vec3 qa_bvec_copy(qa_bot_vec3 v) { return v; }
qa_bot_vec3 qa_bvec_add(qa_bot_vec3 a, qa_bot_vec3 b) { return qa_add3_double(a, b); }
qa_bot_vec3 qa_bvec_sub(qa_bot_vec3 a, qa_bot_vec3 b) { return qa_sub3_double(a, b); }
qa_bot_vec3 qa_bvec_scale(qa_bot_vec3 v, double s) { return qa_scale3_double(v, s); }
qa_bot_vec3 qa_bvec_ma(qa_bot_vec3 b, double s, qa_bot_vec3 d) { return qa_ma3_double(b, s, d); }
double qa_bvec_dot(qa_bot_vec3 a, qa_bot_vec3 b) { return qa_dot3_double(a, b); }
double qa_bvec_length(qa_bot_vec3 v) { return qa_length3_double(v); }
double qa_bvec_length2d(qa_bot_vec3 v) { return sqrt(v.x * v.x + v.y * v.y); }
double qa_bvec_distance(qa_bot_vec3 a, qa_bot_vec3 b) { return qa_distance3_double(a, b); }
double qa_bvec_distance2d(qa_bot_vec3 a, qa_bot_vec3 b) { double dx = a.x - b.x, dy = a.y - b.y; return sqrt(dx * dx + dy * dy); }
qa_bot_vec3 qa_bvec_normalized(qa_bot_vec3 v) { return qa_normalize3_double_or_zero(v); }
double qa_bot_angle_mod(double a) { return fmod(fmod(a, 360) + 360, 360); }
double qa_bot_angle_delta(double from, double to) { double d = qa_bot_angle_mod(to - from); if (d > 180) d -= 360; return d; }
qa_bot_pitch_yaw qa_bot_vector_to_angles(qa_bot_vec3 dir) { qa_vec3 a = qa_vector_to_angles_with_rule(dir, QA_VECTOR_TO_ANGLES_BOT); return (qa_bot_pitch_yaw){a.x, a.y}; }
qa_angle_vectors qa_bot_angle_vectors(double p, double y, double r) { return qa_math_angle_vectors_rule((qa_vec3){p, y, r}, QA_ANGLE_RULE_BOT); }
double qa_bot_angle_between(double p, double y, qa_bot_vec3 dir) { qa_angle_vectors av = qa_bot_angle_vectors(p, y, 0); qa_bot_vec3 d = qa_bvec_normalized(dir); double dot = qa_bvec_dot(av.forward, d); if (dot < -1) dot = -1; if (dot > 1) dot = 1; return acos(dot) * (180 / acos(-1.0)); }
double qa_bot_clamp(double v, double lo, double hi) { if (v < lo) return lo; if (v > hi) return hi; return v; }
