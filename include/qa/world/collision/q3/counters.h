/* Port of src/world/collision/q3/counters.ts. GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q3_COUNTERS_H
#define QA_WORLD_COLLISION_Q3_COUNTERS_H
#include <stdint.h>
typedef struct qa_collision_counters {
  int32_t c_traces, c_brush_traces, c_patch_traces, c_pointcontents;
} qa_collision_counters;
void qa_collision_counters_init(qa_collision_counters *c);
void qa_collision_counters_reset(qa_collision_counters *c);
#endif
