/* Port of src/bots/navigation/helpers.ts. GPL-2.0-or-later. */
#include "qa/bots/navigation/helpers.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <string.h>
#include "qa/foundation/js.h"

const qa_navigation_node *qa_nav_at_node(const qa_navigation_node *values, size_t count, int64_t index) {
  if (index < 0 || index >= (int64_t)count) qa_range_error("Navigation record %lld exceeds %lld", (long long)index, (long long)count);
  return &values[index];
}
const qa_navigation_edge *qa_nav_at_edge(const qa_navigation_edge *values, size_t count, int64_t index) {
  if (index < 0 || index >= (int64_t)count) qa_range_error("Navigation record %lld exceeds %lld", (long long)index, (long long)count);
  return &values[index];
}
double qa_nav_distance(qa_vec3 a, qa_vec3 b) { return qa_js_hypot3(a.x - b.x, a.y - b.y, a.z - b.z); }
qa_vec3 qa_nav_midpoint(qa_vec3 a, qa_vec3 b) { return (qa_vec3){(a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2}; }
qa_bounds qa_nav_translated(qa_vec3 point, qa_bounds bounds) {
  return (qa_bounds){{point.x + bounds.min.x, point.y + bounds.min.y, point.z + bounds.min.z},
                     {point.x + bounds.max.x, point.y + bounds.max.y, point.z + bounds.max.z}};
}
bool qa_nav_crouched_profile(const qa_navigation_profile *profile, qa_navigation_profile *out) {
  if (!profile->has_crouched_shape || !(profile->capabilities & (1u << QA_TM_CROUCH))) return false;
  *out = *profile; out->shape = profile->crouched_shape; return true;
}
bool qa_nav_node_profile(const qa_navigation_profile *profile, const qa_navigation_node *node, qa_navigation_profile *out) {
  bool crouched = node->source.kind == QA_NS_AAS ? ((node->presence & 2) == 0 && (node->presence & 4) != 0)
                  : node->source.kind == QA_NS_NAV3 ? ((node->flags & 512) != 0)
                  : (node->source.kind == QA_NS_CONSTRUCTED && node->presence == 4);
  if (!crouched) { *out = *profile; return true; }
  return qa_nav_crouched_profile(profile, out);
}
int64_t qa_nav_contents(const qa_navigation_world *world, const qa_navigation_profile *profile, qa_vec3 point, qa_query_target target) {
  qa_point_contents_query q = {point, target, profile->policy, profile->movement.numeric, world->pass_actor};
  qa_point_contents_result sample = world->scene.point_contents(world->scene.ctx, q);
  if (sample.kind && !strcmp(sample.kind, "q1"))
    return sample.contents == -3 ? 1 : sample.contents == -4 ? 2 : sample.contents == -5 ? 4 : 0;
  bool q2 = sample.kind && !strcmp(sample.kind, "q2");
  int64_t value = (int64_t)(q2 ? sample.merged : sample.contents);
  return ((value & 32) != 0 ? 1 : 0) | ((value & 16) != 0 ? 2 : 0) | ((value & 8) != 0 ? 4 : 0)
       | (q2 && (value & 0x20000000) != 0 ? 8 : 0);
}
qa_trace_result qa_nav_trace(const qa_navigation_world *world, const qa_navigation_profile *profile, qa_vec3 start, qa_vec3 end, qa_query_target target) {
  qa_trace_query q = {start, end, profile->shape, target, profile->policy, profile->movement.numeric, world->pass_actor};
  return world->scene.trace(world->scene.ctx, q);
}
bool qa_nav_clear(const qa_navigation_world *world, const qa_navigation_profile *profile, qa_vec3 start, qa_vec3 end, qa_query_target target) {
  qa_trace_result result = qa_nav_trace(world, profile, start, end, target);
  return !result.start_solid && !result.all_solid && result.fraction == 1;
}
static qa_query_target world_target(void) { qa_query_target t; memset(&t, 0, sizeof t); return t; }
int64_t qa_nav_contents_world(const qa_navigation_world *world, const qa_navigation_profile *profile, qa_vec3 point) {
  return qa_nav_contents(world, profile, point, world_target());
}
bool qa_nav_clear_world(const qa_navigation_world *world, const qa_navigation_profile *profile, qa_vec3 start, qa_vec3 end) {
  return qa_nav_clear(world, profile, start, end, world_target());
}
qa_trace_result qa_nav_trace_world(const qa_navigation_world *world, const qa_navigation_profile *profile, qa_vec3 start, qa_vec3 end) {
  return qa_nav_trace(world, profile, start, end, world_target());
}
void qa_nav_validate_profile(const qa_navigation_profile *profile) {
  const qa_trace_shape *shapes[2] = {&profile->shape, &profile->crouched_shape};
  size_t n = profile->has_crouched_shape ? 2 : 1;
  for (size_t i = 0; i < n; i++) {
    qa_bounds b = shapes[i]->bounds;
    double vals[9] = {b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z,
                      profile->maximum_step, profile->maximum_drop, profile->minimum_floor_normal};
    for (int j = 0; j < 9; j++) if (!isfinite(vals[j])) qa_range_error("Navigation profile must be finite");
    if (b.min.x >= b.max.x || b.min.y >= b.max.y || b.min.z >= b.max.z
        || profile->maximum_step < 0 || profile->maximum_drop < 0
        || profile->minimum_floor_normal <= 0 || profile->minimum_floor_normal > 1)
      qa_range_error("Invalid navigation body/traversal envelope");
  }
  const char *kind = profile->movement.kind ? profile->movement.kind : "";
  const char *family = !strncmp(kind, "q1", 2) ? "q1" : !strncmp(kind, "q2", 2) ? "q2" : "q3";
  if (!profile->policy.kind || strcmp(profile->policy.kind, family))
    qa_range_error("Navigation collision policy must follow selected movement, independently of BSP format");
}
