/* Port of src/world/collision/q2.ts (Q2Collision). boxLeaves/portal
 * arrays are scratch owned by the collision. GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q2_H
#define QA_WORLD_COLLISION_Q2_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/world/collision/media.h"
typedef struct qa_q2_collision qa_q2_collision;
struct qa_q2_collision {
  const qa_q2_world_geometry *geometry;
  double *open_portals; size_t open_count, open_cap;
  double *flood;
  void *media_stamps, *trace_stamps;
  double *leaf_scratch; size_t leaf_scratch_count;
  uint8_t *area_bits_scratch; size_t area_bits_length;
};
bool qa_q2_collision_init(qa_q2_collision *c, const qa_q2_world_geometry *geometry);
void qa_q2_collision_destroy(qa_q2_collision *c);
qa_bounds qa_q2_collision_model_bounds(qa_q2_collision *c, double index);
double qa_q2_collision_point_leaf(qa_q2_collision *c, qa_vec3 point, double model);
double qa_q2_collision_leaf_cluster(qa_q2_collision *c, double index);
double qa_q2_collision_leaf_area(qa_q2_collision *c, double index);
qa_point_contents_result qa_q2_collision_point_contents(qa_q2_collision *c, qa_point_contents_query query);
qa_trace_media qa_q2_collision_trace_media(qa_q2_collision *c, qa_trace_query query, double fraction);
/* Leaves in collision scratch (valid until next boxLeaves/trace). */
qa_leaf_query_result qa_q2_collision_box_leaves(qa_q2_collision *c, qa_bounds bounds, double limit, double headnode);
qa_leaf_query_result qa_q2_collision_box_leaves_default(qa_q2_collision *c, qa_bounds bounds, double limit);
qa_trace_result qa_q2_collision_trace(qa_q2_collision *c, qa_trace_query query);
void qa_q2_collision_set_area_portal_state(qa_q2_collision *c, double portal, bool open);
const double *qa_q2_collision_portal_state(qa_q2_collision *c, size_t *out_count);
void qa_q2_collision_restore_portal_state(qa_q2_collision *c, const double *open, size_t count);
bool qa_q2_collision_areas_connected(qa_q2_collision *c, double first, double second);
const uint8_t *qa_q2_collision_area_bits(qa_q2_collision *c, double area, size_t *out_length);
bool qa_q2_collision_cluster_visible(qa_q2_collision *c, double from, double to, const char *kind);
#endif
