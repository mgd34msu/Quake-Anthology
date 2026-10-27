/* Port of src/world/collision/q3/counters.ts. GPL-2.0-or-later. */
#include "qa/world/collision/q3/counters.h"
void qa_collision_counters_init(qa_collision_counters*c){qa_collision_counters_reset(c);}
void qa_collision_counters_reset(qa_collision_counters*c){c->c_traces=0;c->c_brush_traces=0;c->c_patch_traces=0;c->c_pointcontents=0;}
