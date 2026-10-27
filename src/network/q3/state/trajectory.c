/* Port of src/network/q3/state/trajectory.ts. GPL-2.0-or-later. */
#include "qa/network/q3/state/trajectory.h"
#include <string.h>
void qa_q3_wire_trajectory_init(qa_q3_wire_trajectory *t, int32_t type) {
  memset(t, 0, sizeof *t);
  t->type = type;
}
void qa_q3_wire_trajectory_copy(qa_q3_wire_trajectory *dst, const qa_q3_wire_trajectory *src) { *dst = *src; }
