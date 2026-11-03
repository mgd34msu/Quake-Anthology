/* Port of src/content/q3/base/shared/trajectory.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_SHARED_TRAJECTORY_H
#define QA_CONTENT_Q3_BASE_SHARED_TRAJECTORY_H
#include "qa/contracts/math.h"
#include "qa/network/q3/state/trajectory.h"
typedef enum qa_q3_base_trajectory_type {
  QA_Q3_BASE_TR_STATIONARY = 0, QA_Q3_BASE_TR_INTERPOLATE = 1, QA_Q3_BASE_TR_LINEAR = 2,
  QA_Q3_BASE_TR_LINEAR_STOP = 3, QA_Q3_BASE_TR_SINE = 4, QA_Q3_BASE_TR_GRAVITY = 5
} qa_q3_base_trajectory_type;
/* TS Trajectory<number>: the same shape as network/q3/state/trajectory.ts
 * (numeric type/time/duration as double), so one C type serves both. */
typedef qa_q3_wire_trajectory qa_q3_base_trajectory;
qa_vec3 qa_q3_base_evaluate_trajectory(qa_q3_base_trajectory tr, double at_time);
qa_vec3 qa_q3_base_evaluate_trajectory_delta(qa_q3_base_trajectory tr, double at_time);
#endif
