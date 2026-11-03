/* Port of src/network/q3/state/trajectory.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_STATE_TRAJECTORY_H
#define QA_NETWORK_Q3_STATE_TRAJECTORY_H
#include <stdint.h>
#include "qa/contracts/math.h"
/* TS Trajectory<T>: trType_t wire storage preserves numeric mod tags. The
 * fields are TS numbers, so storage is double; content/q3/base/shared/
 * trajectory.ts declares the same shape and qa_q3_base_trajectory is this type. */
typedef struct qa_q3_wire_trajectory {
  double type;
  double time;
  double duration;
  qa_vec3 base;
  qa_vec3 delta;
} qa_q3_wire_trajectory;
void qa_q3_wire_trajectory_init(qa_q3_wire_trajectory *t, double type);
void qa_q3_wire_trajectory_copy(qa_q3_wire_trajectory *dst, const qa_q3_wire_trajectory *src);
#endif
