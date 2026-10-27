/* Port of src/content/q3/base/game/radius-damage.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/radius_damage.h"
#include <math.h>
#include <stdlib.h>
#include "qa/core/math.h"
static int actor_equal(qa_actor_id a, qa_actor_id b) { return a.slot == b.slot && a.generation == b.generation && a.token == b.token && a.session.token == b.session.token; }
static qa_q3_radius_trace_result do_trace(qa_q3_radius_spatial s, qa_vec3 origin, qa_vec3 end) {
  qa_q3_radius_trace_query q = {origin, end, false, {{"", NULL}, 0, 0, NULL}, 1}; return s.trace_actor(s.ctx, q);
}
bool qa_q3_can_damage(qa_q3_radius_spatial spatial, qa_actor_id actor, qa_bounds bounds, qa_vec3 origin) {
  qa_vec3 midpoint = qa_scale3(qa_add3(bounds.min, bounds.max), 0.5);
  qa_q3_radius_trace_result center = do_trace(spatial, origin, midpoint);
  if (center.fraction == 1 || (center.hit_kind == QA_Q3_RADIUS_HIT_ACTOR && actor_equal(center.hit_actor, actor))) return true;
  static const double off[4][2] = {{15, 15}, {15, -15}, {-15, 15}, {-15, -15}};
  for (int i = 0; i < 4; i++) {
    qa_vec3 end = {midpoint.x + off[i][0], midpoint.y + off[i][1], midpoint.z};
    if (do_trace(spatial, origin, end).fraction == 1) return true;
  }
  return false;
}
static double axis_dist(double v, double mn, double mx) { return v < mn ? mn - v : v > mx ? v - mx : 0; }
bool qa_q3_radius_damage(qa_q3_radius_host host, qa_vec3 origin, double amount, double radius, const qa_actor_id *ignore) {
  radius = fmax(1, (float)radius); amount = (float)amount;
  qa_vec3 extent = {radius, radius, radius};
  qa_bounds area = {qa_sub3(origin, extent), qa_add3(origin, extent)};
  qa_actor_id *candidates = malloc(1024 * sizeof *candidates); if (!candidates) return false;
  size_t n = host.spatial.area_actors(host.spatial.ctx, area, 1024, candidates, 1024);
  bool hit_client = false;
  for (size_t i = 0; i < n; i++) {
    qa_actor_id actor = candidates[i]; if (ignore && actor_equal(actor, *ignore)) continue;
    qa_q3_radius_target target; if (!host.target(host.ctx, actor, &target)) continue;
    qa_vec3 d = {axis_dist(origin.x, target.bounds.min.x, target.bounds.max.x),
      axis_dist(origin.y, target.bounds.min.y, target.bounds.max.y), axis_dist(origin.z, target.bounds.min.z, target.bounds.max.z)};
    double distance = qa_length3(d); if (distance >= radius) continue;
    double points = (float)(amount * (float)(1 - (float)(distance / radius)));
    if (!qa_q3_can_damage(host.spatial, actor, target.bounds, origin)) continue;
    if (target.accuracy_eligible) hit_client = true;
    host.damage(host.ctx, actor, qa_add3(qa_sub3(target.origin, origin), (qa_vec3){0, 0, 24}), origin, trunc(points));
  }
  free(candidates); return hit_client;
}
