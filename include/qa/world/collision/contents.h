/* Port of src/world/collision/contents.ts. CollisionFamily reuses
 * qa_collision_family (spatial mirror, exact strings). Q1 trace
 * contents for adapt: qa_adapt_trace_result_ex takes TS `contents`
 * extension (has_contents false = absent). GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_CONTENTS_H
#define QA_WORLD_COLLISION_CONTENTS_H
#include <stdbool.h>
#include "qa/contracts/scene.h"
#include "qa/world/spatial/index.h"
double qa_convert_contents(double contents, qa_collision_family from, qa_collision_family to);
double qa_actor_contents(qa_actor_collision collision, qa_collision_family to);
bool qa_contents_block(double contents, qa_collision_family family, qa_trace_policy policy);
bool qa_blocks_q1_contents(double contents, qa_trace_policy policy);
double qa_geometry_mask(qa_trace_policy policy, qa_collision_family family);
/* TS Q2_NULL_SURFACE: cmodel.c's nullsurface. */
extern const qa_q2_surface_info qa_q2_null_surface;
qa_trace_result qa_adapt_trace_result(qa_trace_result result, qa_trace_policy policy);
qa_trace_result qa_adapt_trace_result_ex(qa_trace_result result, qa_trace_policy policy, bool has_q1_contents, double q1_contents);
qa_point_contents_result qa_adapt_point_contents(qa_point_contents_result result, qa_trace_policy policy);
double qa_convert_surface_flags(double flags, qa_collision_family from, qa_collision_family to);
#endif
