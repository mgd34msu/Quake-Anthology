/* Port of src/world/collision/q1/index.ts (Q1Collision + hull re-exports).
 * Trace result carries TS Q1CollisionTrace.contents in `contents`.
 * Prepared-clip cache is a direct derive (cache is performance-only).
 * GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q1_INDEX_H
#define QA_WORLD_COLLISION_Q1_INDEX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/foundation/stamp.h"
#include "qa/world/collision/q1/hull.h"
#include "qa/world/geometry/q1_solid/index.h"
typedef bool (*qa_q1_blocks_contents_fn)(void *ctx, double contents, qa_trace_policy policy);
typedef struct qa_q1_collision_options {
  qa_q1_blocks_contents_fn blocks_contents; void *blocks_ctx;
} qa_q1_collision_options;
typedef struct qa_q1_geometry_coverage { const char *arbitrary_shapes; const char *clip_only; } qa_q1_geometry_coverage;
typedef struct qa_q1_collision qa_q1_collision;
struct qa_q1_collision {
  const qa_q1_world_geometry *geometry;
  qa_q1_solid_space solid_space;
  qa_q1_collision_options options;
  /* per-model hull cache */
  qa_q1_hulls *hull_cache; double *hull_models; size_t hull_count, hull_cap;
  qa_stamp_set leaf_stamps;
  double *leaf_scratch; size_t leaf_scratch_count;
  uint8_t *vis_scratch; size_t vis_scratch_length;
};
bool qa_q1_collision_init(qa_q1_collision *c, const qa_q1_world_geometry *geometry, qa_q1_collision_options options);
void qa_q1_collision_destroy(qa_q1_collision *c);
qa_q1_collision *qa_create_q1_collision(const qa_q1_world_geometry *geometry, qa_q1_collision_options options);
const qa_q1_hull *qa_q1_collision_native_hulls(qa_q1_collision *c, double model, size_t *out_count);
qa_q1_geometry_coverage qa_q1_collision_geometry_coverage(qa_q1_collision *c, double model);
qa_trace_result qa_q1_collision_trace(qa_q1_collision *c, qa_trace_query query);
qa_point_contents_result qa_q1_collision_point_contents(qa_q1_collision *c, qa_point_contents_query query);
double qa_q1_collision_leaf_at(qa_q1_collision *c, qa_vec3 point, double model);
double qa_q1_collision_point_leaf(qa_q1_collision *c, qa_vec3 point, double model);
double qa_q1_collision_leaf_cluster(qa_q1_collision *c, double leaf);
double qa_q1_collision_leaf_area(qa_q1_collision *c, double leaf);
qa_bounds qa_q1_collision_model_bounds(qa_q1_collision *c, double model);
const uint8_t *qa_q1_collision_area_bits(qa_q1_collision *c, double area, size_t *out_length);
bool qa_q1_collision_areas_connected(qa_q1_collision *c, double first, double second);
qa_leaf_query_result qa_q1_collision_box_leaves(qa_q1_collision *c, qa_bounds bounds, double limit);
bool qa_q1_collision_cluster_visible(qa_q1_collision *c, double from, double to, const char *kind);
/* SceneQueries contract adapter. */
qa_scene_queries qa_q1_collision_as_scene_queries(qa_q1_collision *c);
#endif
