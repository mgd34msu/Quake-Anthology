/* Port of src/content/q3/presentation/collision-host.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_PRESENTATION_COLLISION_HOST_H
#define QA_CONTENT_Q3_PRESENTATION_COLLISION_HOST_H
#include "qa/world/collision/q3/world.h"
/* TS re-exports world/collision/q3/world TraceQuery / TraceResult. */
typedef qa_q3_trace_query qa_q3_pres_trace_query;
typedef qa_q3_trace_result qa_q3_pres_trace_result;
/* TS CollisionWorld = Pick<Q3CollisionWorld, "trace"|"pointContents"|
 * "transformedTrace"|"transformedPointContents"|"counters">: methods are
 * supplied by the selected map's collision adapter. */
typedef struct qa_q3_pres_collision_world {
  void *ctx;
  qa_q3_trace_result (*trace)(void *ctx, qa_q3_trace_query query);
  double (*point_contents)(void *ctx, qa_vec3 point, double model_index);
  qa_q3_trace_result (*transformed_trace)(void *ctx, qa_q3_trace_query query, qa_vec3 origin, qa_vec3 angles);
  double (*transformed_point_contents)(void *ctx, qa_vec3 point, double model_index, qa_vec3 origin, qa_vec3 angles);
  qa_collision_counters *counters;
} qa_q3_pres_collision_world;
/* Adapter over a landed qa_collision_world (counters borrows world->counters). */
qa_q3_pres_collision_world qa_q3_pres_collision_world_from_world(qa_collision_world *world);
#endif
