/* Port of src/bots/navigation/aas-reachability-geometry.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_AAS_REACHABILITY_GEOMETRY_H
#define QA_BOTS_NAVIGATION_AAS_REACHABILITY_GEOMETRY_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/bots/navigation/aas.h"
#include "qa/bots/navigation/aas_reachability.h"
#include "qa/bots/navigation/aas_reachability_types.h"
#include "qa/contracts/math.h"

/* TS aasAt: checked indexing, aborts with the RangeError text. */
int64_t qa_aas_at_i64(const int64_t *values, size_t count, int64_t index);
qa_vec3 qa_aas_at_vec3(const qa_vec3 *values, size_t count, int64_t index);
const qa_aas_face_rec *qa_aas_at_face(const qa_aas_asset *world, int64_t index);
const qa_aas_plane_rec *qa_aas_at_plane(const qa_aas_asset *world, int64_t index);
const qa_aas_edge_rec *qa_aas_at_edge(const qa_aas_asset *world, int64_t index);
const qa_aas_area_rec *qa_aas_at_area(const qa_aas_asset *world, int64_t index);
const qa_aas_node_rec *qa_aas_at_node(const qa_aas_asset *world, int64_t index);
const qa_aas_setting_rec *qa_aas_at_setting(const qa_aas_asset *world, int64_t index);
qa_aas_setting_rec *qa_aas_at_setting_mut(qa_aas_asset *world, int64_t index);
const qa_aas_reach_rec *qa_aas_at_reach(const qa_aas_asset *world, int64_t index);
qa_vec3 qa_aas_at_vertex(const qa_aas_asset *world, int64_t index);
qa_aas_linked_reachability *qa_aas_at_head(qa_aas_reachability_context *context, int64_t index);

double qa_aas_face_area(const qa_aas_asset *world, const qa_aas_face_rec *face);
double qa_aas_area_volume(const qa_aas_asset *world, int64_t area_number);
double qa_aas_area_ground_face_area(const qa_aas_asset *world, int64_t area_number);
qa_vec3 qa_aas_face_center(const qa_aas_asset *world, int64_t face_number);
double qa_aas_fall_damage_distance(const qa_aas_movement_settings *settings);
double qa_aas_fall_delta(const qa_aas_movement_settings *settings, double distance);
double qa_aas_max_jump_height(const qa_aas_movement_settings *settings, double velocity);
double qa_aas_max_jump_distance(const qa_aas_movement_settings *settings, double velocity);
double qa_aas_barrier_jump_travel_time(const qa_aas_movement_settings *settings);

typedef struct qa_aas_closest_edge_range { qa_vec3 start1, end1, start2, end2; } qa_aas_closest_edge_range;
typedef struct qa_aas_closest_edge_state { bool has_range; qa_aas_closest_edge_range range; } qa_aas_closest_edge_state;
double qa_aas_closest_edge_points(qa_vec3 v1, qa_vec3 v2, qa_vec3 v3, qa_vec3 v4,
                                  const qa_aas_plane_rec *plane1, const qa_aas_plane_rec *plane2,
                                  qa_aas_closest_edge_state *state, double best_distance);

typedef struct qa_aas_reachability_geometry { qa_aas_reachability_context *context; } qa_aas_reachability_geometry;
void qa_aas_reachability_geometry_init(qa_aas_reachability_geometry *geometry, qa_aas_reachability_context *context);
bool qa_aas_reachability_geometry_nearby_solid_or_gap(qa_aas_reachability_geometry *geometry, qa_vec3 start, qa_vec3 end);
bool qa_aas_reachability_geometry_swim(qa_aas_reachability_geometry *geometry, int64_t from, int64_t to);
bool qa_aas_reachability_geometry_equal_floor_height(qa_aas_reachability_geometry *geometry, int64_t from, int64_t to);
bool qa_aas_reachability_geometry_step_barrier_water_jump_walk_off_ledge(qa_aas_reachability_geometry *geometry, int64_t from, int64_t to);
bool qa_aas_reachability_geometry_jump(qa_aas_reachability_geometry *geometry, int64_t from, int64_t to);
bool qa_aas_reachability_geometry_ladder(qa_aas_reachability_geometry *geometry, int64_t from, int64_t to);
void qa_aas_reachability_geometry_walk_off_ledge(qa_aas_reachability_geometry *geometry, int64_t area_number);
#endif
