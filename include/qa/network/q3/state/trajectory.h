/* Port of src/network/q3/state/trajectory.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_STATE_TRAJECTORY_H
#define QA_NETWORK_Q3_STATE_TRAJECTORY_H
#include <stdint.h>
#include "qa/contracts/math.h"
/* TS Trajectory<T>: trType_t wire storage preserves numeric mod tags. */
typedef struct qa_q3_wire_trajectory {
  int32_t type;
  int32_t time;
  int32_t duration;
  qa_vec3 base;
  qa_vec3 delta;
} qa_q3_wire_trajectory;
void qa_q3_wire_trajectory_init(qa_q3_wire_trajectory *t, int32_t type);
void qa_q3_wire_trajectory_copy(qa_q3_wire_trajectory *dst, const qa_q3_wire_trajectory *src);
#endif
