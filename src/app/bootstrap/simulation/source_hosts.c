/* Port of src/app/bootstrap/simulation/source-hosts.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/source_hosts.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static qa_q1b_trace q1_trace(void *ctx, const qa_q1b_trace_request *request) {
  qa_app_sim_q1_actor_host_env *env = ctx;
  qa_app_sim_actor_host_world *world = &env->world;
  qa_trace_query q; memset(&q, 0, sizeof q);
  q.start = request->start; q.end = request->end;
  q.shape.kind = "box"; q.shape.bounds = request->bounds;
  q.target.is_model = false;
  q.policy.kind = "q1"; q.policy.move = request->missile ? "missile" : request->monsters ? "normal" : "no-monsters";
  q.policy.has_hull = false; q.numeric = world->numeric;
  q.pass_actor = request->ignore.has ? &request->ignore.id : NULL;
  qa_trace_result trace = qa_shared_trace(world->scene, q);
  if (!is(trace.kind, "q1")) qa_range_error("Q1 actor trace returned another source representation");
  qa_point_contents_query cq; memset(&cq, 0, sizeof cq);
  cq.point = trace.end; cq.target.is_model = false;
  cq.policy.kind = "q1"; cq.policy.move = "normal"; cq.numeric = world->numeric;
  cq.pass_actor = q.pass_actor;
  qa_point_contents_result contents = qa_shared_point_contents(world->scene, cq);
  qa_q1b_trace out; memset(&out, 0, sizeof out);
  out.fraction = trace.fraction; out.end = trace.end; out.normal = trace.source_plane_q1.normal;
  if (trace.hit.kind == QA_TRACE_HIT_ACTOR) { out.actor.has = true; out.actor.id = trace.hit.actor; }
  else if (trace.hit.kind == QA_TRACE_HIT_WORLD) {
    qa_actor_id wa;
    if (world->world_actor(world->ctx, &wa)) { out.actor.has = true; out.actor.id = wa; }
  }
  out.start_solid = trace.start_solid; out.all_solid = trace.all_solid;
  out.sky = (((int)(trace.has_surface_flags ? trace.surface_flags : 0) & 4) != 0)
      || (is(contents.kind, "q1") && contents.contents == -6);
  out.in_open = trace.in_open; out.in_water = trace.in_water;
  return out;
}
static const char *q1_contents(void *ctx, qa_vec3 point) {
  qa_app_sim_q1_actor_host_env *env = ctx;
  qa_point_contents_query q; memset(&q, 0, sizeof q);
  q.point = point; q.target.is_model = false; q.pass_actor = NULL;
  q.policy.kind = "q1"; q.policy.move = "normal"; q.numeric = env->world.numeric;
  qa_point_contents_result result = qa_shared_point_contents(env->world.scene, q);
  if (!is(result.kind, "q1")) qa_range_error("Q1 actor contents returned another source representation");
  double v = result.contents;
  return v == -2 ? "solid" : v == -3 ? "water" : v == -4 ? "slime" : v == -5 ? "lava" : v == -6 ? "sky" : "empty";
}
qa_q1b_host qa_app_sim_create_q1_actor_host(qa_app_sim_q1_actor_host_env *env, qa_q1b_host bindings, qa_app_sim_actor_host_world world) {
  env->bindings = bindings; env->world = world;
  qa_q1b_host out = bindings;
  out.ctx = env; out.trace = q1_trace; out.contents = q1_contents;
  return out;
}
static bool visible(qa_app_sim_q2_actor_host_env *env, qa_vec3 first, qa_vec3 second, const char *kind) {
  qa_shared_scene_queries *scene = env->world.scene;
  return qa_shared_cluster_visible(scene,
      qa_shared_leaf_cluster(scene, qa_shared_point_leaf(scene, first)),
      qa_shared_leaf_cluster(scene, qa_shared_point_leaf(scene, second)), kind);
}
static qa_trace_result q2_trace(void *ctx, qa_q2_trace_request request) {
  qa_app_sim_q2_actor_host_env *env = ctx;
  qa_trace_query q; memset(&q, 0, sizeof q);
  q.start = request.start; q.end = request.end;
  if (request.bounds == NULL) q.shape.kind = "point";
  else { q.shape.kind = "box"; q.shape.bounds = *request.bounds; }
  q.target.is_model = false;
  q.policy.kind = "q2"; q.policy.contents_mask = request.mask; q.policy.leaf_contents = "merged";
  q.numeric = env->world.numeric;
  q.pass_actor = request.ignore;
  return (request.exclude == NULL || request.exclude_count == 0)
      ? qa_shared_trace(env->world.scene, q)
      : qa_shared_trace_excluding(env->world.scene, q, request.exclude, request.exclude_count);
}
static double q2_contents(void *ctx, qa_vec3 point) {
  qa_app_sim_q2_actor_host_env *env = ctx;
  qa_point_contents_query q; memset(&q, 0, sizeof q);
  q.point = point; q.target.is_model = false; q.pass_actor = NULL;
  q.policy.kind = "q2"; q.policy.contents_mask = -1; q.policy.leaf_contents = "merged";
  q.numeric = env->world.numeric;
  qa_point_contents_result result = qa_shared_point_contents(env->world.scene, q);
  if (!is(result.kind, "q2")) qa_range_error("Q2 actor contents returned another source representation");
  return result.merged;
}
static bool q2_in_pvs(void *ctx, qa_vec3 a, qa_vec3 b) { return visible(ctx, a, b, "pvs"); }
static bool q2_in_phs(void *ctx, qa_vec3 a, qa_vec3 b) { return visible(ctx, a, b, "phs"); }
static bool q2_areas(void *ctx, qa_vec3 a, qa_vec3 b) {
  qa_app_sim_q2_actor_host_env *env = ctx; qa_shared_scene_queries *scene = env->world.scene;
  return qa_shared_areas_connected(scene,
      qa_shared_leaf_area(scene, qa_shared_point_leaf(scene, a)),
      qa_shared_leaf_area(scene, qa_shared_point_leaf(scene, b)));
}
static int order_cmp(const void *a, const void *b) { (void)a; (void)b; return 0; }
static qa_app_sim_actor_host_world *sort_world;
static int nearby_cmp(const void *a, const void *b) {
  double v = sort_world->source_order(sort_world->ctx, *(const qa_actor_id *)a, *(const qa_actor_id *)b);
  return v < 0 ? -1 : v > 0 ? 1 : 0;
}
static const qa_actor_id *q2_nearby(void *ctx, qa_vec3 origin, double radius, size_t *out_count) {
  qa_app_sim_q2_actor_host_env *env = ctx;
  size_t count = 0;
  const qa_actor_id *all = env->world.observations(env->world.ctx, &count);
  env->nearby_count = 0;
  for (size_t i = 0; i < count; i++) {
    qa_body_state body;
    if (!env->world.body_read(env->world.ctx, all[i], &body)) continue;
    double dx = body.origin.x - origin.x, dy = body.origin.y - origin.y, dz = body.origin.z - origin.z;
    if (hypot(hypot(dx, dy), dz) <= radius && env->nearby_count < 256)
      env->nearby_storage[env->nearby_count++] = all[i];
  }
  sort_world = &env->world;
  qsort(env->nearby_storage, env->nearby_count, sizeof env->nearby_storage[0], env->world.source_order ? nearby_cmp : order_cmp);
  if (out_count) *out_count = env->nearby_count;
  return env->nearby_storage;
}
static qa_bounds q2_model_bounds(void *ctx, double model) {
  return qa_shared_model_bounds(((qa_app_sim_q2_actor_host_env *)ctx)->world.scene, model);
}
qa_q2_foundation_host qa_app_sim_create_q2_actor_host(qa_app_sim_q2_actor_host_env *env, qa_q2_foundation_host bindings, qa_app_sim_actor_host_world world) {
  env->bindings = bindings; env->world = world;
  qa_q2_foundation_host out = bindings;
  out.ctx = env;
  out.trace = q2_trace; out.point_contents = q2_contents;
  out.in_pvs = q2_in_pvs; out.in_phs = q2_in_phs; out.areas_connected = q2_areas;
  out.nearby = q2_nearby; out.inline_model_bounds = q2_model_bounds;
  return out;
}
