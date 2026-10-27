/* Port of src/world/collision/q3/world.ts (also re-exports settings).
 * TraceQuery optional fields use has_* flags. GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_Q3_WORLD_H
#define QA_WORLD_COLLISION_Q3_WORLD_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/persistence/value.h"
#include "qa/world/collision/media.h"
#include "qa/world/collision/q3/counters.h"
#include "qa/world/collision/q3/map_resource.h"
#include "qa/world/collision/q3/settings.h"
#include "qa/world/collision/q3/topology.h"
typedef struct qa_q3_trace_shape { const char *kind; qa_vec3 mins, maxs; } qa_q3_trace_shape;
typedef struct qa_q3_trace_query {
  qa_vec3 start, end; qa_q3_trace_shape shape; double mask;
  bool has_model_index; double model_index;
  bool has_curves; bool curves; bool has_player_curve_clip; bool player_curve_clip;
} qa_q3_trace_query;
typedef struct qa_q3_trace_result {
  double fraction; qa_vec3 end; const char *solidity; /* clear|start-solid|all-solid */
  bool has_plane; qa_plane plane; double contents, surface_flags;
} qa_q3_trace_result;
typedef struct qa_source_trace_plane { qa_plane plane; double type, signbits; } qa_source_trace_plane;
typedef struct qa_source_trace_result {
  bool all_solid, start_solid; double fraction; qa_vec3 end;
  qa_source_trace_plane plane; double surface_flags, contents;
} qa_source_trace_result;
typedef struct qa_capsule_replacement_trace {
  qa_vec3 start, end, mins, extents; double radius; qa_vec3 offset;
  qa_bounds bounds; bool stationary, point_trace;
} qa_capsule_replacement_trace;
qa_source_trace_result qa_empty_source_trace(void);
qa_q3_trace_result qa_source_trace_view(qa_source_trace_result source);
qa_vec3 qa_source_trace_end(qa_vec3 start, qa_vec3 end, double fraction);
double qa_collision_vector_distance_squared(qa_vec3 first, qa_vec3 second);
/* TS CollisionWorldProfile: kind "shared"(owner+settings) | "disabled". */
typedef struct qa_collision_world_profile {
  const char *kind; qa_collision_debug_surface *owner; qa_collision_map_settings *settings;
} qa_collision_world_profile;
typedef struct qa_collision_world qa_collision_world;
struct qa_collision_world {
  qa_collision_map_data *map; qa_collision_topology topology;
  qa_collision_debug_surface *debug; qa_collision_map_settings *settings;
  qa_collision_counters *counters; qa_collision_counters owned_counters;
  void *source_models; /* qa_source_clip_models*, owned lazily */
  void *media_stamps; /* qa_stamp_set storage */
};
void qa_collision_world_init(qa_collision_world *w, qa_collision_map_data *map, qa_collision_world_profile profile, qa_collision_counters *counters);
void qa_collision_world_destroy(qa_collision_world *w);
double qa_collision_world_area_count(qa_collision_world *w);
void qa_collision_world_bind_settings(qa_collision_world *w, qa_collision_map_settings *settings);
double qa_collision_world_cluster_count(qa_collision_world *w);
double qa_collision_world_model_count(qa_collision_world *w);
bool qa_collision_world_has_nodes(qa_collision_world *w);
qa_collision_box_storage *qa_collision_world_box_storage(qa_collision_world *w);
void qa_collision_world_advance_check_count(qa_collision_world *w);
qa_bounds qa_collision_world_model_bounds(qa_collision_world *w, double index);
double qa_collision_world_point_leafnum(qa_collision_world *w, qa_vec3 point);
qa_box_leaf_list qa_collision_world_box_leafnums(qa_collision_world *w, qa_bounds bounds, double max_leaves);
qa_q3_collision_brush **qa_collision_world_box_brushes(qa_collision_world *w, qa_bounds bounds, double max_brushes, size_t *out_count);
double qa_collision_world_leaf_area(qa_collision_world *w, double index);
double qa_collision_world_leaf_cluster(qa_collision_world *w, double index);
qa_source_cluster_pvs qa_collision_world_cluster_pvs(qa_collision_world *w, double cluster);
bool qa_collision_world_cluster_visible(qa_collision_world *w, double from, double to);
qa_save_value *qa_collision_world_capture_portal_checkpoint(qa_collision_world *w);
void qa_collision_world_restore_portal_checkpoint(qa_collision_world *w, const qa_save_value *value);
void qa_collision_world_adjust_area_portal_state(qa_collision_world *w, double a1, double a2, bool open);
bool qa_collision_world_areas_connected(qa_collision_world *w, double a1, double a2);
double qa_collision_world_write_area_bits(qa_collision_world *w, uint8_t *buffer, size_t length, double area);
uint8_t *qa_collision_world_area_bits(qa_collision_world *w, double area, size_t *out_length);
void qa_collision_world_set_no_areas(qa_collision_world *w, bool enabled);
double qa_collision_world_point_contents(qa_collision_world *w, qa_vec3 point, double model_index);
double qa_collision_world_transformed_point_contents(qa_collision_world *w, qa_vec3 point, double model_index, qa_vec3 origin, qa_vec3 angles);
qa_q3_trace_result qa_collision_world_trace(qa_collision_world *w, qa_q3_trace_query query);
void *qa_collision_world_source_clip_models(qa_collision_world *w); /* qa_source_clip_models* */
qa_source_trace_result qa_collision_world_trace_source(qa_collision_world *w, qa_q3_trace_query query);
qa_source_trace_result qa_collision_world_trace_capsule_replacement(qa_collision_world *w, qa_q3_trace_query query, qa_capsule_replacement_trace prepared);
qa_q3_trace_result qa_collision_world_transformed_trace(qa_collision_world *w, qa_q3_trace_query query, qa_vec3 origin, qa_vec3 angles);
qa_source_trace_result qa_collision_world_transformed_trace_source(qa_collision_world *w, qa_q3_trace_query query, qa_vec3 origin, qa_vec3 angles);
typedef struct qa_q3_trace_transform { qa_vec3 origin, angles; } qa_q3_trace_transform;
qa_trace_media qa_collision_world_trace_media(qa_collision_world *w, qa_q3_trace_query query, double fraction, const qa_q3_trace_transform *transform);
#endif
