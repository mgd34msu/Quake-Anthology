/* Port of src/world/collision/q3/topology.ts. Checkpoint uses
 * qa_save_value trees. GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q3_TOPOLOGY_H
#define QA_WORLD_COLLISION_Q3_TOPOLOGY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/persistence/value.h"
#include "qa/world/collision/q3/counters.h"
#include "qa/world/collision/q3/map_resource.h"
typedef struct qa_box_leaf_list {
  double *leaves; size_t leaf_count; bool has_topnode; double topnode;
  double last_leaf; bool overflowed;
} qa_box_leaf_list;
void qa_box_leaf_list_destroy(qa_box_leaf_list *l);
/* TS SourceClusterPVS closure. */
typedef struct qa_source_cluster_pvs { const qa_collision_map_data *map; int64_t base_offset; } qa_source_cluster_pvs;
double qa_source_cluster_pvs_byte_at(qa_source_cluster_pvs pvs, double offset);
typedef struct qa_collision_topology {
  qa_collision_map_data *map; double area_count, cluster_count;
  bool has_visibility_row_bytes; double visibility_row_bytes;
  int32_t flood_valid; bool no_areas; qa_collision_counters *counters;
  qa_collision_counters owned_counters;
} qa_collision_topology;
/* counters NULL = owned default. */
void qa_collision_topology_init(qa_collision_topology *t, qa_collision_map_data *map, qa_collision_counters *counters);
qa_save_value *qa_topology_capture_portal_checkpoint(qa_collision_topology *t);
void qa_topology_restore_portal_checkpoint(qa_collision_topology *t, const qa_save_value *value);
double qa_topology_point_leafnum(qa_collision_topology *t, qa_vec3 point);
qa_box_leaf_list qa_topology_box_leafnums(qa_collision_topology *t, qa_bounds bounds, double max_leaves);
/* Borrowed brush pointers; caller frees array only. */
qa_q3_collision_brush **qa_topology_box_brushes(qa_collision_topology *t, qa_bounds bounds, double max_brushes, size_t *out_count);
typedef void (*qa_store_leaf_fn)(void *ctx, double leafnum);
bool qa_topology_visit_leaves(qa_collision_topology *t, qa_bounds bounds, qa_store_leaf_fn store, void *ctx, double *out_topnode);
double qa_topology_leaf_area(qa_collision_topology *t, double index);
double qa_topology_leaf_cluster(qa_collision_topology *t, double index);
qa_source_cluster_pvs qa_topology_cluster_pvs(qa_collision_topology *t, double cluster);
bool qa_topology_cluster_visible(qa_collision_topology *t, double from, double to);
void qa_topology_set_no_areas(qa_collision_topology *t, bool enabled);
void qa_topology_adjust_area_portal_state(qa_collision_topology *t, double a1, double a2, bool open);
/* no_areas: -1 = use topology flag. */
bool qa_topology_areas_connected(qa_collision_topology *t, double a1, double a2, int no_areas);
double qa_topology_write_area_bits(qa_collision_topology *t, uint8_t *buffer, size_t buffer_length, double area, int no_areas);
uint8_t *qa_topology_area_bits(qa_collision_topology *t, double area, int no_areas, size_t *out_length);
#endif
