/* Port of src/world/collision/index.ts (SharedSceneQueries).
 * Actor-state/collision readers are fn+ctx; SharedBodyTable binding
 * uses qa_shared_body_table_read directly. Scratch arrays owned by
 * the queries object. GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_INDEX_H
#define QA_WORLD_COLLISION_INDEX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/world.h"
#include "qa/world/actors/body.h"
#include "qa/world/collision/contents.h"
#include "qa/world/collision/q1/index.h"
#include "qa/world/collision/q2.h"
#include "qa/world/collision/q3/clip_models.h"
#include "qa/world/collision/q3/index.h"
#include "qa/world/collision/q3/settings.h"
#include "qa/world/spatial/index.h"
typedef bool (*qa_actor_state_fn)(void *ctx, qa_actor_id actor, qa_body_state *out);
typedef bool (*qa_actor_collision_fn)(void *ctx, qa_actor_id actor, qa_actor_collision *out);
typedef struct qa_shared_scene_queries qa_shared_scene_queries;
struct qa_shared_scene_queries {
  qa_decoded_world geometry;
  qa_spatial_index *spatial;
  const char *kind; /* "q1"|"q2"|"q3" */
  qa_q1_collision q1; qa_q2_collision q2; qa_q3_collision *q3;
  qa_shared_body_table *body_table; /* borrowed, or NULL */
  qa_actor_state_fn actor_state_fn; void *actor_state_ctx;
  qa_actor_collision_fn read_collision; void *read_collision_ctx;
  /* q2 portal contributions */
  double *portal_ids; bool *portal_primary; double *portal_counts; size_t portal_count, portal_cap;
  double *leaf_scratch; size_t leaf_scratch_count;
  uint8_t *area_bits_scratch; size_t area_bits_length;
  qa_spatial_actor *actor_scratch; size_t actor_scratch_count;
};
/* NULL only on allocation failure. */
qa_shared_scene_queries *qa_create_scene_queries(qa_decoded_world world);
void qa_shared_scene_queries_destroy(qa_shared_scene_queries *q);
qa_source_clip_models *qa_shared_native_q3_clip_models(qa_shared_scene_queries *q);
void qa_shared_bind_collision_settings(qa_shared_scene_queries *q, qa_collision_map_settings *settings);
void qa_shared_link(qa_shared_scene_queries *q, qa_linked_body body, qa_actor_collision collision);
void qa_shared_unlink(qa_shared_scene_queries *q, qa_actor_id actor);
bool qa_shared_linked_actor(qa_shared_scene_queries *q, qa_actor_id actor, qa_spatial_actor *out);
void qa_shared_bind_body_table(qa_shared_scene_queries *q, qa_shared_body_table *table);
void qa_shared_bind_actor_state_fn(qa_shared_scene_queries *q, qa_actor_state_fn fn, void *ctx);
void qa_shared_bind_actor_collision(qa_shared_scene_queries *q, qa_actor_collision_fn read, void *ctx);
const qa_spatial_actor *qa_shared_query_actors(qa_shared_scene_queries *q, qa_bounds bounds, const char *role, size_t *out_count);
double qa_shared_point_leaf(qa_shared_scene_queries *q, qa_vec3 point);
double qa_shared_leaf_cluster(qa_shared_scene_queries *q, double leaf);
double qa_shared_leaf_area(qa_shared_scene_queries *q, double leaf);
qa_bounds qa_shared_model_bounds(qa_shared_scene_queries *q, double model);
qa_leaf_query_result qa_shared_box_leaves(qa_shared_scene_queries *q, qa_bounds bounds, double limit);
bool qa_shared_areas_connected(qa_shared_scene_queries *q, double first, double second);
const uint8_t *qa_shared_area_bits(qa_shared_scene_queries *q, double area, size_t *out_length);
bool qa_shared_cluster_visible(qa_shared_scene_queries *q, double from, double to, const char *kind);
void qa_shared_set_area_portal_state(qa_shared_scene_queries *q, double portal, bool open);
void qa_shared_adjust_area_portal_contribution(qa_shared_scene_queries *q, double portal, double delta);
const double *qa_shared_q2_portal_state(qa_shared_scene_queries *q, size_t *out_count);
void qa_shared_adjust_area_portal_pair(qa_shared_scene_queries *q, double first, double second, bool open);
qa_trace_result qa_shared_geometry_trace(qa_shared_scene_queries *q, qa_trace_query query);
qa_trace_result qa_shared_trace(qa_shared_scene_queries *q, qa_trace_query query);
qa_trace_result qa_shared_trace_excluding(qa_shared_scene_queries *q, qa_trace_query query, const qa_actor_id *excluded, size_t excluded_count);
qa_point_contents_result qa_shared_point_contents(qa_shared_scene_queries *q, qa_point_contents_query query);
qa_scene_queries qa_shared_as_scene_queries(qa_shared_scene_queries *q);
#endif
