/* Port of src/bots/navigation/aas-reachability-geometry.ts.
 * Area geometry and ordinary reachabilities from id Software's
 * code/botlib/be_aas_reach.c. GPL-2.0-or-later. */
#include "qa/bots/navigation/aas_reachability_geometry.h"
#include "qa/bots/navigation/aas_reachability_spatial.h"
#include "qa/bots/navigation/aas_reachability_types.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float fr(double v) { return (float)v; }
static qa_vec3 v3f(double x, double y, double z) { return qa_math_vec3(x, y, z); }
static const qa_vec3 UP = {0, 0, 1};
static qa_vec3 ma(qa_vec3 start, double scale, qa_vec3 direction) { return qa_add3(start, qa_scale3(direction, scale)); }
static qa_vec3 ma_double(qa_vec3 start, double scale, qa_vec3 direction) {
  return v3f(start.x + scale * direction.x, start.y + scale * direction.y, start.z + scale * direction.z);
}
static qa_vec3 with_z(qa_vec3 point, double height) { return v3f(point.x, point.y, height); }
static double axis_of(qa_vec3 v, int axis) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; }
static int64_t source_integer(double value) {
  double integer = trunc(value);
  if (!isfinite(integer) || integer < -2147483648.0 || integer > 2147483647.0)
    qa_range_error("AAS reachability numeric conversion exceeds source signed integer range");
  return (int64_t)integer;
}
static int64_t integer_abs(double value) {
  int64_t integer = source_integer(value);
  if (integer == -2147483648LL) qa_range_error("AAS reachability abs of INT_MIN is undefined");
  return integer < 0 ? -integer : integer;
}
static bool close_bounds(qa_bounds a, qa_bounds b, bool vertical) {
  for (int axis = 0; axis < 3; axis++) {
    if (!vertical && axis == 2) continue;
    if (axis_of(a.min, axis) > (double)fr(axis_of(b.max, axis) + 10) ||
        axis_of(a.max, axis) < (double)fr(axis_of(b.min, axis) - 10)) return false;
  }
  return true;
}
static void at_fail(int64_t index, size_t count) {
  qa_range_error("AAS reachability index %lld outside %zu", (long long)index, count);
}
int64_t qa_aas_at_i64(const int64_t *values, size_t count, int64_t index) {
  if (index < 0 || index >= (int64_t)count) at_fail(index, count);
  return values[index];
}
qa_vec3 qa_aas_at_vec3(const qa_vec3 *values, size_t count, int64_t index) {
  if (index < 0 || index >= (int64_t)count) at_fail(index, count);
  return values[index];
}
#define AT_REC(name, field, count_field, type) \
  const type *name(const qa_aas_asset *world, int64_t index) { \
    if (index < 0 || index >= (int64_t)world->count_field) at_fail(index, world->count_field); \
    return &world->field[index]; \
  }
AT_REC(qa_aas_at_face, faces, face_count, qa_aas_face_rec)
AT_REC(qa_aas_at_plane, planes, plane_count, qa_aas_plane_rec)
AT_REC(qa_aas_at_edge, edges, edge_count, qa_aas_edge_rec)
AT_REC(qa_aas_at_area, areas, area_count, qa_aas_area_rec)
AT_REC(qa_aas_at_node, nodes, node_count, qa_aas_node_rec)
AT_REC(qa_aas_at_setting, settings, setting_count, qa_aas_setting_rec)
AT_REC(qa_aas_at_reach, reachability, reachability_count, qa_aas_reach_rec)
#undef AT_REC
qa_aas_setting_rec *qa_aas_at_setting_mut(qa_aas_asset *world, int64_t index) {
  if (index < 0 || index >= (int64_t)world->setting_count) at_fail(index, world->setting_count);
  return &world->settings[index];
}
qa_vec3 qa_aas_at_vertex(const qa_aas_asset *world, int64_t index) {
  return qa_aas_at_vec3(world->vertices, world->vertex_count, index);
}
qa_aas_linked_reachability *qa_aas_at_head(qa_aas_reachability_context *context, int64_t index) {
  if (index < 0 || index >= (int64_t)context->head_count) at_fail(index, context->head_count);
  return context->heads[index];
}
static int64_t abs64(int64_t v) { return v < 0 ? -v : v; }

double qa_aas_face_area(const qa_aas_asset *world, const qa_aas_face_rec *face) {
  int64_t first = qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face->first_edge);
  const qa_aas_edge_rec *edge = qa_aas_at_edge(world, abs64(first));
  qa_vec3 origin = qa_aas_at_vertex(world, edge->vertices[first < 0 ? 1 : 0]);
  float total = 0;
  for (int64_t i = 1; i < face->edge_count - 1; i++) {
    int64_t number = qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face->first_edge + i);
    const qa_aas_edge_rec *current = qa_aas_at_edge(world, abs64(number));
    qa_vec3 first_vertex = qa_aas_at_vertex(world, current->vertices[number < 0 ? 1 : 0]);
    qa_vec3 last_vertex = qa_aas_at_vertex(world, current->vertices[number < 0 ? 0 : 1]);
    total = fr((double)total + 0.5 * qa_length3(qa_cross3(qa_sub3(first_vertex, origin), qa_sub3(last_vertex, origin))));
  }
  return total;
}
double qa_aas_area_volume(const qa_aas_asset *world, int64_t area_number) {
  const qa_aas_area_rec *area = qa_aas_at_area(world, area_number);
  const qa_aas_face_rec *first_face = qa_aas_at_face(world, abs64(qa_aas_at_i64(world->face_indexes, world->face_index_count, area->first_face)));
  const qa_aas_edge_rec *edge = qa_aas_at_edge(world, abs64(qa_aas_at_i64(world->edge_indexes, world->edge_index_count, first_face->first_edge)));
  qa_vec3 corner = qa_aas_at_vertex(world, edge->vertices[0]);
  float volume = 0;
  for (int64_t i = 0; i < area->face_count; i++) {
    const qa_aas_face_rec *face = qa_aas_at_face(world, abs64(qa_aas_at_i64(world->face_indexes, world->face_index_count, area->first_face + i)));
    const qa_aas_plane_rec *plane = qa_aas_at_plane(world, face->plane ^ (face->back_area != area_number ? 1 : 0));
    float distance = -fr(qa_dot3(corner, plane->normal) - plane->distance);
    volume = fr((double)volume + (double)fr((double)distance * qa_aas_face_area(world, face)));
  }
  return fr((double)volume / 3);
}
double qa_aas_area_ground_face_area(const qa_aas_asset *world, int64_t area_number) {
  const qa_aas_area_rec *area = qa_aas_at_area(world, area_number);
  float total = 0;
  for (int64_t i = 0; i < area->face_count; i++) {
    const qa_aas_face_rec *face = qa_aas_at_face(world, abs64(qa_aas_at_i64(world->face_indexes, world->face_index_count, area->first_face + i)));
    if ((face->flags & 4) != 0) total = fr((double)total + qa_aas_face_area(world, face));
  }
  return total;
}
qa_vec3 qa_aas_face_center(const qa_aas_asset *world, int64_t face_number) {
  const qa_aas_face_rec *face = qa_aas_at_face(world, face_number);
  qa_vec3 center = v3f(0, 0, 0);
  for (int64_t i = 0; i < face->edge_count; i++) {
    const qa_aas_edge_rec *edge = qa_aas_at_edge(world, abs64(qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face->first_edge + i)));
    center = qa_add3(center, qa_aas_at_vertex(world, edge->vertices[0]));
    center = qa_add3(center, qa_aas_at_vertex(world, edge->vertices[1]));
  }
  return qa_scale3(center, fr(0.5 / (double)face->edge_count));
}
double qa_aas_fall_damage_distance(const qa_aas_movement_settings *settings) {
  float velocity = fr(sqrt(30.0 * 10000.0));
  float time = fr((double)velocity / (double)settings->gravity);
  return (double)source_integer(0.5 * (double)settings->gravity * (double)time * (double)time);
}
double qa_aas_fall_delta(const qa_aas_movement_settings *settings, double distance) {
  float time = fr(sqrt(fabs(distance) * 2 / (double)settings->gravity));
  float delta = fr((double)time * (double)settings->gravity);
  return fr((double)fr((double)delta * (double)delta) * 0.0001);
}
double qa_aas_max_jump_height(const qa_aas_movement_settings *settings, double velocity) {
  float time = fr(velocity / (double)settings->gravity);
  return fr(0.5 * (double)settings->gravity * (double)time * (double)time);
}
double qa_aas_max_jump_distance(const qa_aas_movement_settings *settings, double velocity) {
  float time = fr(sqrt((double)settings->max_jump_fall_height / (0.5 * (double)settings->gravity)));
  return fr((double)settings->max_velocity * (double)fr((double)time + (double)fr(velocity / (double)settings->gravity)));
}
double qa_aas_barrier_jump_travel_time(const qa_aas_movement_settings *settings) {
  return (double)(source_integer((double)settings->jump_velocity / ((double)settings->gravity * 0.1)) & 0xffff);
}
static qa_vec3 edge_projection(qa_vec3 point, qa_vec3 edge_start, qa_vec3 direction, const qa_aas_plane_rec *plane) {
  qa_vec3 flat;
  if (direction.x != 0) {
    float a = fr(direction.y / direction.x);
    float b = fr(edge_start.y - (double)fr((double)a * edge_start.x));
    float x = fr((qa_dot3(point, direction) - (double)fr((double)fr((double)a * direction.x) + (double)fr((double)b * direction.y))) / direction.x);
    flat = v3f(x, fr((double)fr((double)a * (double)x) + (double)b), 0);
  } else flat = v3f(edge_start.x, point.y, 0);
  return with_z(flat, fr((plane->distance - qa_dot3(plane->normal, flat)) / plane->normal.z));
}
typedef struct { qa_aas_closest_edge_state *state; double best; } ClosestUpdate;
static void closest_update(ClosestUpdate *u, qa_vec3 start, qa_vec3 end, bool allow_range) {
  double current = qa_length3(qa_sub3(end, start));
  if (allow_range && current > u->best - 0.5 && current < u->best + 0.5) {
    if (!u->state->has_range) qa_range_error("AAS_ClosestEdgePoints reads an uninitialized closest-point range");
    qa_aas_closest_edge_range range = u->state->range;
    double first_start = qa_length3(qa_sub3(start, range.start1));
    double second_start = qa_length3(qa_sub3(start, range.start2));
    if (first_start > second_start) {
      if (first_start > qa_length3(qa_sub3(range.start2, range.start1))) range.start2 = start;
    } else if (second_start > qa_length3(qa_sub3(range.start2, range.start1))) range.start1 = start;
    double first_end = qa_length3(qa_sub3(end, range.end1));
    double second_end = qa_length3(qa_sub3(end, range.end2));
    if (first_end > second_end) {
      if (first_end > qa_length3(qa_sub3(range.end2, range.end1))) range.end2 = end;
    } else if (second_end > qa_length3(qa_sub3(range.end2, range.end1))) range.end1 = end;
    u->state->range = range;
  } else if (current < u->best) {
    u->best = current;
    u->state->range = (qa_aas_closest_edge_range){start, end, start, end};
    u->state->has_range = true;
  }
}
double qa_aas_closest_edge_points(qa_vec3 v1, qa_vec3 v2, qa_vec3 v3, qa_vec3 v4,
                                  const qa_aas_plane_rec *plane1, const qa_aas_plane_rec *plane2,
                                  qa_aas_closest_edge_state *state, double best_distance) {
  qa_vec3 direction1 = with_z(qa_sub3(v2, v1), 0), direction2 = with_z(qa_sub3(v4, v3), 0);
  qa_vec3 p1 = edge_projection(v1, v3, direction2, plane2), p2 = edge_projection(v2, v3, direction2, plane2);
  qa_vec3 p3 = edge_projection(v3, v1, direction1, plane1), p4 = edge_projection(v4, v1, direction1, plane1);
  ClosestUpdate u = {state, best_distance};
  bool found = false;
  if (qa_dot3(qa_sub3(p1, v3), qa_sub3(p1, v4)) <= 0) { closest_update(&u, v1, p1, true); found = true; }
  if (qa_dot3(qa_sub3(p2, v3), qa_sub3(p2, v4)) <= 0) { closest_update(&u, v2, p2, true); found = true; }
  if (qa_dot3(qa_sub3(p3, v1), qa_sub3(p3, v2)) <= 0) { closest_update(&u, p3, v3, true); found = true; }
  if (qa_dot3(qa_sub3(p4, v1), qa_sub3(p4, v2)) <= 0) { closest_update(&u, p4, v4, true); found = true; }
  if (!found) {
    closest_update(&u, v1, v3, false); closest_update(&u, v1, v4, false);
    closest_update(&u, v2, v3, false); closest_update(&u, v2, v4, false);
  }
  return u.best;
}

void qa_aas_reachability_geometry_init(qa_aas_reachability_geometry *geometry, qa_aas_reachability_context *context) {
  geometry->context = context;
}
static bool area_grounded(qa_aas_reachability_geometry *g, int64_t area) {
  return (qa_aas_at_setting(&g->context->world->asset, area)->flags & 1) != 0;
}
static bool area_swim(qa_aas_reachability_geometry *g, int64_t area) {
  return (qa_aas_at_setting(&g->context->world->asset, area)->flags & 4) != 0;
}
static bool area_crouch(qa_aas_reachability_geometry *g, int64_t area) {
  return (qa_aas_at_setting(&g->context->world->asset, area)->presence & 2) == 0;
}
static bool area_ladder(qa_aas_reachability_geometry *g, int64_t area) {
  return (qa_aas_at_setting(&g->context->world->asset, area)->flags & 2) != 0;
}
static void link_reach(qa_aas_reachability_geometry *g, int64_t area, qa_aas_linked_reachability *reach) {
  reach->next = qa_aas_at_head(g->context, area);
  if (area < 0 || area >= (int64_t)g->context->head_count) qa_range_error("AAS reachability index %lld outside %zu", (long long)area, g->context->head_count);
  g->context->heads[area] = reach;
}
bool qa_aas_reachability_geometry_nearby_solid_or_gap(qa_aas_reachability_geometry *g, qa_vec3 start, qa_vec3 end) {
  qa_aas_reachability_world *world = g->context->world;
  qa_vec3 direction = qa_normalize3(with_z(qa_sub3(end, start), 0));
  qa_vec3 point = ma(end, 48, direction);
  int64_t area = qa_aas_reachability_world_point_area(world, point);
  if (area == 0) {
    point = with_z(point, point.z + 16);
    area = qa_aas_reachability_world_point_area(world, point);
    if (area == 0) return true;
  }
  area = qa_aas_reachability_world_point_area(world, ma(end, 64, direction));
  return area != 0 && !area_swim(g, area) && !area_grounded(g, area);
}
bool qa_aas_reachability_geometry_swim(qa_aas_reachability_geometry *g, int64_t from, int64_t to) {
  qa_aas_asset *world = &g->context->world->asset;
  qa_aas_reachability_spatial *spatial = g->context->spatial;
  if (!area_swim(g, from) || !area_swim(g, to) || area_crouch(g, to)) return false;
  const qa_aas_area_rec *area1 = qa_aas_at_area(world, from), *area2 = qa_aas_at_area(world, to);
  if (!close_bounds(area1->bounds, area2->bounds, true)) return false;
  for (int64_t i = 0; i < area1->face_count; i++) {
    int64_t signed_face = qa_aas_at_i64(world->face_indexes, world->face_index_count, area1->first_face + i);
    int64_t face_number = abs64(signed_face);
    for (int64_t j = 0; j < area2->face_count; j++) {
      if (face_number != abs64(qa_aas_at_i64(world->face_indexes, world->face_index_count, area2->first_face + j))) continue;
      qa_vec3 start = qa_aas_face_center(world, face_number);
      if (((int64_t)qa_aas_spatial_point_contents(spatial, start) & 56) == 0) continue;
      const qa_aas_face_rec *face = qa_aas_at_face(world, face_number);
      qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(g->context);
      if (!reach) return false;
      qa_aas_link_set_area(reach, to);
      qa_aas_link_set_face(reach, face_number);
      qa_aas_link_set_edge(reach, 0);
      qa_aas_link_set_start(reach, start);
      qa_aas_link_set_end(reach, ma(start, -2, qa_aas_at_plane(world, face->plane ^ (signed_face < 0 ? 1 : 0))->normal));
      qa_aas_link_set_travel_type(reach, QA_TRAVEL_SWIM);
      qa_aas_link_set_travel_time(reach, 1);
      if (qa_aas_area_volume(world, to) < 800) qa_aas_link_set_travel_time(reach, qa_aas_link_travel_time(reach) + 200);
      link_reach(g, from, reach);
      qa_aas_reachability_debug_count(&g->context->debug_state, "swim");
      return true;
    }
  }
  return false;
}
bool qa_aas_reachability_geometry_equal_floor_height(qa_aas_reachability_geometry *g, int64_t from, int64_t to) {
  qa_aas_asset *world = &g->context->world->asset;
  const qa_aas_movement_settings *settings = g->context->settings;
  if (!area_grounded(g, from) || !area_grounded(g, to)) return false;
  const qa_aas_area_rec *area1 = qa_aas_at_area(world, from), *area2 = qa_aas_at_area(world, to);
  if (!close_bounds(area1->bounds, area2->bounds, false) || area2->bounds.min.z > area1->bounds.max.z) return false;
  bool has_best = false;
  double best_height = 0, best_length = 0;
  int64_t best_edge = 0;
  qa_vec3 best_start = {0, 0, 0}, best_end = {0, 0, 0};
  for (int64_t i = 0; i < area1->face_count; i++) {
    const qa_aas_face_rec *face1 = qa_aas_at_face(world, abs64(qa_aas_at_i64(world->face_indexes, world->face_index_count, area1->first_face + i)));
    if ((face1->flags & 4) == 0) continue;
    for (int64_t j = 0; j < area2->face_count; j++) {
      const qa_aas_face_rec *face2 = qa_aas_at_face(world, abs64(qa_aas_at_i64(world->face_indexes, world->face_index_count, area2->first_face + j)));
      if ((face2->flags & 4) == 0) continue;
      for (int64_t first = 0; first < face1->edge_count; first++)
        for (int64_t second = 0; second < face2->edge_count; second++) {
          int64_t number = qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face1->first_edge + first);
          if (abs64(number) != abs64(qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face2->first_edge + second))) continue;
          const qa_aas_edge_rec *edge = qa_aas_at_edge(world, abs64(number));
          qa_vec3 v0 = qa_aas_at_vertex(world, edge->vertices[0]), v1 = qa_aas_at_vertex(world, edge->vertices[1]);
          double length = qa_length3(qa_sub3(v1, v0));
          qa_vec3 midpoint = qa_scale3(qa_add3(v0, v1), 0.5);
          qa_vec3 edge_vector = number < 0 ? qa_sub3(v1, v0) : qa_sub3(v0, v1);
          qa_vec3 normal = qa_normalize3(qa_cross3(edge_vector, qa_aas_at_plane(world, face2->plane)->normal));
          qa_vec3 start = ma_double(midpoint, 0.1, normal);
          qa_vec3 raw_end = ma(midpoint, 5, normal);
          qa_vec3 end = with_z(raw_end, raw_end.z + 0.125);
          double height = qa_dot3(UP, start);
          double bh = has_best ? best_height : 99999, bl = has_best ? best_length : 0;
          if (height < bh || (height < (double)fr(bh + 1) && length > bl)) {
            has_best = true; best_height = height; best_length = length; best_edge = number; best_start = start; best_end = end;
          }
        }
    }
  }
  if (!has_best) return false;
  qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(g->context);
  if (!reach) return false;
  qa_aas_link_set_area(reach, to);
  qa_aas_link_set_face(reach, 0);
  qa_aas_link_set_edge(reach, best_edge);
  qa_aas_link_set_start(reach, best_start);
  qa_aas_link_set_end(reach, best_end);
  qa_aas_link_set_travel_type(reach, QA_TRAVEL_WALK);
  qa_aas_link_set_travel_time(reach, 1);
  link_reach(g, from, reach);
  if (!area_crouch(g, from) && area_crouch(g, to))
    qa_aas_link_set_travel_time(reach, fr((double)qa_aas_link_travel_time(reach) + (double)settings->start_crouch_time));
  qa_aas_reachability_debug_count(&g->context->debug_state, "equal floor");
  return true;
}
typedef struct { double distance, length; int64_t edge; qa_vec3 start, end, normal; } StepCandidate;
bool qa_aas_reachability_geometry_step_barrier_water_jump_walk_off_ledge(qa_aas_reachability_geometry *g, int64_t from, int64_t to) {
  qa_aas_asset *world = &g->context->world->asset;
  qa_aas_reachability_spatial *spatial = g->context->spatial;
  const qa_aas_movement_settings *settings = g->context->settings;
  if ((!area_grounded(g, from) && !area_swim(g, from)) || (!area_grounded(g, to) && !area_swim(g, to))) return false;
  const qa_aas_area_rec *area1 = qa_aas_at_area(world, from), *area2 = qa_aas_at_area(world, to);
  bool swim1 = area_swim(g, from);
  if (!close_bounds(area1->bounds, area2->bounds, false)) return false;
  bool has_ground = false, has_water = false;
  StepCandidate ground = {0, 0, 0, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, water = {0, 0, 0, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
  for (int64_t i = 0; i < area1->face_count; i++) {
    int64_t face_number = qa_aas_at_i64(world->face_indexes, world->face_index_count, area1->first_face + i);
    bool face_side = face_number < 0;
    const qa_aas_face_rec *face1 = qa_aas_at_face(world, abs64(face_number));
    if ((face1->flags & 4) == 0) {
      if (!swim1 || qa_dot3(qa_aas_at_plane(world, face1->plane ^ (face_side ? 0 : 1))->normal, UP) < 0.7) continue;
    }
    for (int64_t k = 0; k < face1->edge_count; k++) {
      int64_t signed_edge = qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face1->first_edge + k);
      int64_t edge_number = abs64(signed_edge);
      bool side = signed_edge < 0;
      if ((face1->flags & 4) == 0) side = side == face_side;
      const qa_aas_edge_rec *edge1 = qa_aas_at_edge(world, edge_number);
      qa_vec3 v1 = qa_aas_at_vertex(world, edge1->vertices[side ? 0 : 1]);
      qa_vec3 v2 = qa_aas_at_vertex(world, edge1->vertices[side ? 1 : 0]);
      qa_vec3 normal = qa_normalize3(qa_cross3(qa_sub3(v2, v1), UP));
      double dist = qa_dot3(normal, v1);
      for (int64_t j = 0; j < area2->face_count; j++) {
        const qa_aas_face_rec *face2 = qa_aas_at_face(world, abs64(qa_aas_at_i64(world->face_indexes, world->face_index_count, area2->first_face + j)));
        if ((face2->flags & 4) == 0) continue;
        for (int64_t l = 0; l < face2->edge_count; l++) {
          const qa_aas_edge_rec *edge2 = qa_aas_at_edge(world, abs64(qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face2->first_edge + l)));
          qa_vec3 v3v = qa_aas_at_vertex(world, edge2->vertices[0]), v4v = qa_aas_at_vertex(world, edge2->vertices[1]);
          float diff1 = fr(qa_dot3(normal, v3v) - dist), diff2 = fr(qa_dot3(normal, v4v) - dist);
          if (diff1 < -0.1f || diff1 > 0.1f || diff2 < -0.1f || diff2 > 0.1f) continue;
          qa_vec3 ort = qa_cross3(UP, normal);
          double ort_dot = qa_dot3(ort, ort);
          qa_vec3 a1 = v1, a2 = v2, a3 = v3v, a4 = v4v;
          double y1 = a1.z, y2 = a2.z, y3 = a3.z, y4 = a4.z;
          float x1 = fr(qa_dot3(a1, ort) / ort_dot), x2 = fr(qa_dot3(a2, ort) / ort_dot);
          float x3 = fr(qa_dot3(a3, ort) / ort_dot), x4 = fr(qa_dot3(a4, ort) / ort_dot);
          if (x1 > x2) { float tx = x1; x1 = x2; x2 = tx; double ty = y1; y1 = y2; y2 = ty; qa_vec3 tv = a1; a1 = a2; a2 = tv; }
          if (x3 > x4) { float tx = x3; x3 = x4; x4 = tx; double ty = y3; y3 = y4; y4 = ty; qa_vec3 tv = a3; a3 = a4; a4 = tv; }
          if (x2 <= x3 || x4 <= x1) continue;
          float dist1, dist2;
          qa_vec3 p11, p12, p21, p22;
          if ((double)x1 - 0.5 < (double)x3 && (double)x4 < (double)x2 + 0.5 && (double)x3 - 0.5 < (double)x1 && (double)x2 < (double)x4 + 0.5) {
            dist1 = fr(y3 - y1); dist2 = fr(y4 - y2); p11 = a1; p21 = a2; p12 = a3; p22 = a4;
          } else {
            if ((double)x1 > (double)x3 - 0.1 && (double)x1 < (double)x3 + 0.1) { dist1 = fr(y3 - y1); p11 = a1; p12 = a3; }
            else if (x1 < x3) {
              float y = fr(y1 + (double)fr((double)fr((double)fr((double)x3 - (double)x1) * (double)fr(y2 - y1)) / (double)fr((double)x2 - (double)x1)));
              dist1 = fr(y3 - (double)y); p11 = with_z(a3, y); p12 = a3;
            } else {
              float y = fr(y3 + (double)fr((double)fr((double)fr((double)x1 - (double)x3) * (double)fr(y4 - y3)) / (double)fr((double)x4 - (double)x3)));
              dist1 = fr((double)y - y1); p11 = a1; p12 = with_z(a1, y);
            }
            if ((double)x2 > (double)x4 - 0.1 && (double)x2 < (double)x4 + 0.1) { dist2 = fr(y4 - y2); p21 = a2; p22 = a4; }
            else if (x2 < x4) {
              float y = fr(y3 + (double)fr((double)fr((double)fr((double)x2 - (double)x3) * (double)fr(y4 - y3)) / (double)fr((double)x4 - (double)x3)));
              dist2 = fr((double)y - y2); p21 = a2; p22 = with_z(a2, y);
            } else {
              float y = fr(y1 + (double)fr((double)fr((double)fr((double)x4 - (double)x1) * (double)fr(y2 - y1)) / (double)fr((double)x2 - (double)x1)));
              dist2 = fr(y4 - (double)y); p21 = with_z(a4, y); p22 = a4;
            }
          }
          qa_vec3 start, end;
          if ((double)dist1 > (double)fr((double)dist2 - 1) && (double)dist1 < (double)fr((double)dist2 + 1)) {
            dist = dist1; start = qa_scale3(qa_add3(p11, p21), 0.5); end = qa_scale3(qa_add3(p12, p22), 0.5);
          } else if (dist1 < dist2) { dist = dist1; start = p11; end = p12; }
          else { dist = dist2; start = p21; end = p22; }
          double length = qa_length3(qa_sub3(p22, p12));
          bool is_ground = (face1->flags & 4) != 0;
          bool has_prior = is_ground ? has_ground : has_water;
          double best_dist = has_prior ? (is_ground ? ground.distance : water.distance) : 99999;
          double best_length = has_prior ? (is_ground ? ground.length : water.length) : 0;
          if (dist < best_dist || (dist < (double)fr(best_dist + 1) && length > best_length)) {
            StepCandidate candidate = {dist, length, edge_number, start, end, normal};
            if (is_ground) { ground = candidate; has_ground = true; }
            else { water = candidate; has_water = true; }
          }
        }
      }
    }
  }
  if (has_ground && ground.distance >= 0 && ground.distance < (double)settings->max_step) {
    qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(g->context);
    if (!reach) return false;
    qa_aas_link_set_area(reach, to); qa_aas_link_set_face(reach, 0); qa_aas_link_set_edge(reach, ground.edge);
    qa_aas_link_set_start(reach, ma_double(ground.start, 0.1, ground.normal));
    qa_aas_link_set_end(reach, ma(ground.end, 5, ground.normal));
    qa_aas_link_set_travel_type(reach, QA_TRAVEL_WALK);
    qa_aas_link_set_travel_time(reach, 0);
    if (!area_crouch(g, from) && area_crouch(g, to))
      qa_aas_link_set_travel_time(reach, fr((double)qa_aas_link_travel_time(reach) + (double)settings->start_crouch_time));
    link_reach(g, from, reach);
    qa_aas_reachability_debug_count(&g->context->debug_state, "step");
    return true;
  }
  if (has_water) {
    qa_vec3 point = ma(water.end, -2, water.normal);
    qa_vec3 test_point = with_z(point, fr(point.z - (double)settings->max_water_jump));
    if (area_swim(g, qa_aas_reachability_world_point_area(g->context->world, test_point)) &&
        water.distance < (double)fr((double)settings->max_water_jump + 24) && !area_crouch(g, from) && !area_crouch(g, to)) {
      qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(g->context);
      if (!reach) return false;
      qa_aas_link_set_area(reach, to); qa_aas_link_set_face(reach, 0); qa_aas_link_set_edge(reach, water.edge);
      qa_aas_link_set_start(reach, water.start);
      qa_aas_link_set_end(reach, ma(water.end, 15, water.normal));
      qa_aas_link_set_travel_type(reach, QA_TRAVEL_WATERJUMP);
      qa_aas_link_set_travel_time(reach, settings->water_jump_time);
      link_reach(g, from, reach);
      qa_aas_reachability_debug_count(&g->context->debug_state, "waterjump");
      return true;
    }
  }
  if (has_ground && ground.distance > 0 && ground.distance < (double)settings->max_barrier &&
      (!has_water || (double)fr(ground.distance - water.distance) < 16) && !area_crouch(g, from) && !area_crouch(g, to)) {
    qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(g->context);
    if (!reach) return false;
    qa_aas_link_set_area(reach, to); qa_aas_link_set_face(reach, 0); qa_aas_link_set_edge(reach, ground.edge);
    qa_aas_link_set_start(reach, ma_double(ground.start, 0.1, ground.normal));
    qa_aas_link_set_end(reach, ma(ground.end, 5, ground.normal));
    qa_aas_link_set_travel_type(reach, QA_TRAVEL_BARRIERJUMP);
    qa_aas_link_set_travel_time(reach, settings->barrier_jump_time);
    link_reach(g, from, reach);
    qa_aas_reachability_debug_count(&g->context->debug_state, "barrier");
    return true;
  }
  if (!has_ground || ground.distance >= 0) return false;
  if (ground.distance > -(double)settings->max_step) {
    qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(g->context);
    if (!reach) return false;
    qa_aas_link_set_area(reach, to); qa_aas_link_set_face(reach, 0); qa_aas_link_set_edge(reach, ground.edge);
    qa_aas_link_set_start(reach, ma_double(ground.start, 0.1, ground.normal));
    qa_aas_link_set_end(reach, ma(ground.end, 5, ground.normal));
    qa_aas_link_set_travel_type(reach, QA_TRAVEL_WALK);
    qa_aas_link_set_travel_time(reach, 1);
    link_reach(g, from, reach);
    qa_aas_reachability_debug_count(&g->context->debug_state, "walk");
    return true;
  }
  if (settings->max_fall_height != 0 && !(fabs(ground.distance) < (double)settings->max_fall_height)) return false;
  qa_vec3 ground_end = ma(ground.end, 2, ground.normal);
  qa_vec3 start = with_z(ground_end, ground.start.z), end = with_z(ground_end, ground_end.z + 4);
  qa_aas_trace trace = qa_aas_spatial_trace_client_bbox(spatial, start, end, 2, -1);
  if (trace.start_solid || !(trace.fraction >= 1) ||
      qa_aas_reachability_world_point_area(g->context->world, with_z(trace.end, trace.end.z + 1)) != to) return false;
  size_t crossing_count = 0;
  qa_aas_area_crossing *crossings = qa_aas_spatial_trace_areas(spatial, start, end, 10, &crossing_count);
  bool blocked = false;
  for (size_t i = 0; i < crossing_count; i++)
    if ((qa_aas_at_setting(world, crossings[i].area)->contents & 8) != 0) blocked = true;
  free(crossings);
  if (blocked) return false;
  qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(g->context);
  if (!reach) return false;
  qa_aas_link_set_area(reach, to); qa_aas_link_set_face(reach, 0); qa_aas_link_set_edge(reach, ground.edge);
  qa_aas_link_set_start(reach, ground.start);
  qa_aas_link_set_end(reach, ground_end);
  qa_aas_link_set_travel_type(reach, QA_TRAVEL_WALKOFFLEDGE);
  qa_aas_link_set_travel_time(reach, (double)settings->start_walk_off_ledge_time + fabs(ground.distance) * 50 / (double)settings->gravity);
  if (!area_swim(g, to) && (qa_aas_at_setting(world, to)->contents & 128) == 0) {
    if (qa_aas_fall_delta(settings, ground.distance) > (double)settings->fall_delta5)
      qa_aas_link_set_travel_time(reach, fr((double)qa_aas_link_travel_time(reach) + (double)settings->fall_damage5_time));
    if (qa_aas_fall_delta(settings, ground.distance) > (double)settings->fall_delta10)
      qa_aas_link_set_travel_time(reach, fr((double)qa_aas_link_travel_time(reach) + (double)settings->fall_damage10_time));
  }
  link_reach(g, from, reach);
  qa_aas_reachability_debug_count(&g->context->debug_state, "walkoffledge");
  return true;
}
bool qa_aas_reachability_geometry_jump(qa_aas_reachability_geometry *g, int64_t from, int64_t to) {
  qa_aas_asset *world = &g->context->world->asset;
  qa_aas_reachability_spatial *spatial = g->context->spatial;
  const qa_aas_movement_settings *settings = g->context->settings;
  if (!area_grounded(g, from) || !area_grounded(g, to) || area_crouch(g, from) || area_crouch(g, to)) return false;
  const qa_aas_area_rec *area1 = qa_aas_at_area(world, from), *area2 = qa_aas_at_area(world, to);
  float maximum_distance = fr(2 * qa_aas_max_jump_distance(settings, settings->jump_velocity));
  for (int axis = 0; axis < 2; axis++) {
    if (axis_of(area1->bounds.min, axis) > (double)fr(axis_of(area2->bounds.max, axis) + (double)maximum_distance) ||
        axis_of(area1->bounds.max, axis) < (double)fr(axis_of(area2->bounds.min, axis) - (double)maximum_distance)) return false;
  }
  if (area2->bounds.min.z > (double)fr(area1->bounds.max.z + qa_aas_max_jump_height(settings, settings->jump_velocity))) return false;
  double best_distance = 999999;
  qa_aas_closest_edge_state state; memset(&state, 0, sizeof state);
  for (int64_t i = 0; i < area1->face_count; i++) {
    const qa_aas_face_rec *face1 = qa_aas_at_face(world, abs64(qa_aas_at_i64(world->face_indexes, world->face_index_count, area1->first_face + i)));
    if ((face1->flags & 4) == 0) continue;
    for (int64_t j = 0; j < area2->face_count; j++) {
      const qa_aas_face_rec *face2 = qa_aas_at_face(world, abs64(qa_aas_at_i64(world->face_indexes, world->face_index_count, area2->first_face + j)));
      if ((face2->flags & 4) == 0) continue;
      for (int64_t k = 0; k < face1->edge_count; k++) {
        const qa_aas_edge_rec *edge1 = qa_aas_at_edge(world, abs64(qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face1->first_edge + k)));
        for (int64_t l = 0; l < face2->edge_count; l++) {
          const qa_aas_edge_rec *edge2 = qa_aas_at_edge(world, abs64(qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face2->first_edge + l)));
          best_distance = qa_aas_closest_edge_points(qa_aas_at_vertex(world, edge1->vertices[0]), qa_aas_at_vertex(world, edge1->vertices[1]),
              qa_aas_at_vertex(world, edge2->vertices[0]), qa_aas_at_vertex(world, edge2->vertices[1]),
              qa_aas_at_plane(world, face1->plane), qa_aas_at_plane(world, face2->plane), &state, best_distance);
        }
      }
    }
  }
  if (!(best_distance > 4 && best_distance < (double)maximum_distance)) return false;
  if (!state.has_range) qa_range_error("AAS_Reachability_Jump reads uninitialized closest edge points");
  qa_vec3 best_start = qa_scale3(qa_add3(state.range.start1, state.range.start2), 0.5);
  qa_vec3 best_end = qa_scale3(qa_add3(state.range.end1, state.range.end2), 0.5);
  double speed;
  int64_t travel_type;
  if (best_distance <= 48 && fabs((double)fr(best_start.z - best_end.z)) < 8) {
    speed = 400; travel_type = QA_TRAVEL_WALKOFFLEDGE;
  } else {
    qa_aas_jump_speed fall_velocity = qa_aas_spatial_horizontal_velocity_for_jump(spatial, 0, best_start, best_end);
    if (fall_velocity.success) { speed = fr(fall_velocity.velocity * (double)fr(1.2)); travel_type = QA_TRAVEL_WALKOFFLEDGE; }
    else {
      qa_aas_jump_speed jump_velocity = qa_aas_spatial_horizontal_velocity_for_jump(spatial, settings->jump_velocity, best_start, best_end);
      if (!jump_velocity.success) return false;
      speed = fr(jump_velocity.velocity * (double)fr(1.05)); travel_type = QA_TRAVEL_JUMP;
      if (qa_length3(with_z(qa_sub3(best_end, best_start), 0)) < 10) return false;
    }
  }
  qa_vec3 direction = qa_normalize3(qa_sub3(best_end, best_start));
  qa_vec3 test_starts[2] = {ma(best_start, 1, direction), ma(best_end, -1, direction)};
  for (int i = 0; i < 2; i++) {
    qa_vec3 test_start = test_starts[i];
    qa_aas_trace trace = qa_aas_spatial_trace_client_bbox(spatial, test_start, with_z(test_start, test_start.z - 100), 2, -1);
    if (trace.start_solid) return false;
    if (trace.fraction < 1 && qa_dot3(qa_aas_at_plane(world, trace.plane)->normal, UP) >= 0.7 &&
        ((int64_t)qa_aas_spatial_point_contents(spatial, trace.end) & 24) == 0 &&
        (double)fr(test_start.z - trace.end.z) <= (double)settings->max_barrier) return false;
  }
  qa_vec3 command = v3f(0, 0, (travel_type & QA_TRAVEL_MASK) == QA_TRAVEL_JUMP ? settings->jump_velocity : 0);
  direction = qa_normalize3(with_z(qa_sub3(best_end, best_start), 0));
  qa_vec3 sideways = qa_cross3(direction, UP);
  int64_t stop_events = QA_AAS_STOP_HIT_GROUND | QA_AAS_STOP_ENTER_WATER | QA_AAS_STOP_ENTER_SLIME |
                        QA_AAS_STOP_ENTER_LAVA | QA_AAS_STOP_HIT_GROUND_DAMAGE;
  if ((qa_aas_at_setting(world, from)->contents & 8) == 0 && (qa_aas_at_setting(world, to)->contents & 8) == 0)
    stop_events |= QA_AAS_STOP_TOUCH_CLUSTER_PORTAL;
  qa_vec3 test_end = best_end;
  bool found = false;
  for (int i = 0; i < 3; i++) {
    if (i == 1) test_end = qa_add3(test_end, sideways);
    else if (i == 2) test_end = qa_sub3(best_end, sideways);
    else test_end = best_end;
    direction = qa_normalize3(with_z(qa_sub3(test_end, best_start), 0));
    qa_bot_movement_prediction request; memset(&request, 0, sizeof request);
    request.entity_num = -1; request.origin = best_start; request.presence = 2; request.on_ground = true;
    request.velocity = qa_scale3(direction, speed); request.command_move = command;
    request.command_frames = 3; request.max_frames = 30; request.frame_time = fr(0.1);
    request.stop_events = stop_events; request.stop_area = 0; request.visualize = false;
    qa_aas_client_move move = qa_aas_spatial_predict_client_movement(spatial, request);
    if (move.frames >= 30 || (move.stop_event & (QA_AAS_STOP_ENTER_SLIME | QA_AAS_STOP_ENTER_LAVA | QA_AAS_STOP_TOUCH_CLUSTER_PORTAL)) != 0) return false;
    qa_vec3 test_start = ma(move.end, -64, direction);
    size_t crossing_count = 0;
    qa_aas_area_crossing *crossings = qa_aas_spatial_trace_areas(spatial, move.end, with_z(test_start, test_start.z + 1), 10, &crossing_count);
    for (size_t ci = 0; ci < crossing_count; ci++) if (crossings[ci].area == to) found = true;
    free(crossings);
    if (found) break;
  }
  if (!found) return false;
  if (g->context->debug) {
    char message[128];
    snprintf(message, sizeof message, "jump reachability between %lld and %lld\r\n", (long long)from, (long long)to);
    qa_aas_reachability_context_log(g->context, message);
  }
  qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(g->context);
  if (!reach) return false;
  qa_aas_link_set_area(reach, to); qa_aas_link_set_face(reach, 0); qa_aas_link_set_edge(reach, 0);
  qa_aas_link_set_start(reach, best_start); qa_aas_link_set_end(reach, best_end);
  qa_aas_link_set_travel_type(reach, travel_type);
  qa_vec3 delta = qa_sub3(best_end, best_start);
  double height = delta.z;
  if ((travel_type & QA_TRAVEL_MASK) == QA_TRAVEL_WALKOFFLEDGE && height > qa_length3(with_z(delta, 0))) {
    qa_aas_link_set_travel_time(reach, fr((double)settings->start_walk_off_ledge_time + (double)fr((double)fr(height * 50) / (double)settings->gravity)));
  } else {
    qa_aas_link_set_travel_time(reach, fr((double)settings->start_jump_time + (double)fr((double)fr(qa_length3(qa_sub3(best_start, best_end)) * 240) / (double)settings->max_walk_velocity)));
  }
  if ((qa_aas_at_setting(world, to)->contents & 128) == 0) {
    if (qa_aas_fall_delta(settings, fr(best_start.z - best_end.z)) > (double)settings->fall_delta5)
      qa_aas_link_set_travel_time(reach, fr((double)qa_aas_link_travel_time(reach) + (double)settings->fall_damage5_time));
    else if (qa_aas_fall_delta(settings, fr(best_start.z - best_end.z)) > (double)settings->fall_delta10)
      qa_aas_link_set_travel_time(reach, fr((double)qa_aas_link_travel_time(reach) + (double)settings->fall_damage10_time));
  }
  link_reach(g, from, reach);
  qa_aas_reachability_debug_count(&g->context->debug_state, (travel_type & QA_TRAVEL_MASK) == QA_TRAVEL_JUMP ? "jump" : "walkoffledge");
  return false;
}
bool qa_aas_reachability_geometry_ladder(qa_aas_reachability_geometry *g, int64_t from, int64_t to) {
  qa_aas_asset *world = &g->context->world->asset;
  const qa_aas_movement_settings *settings = g->context->settings;
  qa_aas_reachability_spatial *spatial = g->context->spatial;
  if (!area_ladder(g, from) || !area_ladder(g, to)) return false;
  double maximum_height = qa_aas_max_jump_height(settings, settings->jump_velocity);
  const qa_aas_area_rec *area1 = qa_aas_at_area(world, from), *area2 = qa_aas_at_area(world, to);
  bool has_best = false;
  const qa_aas_face_rec *best_face1 = NULL, *best_face2 = NULL;
  int64_t best_number1 = 0, best_number2 = 0, best_edge = 0;
  double best_area1 = 0, best_area2 = 0;
  for (int64_t i = 0; i < area1->face_count; i++) {
    int64_t number1 = qa_aas_at_i64(world->face_indexes, world->face_index_count, area1->first_face + i);
    const qa_aas_face_rec *face1 = qa_aas_at_face(world, abs64(number1));
    if ((face1->flags & 2) == 0) continue;
    for (int64_t j = 0; j < area2->face_count; j++) {
      int64_t number2 = qa_aas_at_i64(world->face_indexes, world->face_index_count, area2->first_face + j);
      const qa_aas_face_rec *face2 = qa_aas_at_face(world, abs64(number2));
      if ((face2->flags & 2) == 0) continue;
      bool shared = false;
      for (int64_t k = 0; k < face1->edge_count; k++) {
        int64_t edge1 = qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face1->first_edge + k);
        for (int64_t l = 0; l < face2->edge_count; l++) {
          if (abs64(edge1) != abs64(qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face2->first_edge + l))) continue;
          double surface1 = qa_aas_face_area(world, face1), surface2 = qa_aas_face_area(world, face2);
          if (surface1 > (has_best ? best_area1 : -9999) && surface2 > (has_best ? best_area2 : -9999)) {
            has_best = true; best_face1 = face1; best_face2 = face2;
            best_number1 = number1; best_number2 = number2;
            best_area1 = surface1; best_area2 = surface2; best_edge = edge1;
          }
          shared = true; break;
        }
        if (shared) break;
      }
    }
  }
  if (!has_best) return false;
  const qa_aas_edge_rec *edge = qa_aas_at_edge(world, abs64(best_edge));
  qa_vec3 v1 = qa_aas_at_vertex(world, edge->vertices[best_edge < 0 ? 1 : 0]);
  qa_vec3 v2 = qa_aas_at_vertex(world, edge->vertices[best_edge < 0 ? 0 : 1]);
  qa_vec3 midpoint = qa_scale3(qa_add3(v1, v2), 0.5);
  qa_vec3 shared_edge = qa_sub3(v2, v1);
  const qa_aas_plane_rec *plane1 = qa_aas_at_plane(world, best_face1->plane ^ (best_number1 < 0 ? 1 : 0));
  const qa_aas_plane_rec *plane2 = qa_aas_at_plane(world, best_face2->plane ^ (best_number2 < 0 ? 1 : 0));
  qa_vec3 direction = qa_normalize3(qa_cross3(plane1->normal, shared_edge));
  qa_vec3 point1 = ma(midpoint, -32, direction), point2 = ma(midpoint, 32, direction);
  bool vertical1 = integer_abs(qa_dot3(plane1->normal, UP)) < 0.1;
  bool vertical2 = integer_abs(qa_dot3(plane2->normal, UP)) < 0.1;
  if (!vertical1 && !vertical2) return false;
  if (vertical1 && vertical2 && qa_dot3(plane1->normal, plane2->normal) > 0.7 && integer_abs(qa_dot3(shared_edge, UP)) < 0.7) {
    qa_aas_linked_reachability *first = qa_aas_reachability_context_allocate(g->context);
    if (!first) return false;
    qa_aas_link_set_area(first, to); qa_aas_link_set_face(first, best_number1); qa_aas_link_set_edge(first, abs64(best_edge));
    qa_aas_link_set_start(first, point1);
    qa_aas_link_set_end(first, ma(point2, -3, plane1->normal));
    qa_aas_link_set_travel_type(first, QA_TRAVEL_LADDER); qa_aas_link_set_travel_time(first, 10);
    link_reach(g, from, first);
    qa_aas_reachability_debug_count(&g->context->debug_state, "ladder");
    qa_aas_linked_reachability *second = qa_aas_reachability_context_allocate(g->context);
    if (!second) return false;
    qa_aas_link_set_area(second, from); qa_aas_link_set_face(second, best_number2); qa_aas_link_set_edge(second, abs64(best_edge));
    qa_aas_link_set_start(second, point2);
    qa_aas_link_set_end(second, ma(point1, -3, plane1->normal));
    qa_aas_link_set_travel_type(second, QA_TRAVEL_LADDER); qa_aas_link_set_travel_time(second, 10);
    link_reach(g, to, second);
    qa_aas_reachability_debug_count(&g->context->debug_state, "ladder");
    return true;
  }
  if (vertical1 && (best_face2->flags & 4) != 0) {
    qa_aas_linked_reachability *first = qa_aas_reachability_context_allocate(g->context);
    if (!first) return false;
    qa_aas_link_set_area(first, to); qa_aas_link_set_face(first, best_number1); qa_aas_link_set_edge(first, abs64(best_edge));
    qa_aas_link_set_start(first, point1);
    qa_aas_link_set_end(first, ma(with_z(point2, point2.z + 16), -15, plane1->normal));
    qa_aas_link_set_travel_type(first, QA_TRAVEL_LADDER); qa_aas_link_set_travel_time(first, 10);
    link_reach(g, from, first);
    qa_aas_reachability_debug_count(&g->context->debug_state, "ladder");
    qa_aas_linked_reachability *second = qa_aas_reachability_context_allocate(g->context);
    if (!second) return false;
    qa_aas_link_set_area(second, from); qa_aas_link_set_face(second, best_number2); qa_aas_link_set_edge(second, abs64(best_edge));
    qa_aas_link_set_start(second, point2);
    qa_aas_link_set_end(second, point1);
    qa_aas_link_set_travel_type(second, QA_TRAVEL_WALKOFFLEDGE); qa_aas_link_set_travel_time(second, 10);
    link_reach(g, to, second);
    qa_aas_reachability_debug_count(&g->context->debug_state, "walkoffledge");
    return true;
  }
  if (!vertical1) return false;
  bool has_lowest = false;
  qa_vec3 lowest_point = {0, 0, 0};
  int64_t lowest_edge = 0;
  for (int64_t i = 0; i < best_face1->edge_count; i++) {
    int64_t edge_number = abs64(qa_aas_at_i64(world->edge_indexes, world->edge_index_count, best_face1->first_edge + i));
    const qa_aas_edge_rec *current = qa_aas_at_edge(world, edge_number);
    qa_vec3 point = qa_scale3(qa_add3(qa_aas_at_vertex(world, current->vertices[0]), qa_aas_at_vertex(world, current->vertices[1])), 0.5);
    if (point.z < (has_lowest ? lowest_point.z : 99999)) { has_lowest = true; lowest_point = point; lowest_edge = edge_number; }
  }
  plane1 = qa_aas_at_plane(world, best_face1->plane);
  if (!has_lowest) qa_range_error("AAS_Reachability_Ladder reads an uninitialized lowest point");
  qa_vec3 offset = ma(lowest_point, 5, plane1->normal);
  qa_vec3 start = with_z(offset, offset.z + 5), end = with_z(offset, offset.z - 100);
  qa_aas_trace trace = qa_aas_spatial_trace_client_bbox(spatial, start, end, 2, -1);
  if (g->context->debug && trace.start_solid) {
    char message[128];
    snprintf(message, sizeof message, "trace from area %lld started in solid\r\n", (long long)from);
    qa_aas_reachability_context_log(g->context, message);
  }
  qa_vec3 trace_end = with_z(trace.end, trace.end.z + 1);
  int64_t destination = qa_aas_reachability_world_point_area(g->context->world, trace_end);
  const qa_aas_area_rec *destination_area = qa_aas_at_area(world, destination);
  for (int64_t i = 0; i < destination_area->face_count; i++) {
    const qa_aas_face_rec *face = qa_aas_at_face(world, abs64(qa_aas_at_i64(world->face_indexes, world->face_index_count, destination_area->first_face + i)));
    if ((face->flags & 2) != 0 && integer_abs(qa_dot3(qa_aas_at_plane(world, face->plane)->normal, UP)) < 0.1) return false;
  }
  if (destination == from || qa_aas_reachability_context_exists(g->context, from, destination) ||
      qa_aas_reachability_context_exists(g->context, destination, from)) return false;
  if (!((double)fr(start.z - trace_end.z) < maximum_height)) {
    if (g->context->debug) {
      char message[160];
      snprintf(message, sizeof message, "jump too high between area %lld and %lld\r\n", (long long)destination, (long long)from);
      qa_aas_reachability_context_log(g->context, message);
    }
    return false;
  }
  qa_aas_linked_reachability *first = qa_aas_reachability_context_allocate(g->context);
  if (!first) return false;
  qa_aas_link_set_area(first, destination); qa_aas_link_set_face(first, best_number1); qa_aas_link_set_edge(first, lowest_edge);
  qa_aas_link_set_start(first, lowest_point);
  qa_aas_link_set_end(first, trace_end);
  qa_aas_link_set_travel_type(first, QA_TRAVEL_LADDER); qa_aas_link_set_travel_time(first, 10);
  link_reach(g, from, first);
  qa_aas_reachability_debug_count(&g->context->debug_state, "ladder");
  qa_aas_linked_reachability *second = qa_aas_reachability_context_allocate(g->context);
  if (!second) return false;
  qa_aas_link_set_area(second, from); qa_aas_link_set_face(second, best_number1); qa_aas_link_set_edge(second, lowest_edge);
  qa_aas_link_set_start(second, trace_end);
  qa_vec3 finish = ma(lowest_point, -5, plane1->normal);
  qa_aas_link_set_end(second, with_z(finish, finish.z + 10));
  qa_aas_link_set_travel_type(second, QA_TRAVEL_JUMP); qa_aas_link_set_travel_time(second, 10);
  link_reach(g, destination, second);
  qa_aas_reachability_debug_count(&g->context->debug_state, "jump");
  return true;
}
void qa_aas_reachability_geometry_walk_off_ledge(qa_aas_reachability_geometry *g, int64_t area_number) {
  qa_aas_asset *world = &g->context->world->asset;
  const qa_aas_movement_settings *settings = g->context->settings;
  qa_aas_reachability_spatial *spatial = g->context->spatial;
  if (!area_grounded(g, area_number) || area_swim(g, area_number)) return;
  const qa_aas_area_rec *area = qa_aas_at_area(world, area_number);
  for (int64_t i = 0; i < area->face_count; i++) {
    const qa_aas_face_rec *face1 = qa_aas_at_face(world, abs64(qa_aas_at_i64(world->face_indexes, world->face_index_count, area->first_face + i)));
    if ((face1->flags & 4) == 0) continue;
    for (int64_t k = 0; k < face1->edge_count; k++) {
      int64_t edge_number = qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face1->first_edge + k);
      for (int64_t j = 0; j < area->face_count; j++) {
        int64_t number2 = qa_aas_at_i64(world->face_indexes, world->face_index_count, area->first_face + j);
        const qa_aas_face_rec *face2 = qa_aas_at_face(world, abs64(number2));
        if ((face2->flags & 4) != 0) continue;
        for (int64_t l = 0; l < face2->edge_count; l++) {
          if (abs64(edge_number) != abs64(qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face2->first_edge + l))) continue;
          int64_t other = face2->front_area == area_number ? face2->back_area : face2->front_area;
          const qa_aas_area_rec *other_area = qa_aas_at_area(world, other);
          if (area_grounded(g, other)) {
            bool gap = false, shared = false;
            for (int64_t n = 0; n < other_area->face_count; n++) {
              int64_t number3 = qa_aas_at_i64(world->face_indexes, world->face_index_count, other_area->first_face + n);
              if (abs64(number3) == abs64(number2)) continue;
              const qa_aas_face_rec *face3 = qa_aas_at_face(world, abs64(number3));
              for (int64_t m = 0; m < face3->edge_count; m++) {
                if (abs64(qa_aas_at_i64(world->edge_indexes, world->edge_index_count, face3->first_edge + m)) != abs64(edge_number)) continue;
                gap = (face3->flags & 1) == 0 || (face3->flags & 4) == 0;
                shared = true; break;
              }
              if (shared) break;
            }
            if (!gap) break;
          }
          const qa_aas_edge_rec *edge = qa_aas_at_edge(world, abs64(edge_number));
          qa_vec3 v1 = qa_aas_at_vertex(world, edge->vertices[edge_number < 0 ? 1 : 0]);
          qa_vec3 v2 = qa_aas_at_vertex(world, edge->vertices[edge_number < 0 ? 0 : 1]);
          qa_vec3 direction = qa_normalize3(qa_cross3(qa_aas_at_plane(world, face1->plane)->normal, qa_sub3(v2, v1)));
          qa_vec3 midpoint = ma(qa_scale3(qa_add3(v1, v2), 0.5), 8, direction);
          qa_vec3 test_end = with_z(midpoint, midpoint.z - 1000);
          qa_aas_trace trace = qa_aas_spatial_trace_client_bbox(spatial, midpoint, test_end, 4, -1);
          if (trace.start_solid) break;
          int64_t destination = qa_aas_reachability_world_point_area(g->context->world, trace.end);
          if (destination == area_number || qa_aas_reachability_context_exists(g->context, area_number, destination) ||
              (!area_grounded(g, destination) && !area_swim(g, destination)) ||
              (qa_aas_at_setting(world, destination)->contents & 6) != 0) break;
          size_t crossing_count = 0;
          qa_aas_area_crossing *crossings = qa_aas_spatial_trace_areas(spatial, midpoint, test_end, 10, &crossing_count);
          bool blocked = false;
          for (size_t ci = 0; ci < crossing_count; ci++)
            if ((qa_aas_at_setting(world, crossings[ci].area)->contents & 8) != 0) blocked = true;
          free(crossings);
          if (blocked) break;
          float distance = fr(midpoint.z - trace.end.z);
          if (settings->max_fall_height != 0 && fabs((double)distance) > (double)settings->max_fall_height) break;
          qa_aas_linked_reachability *reach = qa_aas_reachability_context_allocate(g->context);
          if (!reach) break;
          qa_aas_link_set_area(reach, destination); qa_aas_link_set_face(reach, 0); qa_aas_link_set_edge(reach, edge_number);
          qa_aas_link_set_start(reach, midpoint);
          qa_aas_link_set_end(reach, trace.end);
          qa_aas_link_set_travel_type(reach, QA_TRAVEL_WALKOFFLEDGE);
          qa_aas_link_set_travel_time(reach, (double)settings->start_walk_off_ledge_time + fabs((double)distance) * 50 / (double)settings->gravity);
          if (!area_swim(g, destination) && (qa_aas_at_setting(world, destination)->contents & 128) == 0) {
            if (qa_aas_fall_delta(settings, distance) > (double)settings->fall_delta5)
              qa_aas_link_set_travel_time(reach, fr((double)qa_aas_link_travel_time(reach) + (double)settings->fall_damage5_time));
            else if (qa_aas_fall_delta(settings, distance) > (double)settings->fall_delta10)
              qa_aas_link_set_travel_time(reach, fr((double)qa_aas_link_travel_time(reach) + (double)settings->fall_damage10_time));
          }
          link_reach(g, area_number, reach);
          qa_aas_reachability_debug_count(&g->context->debug_state, "walkoffledge");
        }
      }
    }
  }
}
