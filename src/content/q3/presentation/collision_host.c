/* Port of src/content/q3/presentation/collision-host.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/collision_host.h"
static qa_q3_trace_result trace_cb(void *ctx, qa_q3_trace_query query) {
  return qa_collision_world_trace((qa_collision_world *)ctx, query);
}
static double point_contents_cb(void *ctx, qa_vec3 point, double model_index) {
  return qa_collision_world_point_contents((qa_collision_world *)ctx, point, model_index);
}
static qa_q3_trace_result transformed_trace_cb(void *ctx, qa_q3_trace_query query, qa_vec3 origin, qa_vec3 angles) {
  return qa_collision_world_transformed_trace((qa_collision_world *)ctx, query, origin, angles);
}
static double transformed_point_contents_cb(void *ctx, qa_vec3 point, double model_index, qa_vec3 origin, qa_vec3 angles) {
  return qa_collision_world_transformed_point_contents((qa_collision_world *)ctx, point, model_index, origin, angles);
}
qa_q3_pres_collision_world qa_q3_pres_collision_world_from_world(qa_collision_world *world) {
  qa_q3_pres_collision_world host = {world, trace_cb, point_contents_cb, transformed_trace_cb,
                                     transformed_point_contents_cb, world->counters};
  return host;
}
