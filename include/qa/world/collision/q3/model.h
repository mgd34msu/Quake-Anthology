/* Port of src/world/collision/q3/model.ts. Heap models; counters
 * NULL creates owned counters (field remains accessible).
 * GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q3_MODEL_H
#define QA_WORLD_COLLISION_Q3_MODEL_H
#include "qa/contracts/math.h"
#include "qa/world/collision/q3/counters.h"
#include "qa/world/collision/q3/map_resource.h"
#include "qa/world/collision/q3/world.h"
typedef qa_q3_trace_query qa_temporary_trace_query; /* modelIndex ignored */
typedef struct qa_temporary_collision_model qa_temporary_collision_model;
struct qa_temporary_collision_model {
  const char *kind; qa_bounds bounds;
  qa_collision_counters *counters; qa_collision_counters owned_counters;
  qa_collision_box_storage *storage;
  /* capsule target PreparedShape */
  qa_vec3 target_center, target_mins, target_extents, target_offset;
  double target_radius, target_halfheight;
};
qa_temporary_collision_model *qa_temporary_collision_model_create(const char *kind, qa_bounds bounds, qa_collision_counters *counters, qa_collision_box_storage *storage);
void qa_temporary_collision_model_destroy(qa_temporary_collision_model *m);
qa_bounds qa_temporary_collision_model_bounds(qa_temporary_collision_model *m);
double qa_temporary_model_point_contents(qa_temporary_collision_model *m, qa_vec3 point);
double qa_temporary_model_transformed_point_contents(qa_temporary_collision_model *m, qa_vec3 point, qa_vec3 origin, qa_vec3 angles);
qa_q3_trace_result qa_temporary_model_trace(qa_temporary_collision_model *m, qa_temporary_trace_query query);
qa_source_trace_result qa_temporary_model_trace_source(qa_temporary_collision_model *m, qa_temporary_trace_query query);
qa_q3_trace_result qa_temporary_model_transformed_trace(qa_temporary_collision_model *m, qa_temporary_trace_query query, qa_vec3 origin, qa_vec3 angles);
qa_source_trace_result qa_temporary_model_transformed_trace_source(qa_temporary_collision_model *m, qa_temporary_trace_query query, qa_vec3 origin, qa_vec3 angles);
typedef struct qa_q3_model_transform { qa_vec3 origin, angles; } qa_q3_model_transform;
qa_source_trace_result qa_temporary_model_trace_capsule_replacement(qa_temporary_collision_model *m, qa_temporary_trace_query query, qa_collision_world *world, const qa_q3_model_transform *transform);
qa_temporary_collision_model *qa_create_box_model(qa_bounds bounds, qa_collision_counters *counters, qa_collision_box_storage *storage);
qa_temporary_collision_model *qa_create_capsule_model(qa_bounds bounds, qa_collision_counters *counters, qa_collision_box_storage *storage);
#endif
