/* Port of src/render/scene/visibility.ts. GPL-2.0-or-later.
 * C accommodation: the per-map WeakMap stamps/PVS caches become a
 * caller-owned qa_world_visibility_cache tied to one map by pointer. */
#ifndef QA_RENDER_SCENE_VISIBILITY_H
#define QA_RENDER_SCENE_VISIBILITY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"
#include "qa/foundation/stamp.h"
#include "qa/materials/q3_lighting.h"
double qa_world_point_leaf(qa_decoded_world map, qa_vec3 point);
typedef struct qa_world_visibility_cache {
  const void *map_key; qa_stamp_set stamps; bool stamps_init;
  struct qa_scene_pvs_entry { double key; uint8_t *bytes; size_t length; } *q1_entries, *q2_entries;
  size_t q1_count, q1_capacity, q2_count, q2_capacity;
  /* TS ancestorIndex: parent links, built once per map. */
  bool ancestor_built, ancestor_unsafe;
  int32_t *node_parents, *leaf_parents;
  /* TS AncestorIndex.caches: most recently used first, at most 4. */
  struct qa_scene_ancestor_marks {
    const uint8_t *pvs; size_t pvs_length; double from_cluster;
    bool has_areas; double *areas; size_t area_count;
    uint8_t *marks;
  } ancestor_marks[4];
  size_t ancestor_mark_count;
  /* TS mergedPvs: one buffer per cluster pair (bytes NULL: TS null), cleared at 64. */
  struct qa_scene_merged_pvs { double first, second; uint8_t *bytes; size_t length; } *merged;
  size_t merged_count, merged_capacity;
} qa_world_visibility_cache;
void qa_world_visibility_cache_init(qa_world_visibility_cache *cache);
void qa_world_visibility_cache_destroy(qa_world_visibility_cache *cache);
typedef struct qa_world_visibility_options {
  bool has_pvs_origin; qa_vec3 pvs_origin;
  bool no_vis, no_cull;
  bool has_visible_areas; const double *visible_areas; size_t visible_area_count; /* TS ReadonlySet */
  int secondary; /* 0 undefined, 1 null, 2 value */ double secondary_cluster;
  const qa_q3_dynamic_light *q3_lights; size_t q3_light_count;
} qa_world_visibility_options;
typedef struct qa_visible_world {
  double leaf;
  double *leaves; size_t leaf_count;       /* owned */
  double *surfaces; size_t surface_count;  /* owned */
  double *mask_surfaces; int *mask_values; size_t mask_count; /* owned map */
} qa_visible_world;
void qa_visible_world_destroy(qa_visible_world *v);
/* options NULL is TS default {}. cache NULL uses uncached stamps/PVS. */
bool qa_visible_world_new(qa_decoded_world map, qa_scene_camera camera, const qa_world_visibility_options *options, qa_world_visibility_cache *cache, qa_visible_world *out);
#endif
