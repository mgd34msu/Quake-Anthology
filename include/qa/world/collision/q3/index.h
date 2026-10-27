/* Port of src/world/collision/q3/index.ts (Q3Collision + re-exports).
 * GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q3_INDEX_H
#define QA_WORLD_COLLISION_Q3_INDEX_H
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/world/collision/media.h"
#include "qa/world/collision/q3/checksum.h"
#include "qa/world/collision/q3/clip_models.h"
#include "qa/world/collision/q3/map_loader.h"
#include "qa/world/collision/q3/map_resource.h"
#include "qa/world/collision/q3/model.h"
#include "qa/world/collision/q3/patch.h"
#include "qa/world/collision/q3/polylib.h"
#include "qa/world/collision/q3/world.h"
typedef struct qa_q3_collision {
  qa_collision_world *world; bool owns_world, owns_map;
  qa_collision_world world_storage;
  /* PHS row cache */
  double *phs_keys; uint8_t **phs_rows; size_t *phs_lengths; size_t phs_count, phs_cap;
} qa_q3_collision;
void qa_q3_collision_init_geometry(qa_q3_collision *c, const qa_q3_world_geometry *geometry);
void qa_q3_collision_init_world(qa_q3_collision *c, qa_collision_world *world);
void qa_q3_collision_destroy(qa_q3_collision *c);
qa_q3_collision *qa_create_q3_collision(const qa_q3_world_geometry *geometry);
qa_q3_collision *qa_create_source_q3_collision(const uint8_t *bytes, size_t byte_length, const char *source);
double qa_q3_collision_point_leaf(qa_q3_collision *c, qa_vec3 point);
double qa_q3_collision_leaf_area(qa_q3_collision *c, double index);
double qa_q3_collision_leaf_cluster(qa_q3_collision *c, double index);
qa_bounds qa_q3_collision_model_bounds(qa_q3_collision *c, double index);
qa_leaf_query_result qa_q3_collision_box_leaves(qa_q3_collision *c, qa_bounds bounds, double limit, double *leaf_storage, size_t storage_count);
bool qa_q3_collision_areas_connected(qa_q3_collision *c, double first, double second);
uint8_t *qa_q3_collision_area_bits(qa_q3_collision *c, double area, size_t *out_length);
bool qa_q3_collision_cluster_visible(qa_q3_collision *c, double from, double to, const char *kind);
void qa_q3_collision_adjust_area_portal_state(qa_q3_collision *c, double first, double second, bool open);
qa_point_contents_result qa_q3_collision_point_contents(qa_q3_collision *c, qa_point_contents_query query);
qa_trace_result qa_q3_collision_trace(qa_q3_collision *c, qa_trace_query query);
qa_trace_media qa_q3_collision_trace_media(qa_q3_collision *c, qa_trace_query query, double fraction);
#endif
