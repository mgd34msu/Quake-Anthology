/* Port of src/content/q3/base/shared/trajectory.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/shared/trajectory.h"
#include <math.h>
#include <stdio.h>
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/core/common_error.h"
#include "qa/core/math.h"

static float fr(double v) { return (float)v; }
/* JS (x)|0: ToInt32. */
static int32_t to_int32(double v) {
  if (!isfinite(v)) return 0;
  double m = fmod(trunc(v), 4294967296.0);
  if (m >= 2147483648.0) m -= 4294967296.0;
  if (m < -2147483648.0) m += 4294967296.0;
  return (int32_t)m;
}
static double seconds(double milliseconds) {
  return (double)fr((double)fr(milliseconds) * (double)fr(0.001));
}
static double periodic_radians(qa_q3_base_trajectory tr, double at_time) {
  double fraction = (double)fr((double)fr((double)to_int32(at_time - tr.time)) / (double)fr(tr.duration));
  return (double)fr((double)fr(fraction * (double)fr(3.141592653589793)) * 2);
}
static qa_vec3 vec(double x, double y, double z) { return qa_math_vec3(x, y, z); }

qa_vec3 qa_q3_base_evaluate_trajectory(qa_q3_base_trajectory tr, double at_time) {
  /* TS switch on numeric type. */
  if (tr.type == QA_Q3_BASE_TR_STATIONARY || tr.type == QA_Q3_BASE_TR_INTERPOLATE)
    return vec(tr.base.x, tr.base.y, tr.base.z);
  if (tr.type == QA_Q3_BASE_TR_LINEAR) {
    double dt = seconds((double)to_int32(at_time - tr.time));
    return qa_add3(tr.base, qa_scale3(tr.delta, dt));
  }
  if (tr.type == QA_Q3_BASE_TR_SINE) {
    double phase = (double)fr(sin(periodic_radians(tr, at_time)));
    return qa_add3(tr.base, qa_scale3(tr.delta, phase));
  }
  if (tr.type == QA_Q3_BASE_TR_LINEAR_STOP) {
    double end = (double)to_int32(tr.time + tr.duration);
    double time = at_time > end ? end : at_time;
    double dt = seconds((double)to_int32(time - tr.time));
    if (dt < 0) dt = 0;
    return qa_add3(tr.base, qa_scale3(tr.delta, dt));
  }
  if (tr.type == QA_Q3_BASE_TR_GRAVITY) {
    double dt = seconds((double)to_int32(at_time - tr.time));
    qa_vec3 result = qa_add3(tr.base, qa_scale3(tr.delta, dt));
    double fall = (double)fr((double)fr(0.5 * QA_Q3_BASE_DEFAULT_GRAVITY * dt) * dt);
    return vec(result.x, result.y, result.z - fall);
  }
  char msg[128];
  snprintf(msg, sizeof msg, "BG_EvaluateTrajectory: unknown trType: %g", tr.time);
  qa_common_error(QA_COMMON_ERROR_DROP, msg);
}
qa_vec3 qa_q3_base_evaluate_trajectory_delta(qa_q3_base_trajectory tr, double at_time) {
  if (tr.type == QA_Q3_BASE_TR_STATIONARY || tr.type == QA_Q3_BASE_TR_INTERPOLATE)
    return vec(0, 0, 0);
  if (tr.type == QA_Q3_BASE_TR_LINEAR)
    return vec(tr.delta.x, tr.delta.y, tr.delta.z);
  if (tr.type == QA_Q3_BASE_TR_SINE) {
    double phase = (double)fr((double)fr(cos(periodic_radians(tr, at_time))) * 0.5);
    return qa_scale3(tr.delta, phase);
  }
  if (tr.type == QA_Q3_BASE_TR_LINEAR_STOP)
    return at_time > (double)to_int32(tr.time + tr.duration) ? vec(0, 0, 0) : vec(tr.delta.x, tr.delta.y, tr.delta.z);
  if (tr.type == QA_Q3_BASE_TR_GRAVITY) {
    double dt = seconds((double)to_int32(at_time - tr.time));
    return vec(tr.delta.x, tr.delta.y, tr.delta.z - (double)fr(QA_Q3_BASE_DEFAULT_GRAVITY * dt));
  }
  char msg[128];
  snprintf(msg, sizeof msg, "BG_EvaluateTrajectoryDelta: unknown trType: %g", tr.time);
  qa_common_error(QA_COMMON_ERROR_DROP, msg);
}
