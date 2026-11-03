/* Port of src/app/bootstrap/simulation/source-hosts.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/source_hosts.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static qa_q1_trace q1_trace(void *ctx, qa_q1_trace_request request) {
  qa_app_sim_q1_actor_host_env *env = ctx;
  qa_app_sim_actor_host_world *world = &env->world;
  qa_trace_query q; memset(&q, 0, sizeof q);
  q.start = request.start; q.end = request.end;
  q.shape.kind = "box"; q.shape.bounds = request.bounds;
  q.target.is_model = false;
  q.policy.kind = "q1"; q.policy.move = request.missile ? "missile" : request.monsters ? "normal" : "no-monsters";
  q.policy.has_hull = false; q.numeric = world->numeric;
  q.pass_actor = request.has_ignore ? &request.ignore : NULL;
  qa_trace_result trace = qa_shared_trace(world->scene, q);
  if (!is(trace.kind, "q1")) qa_range_error("Q1 actor trace returned another source representation");
  qa_q1_trace out; memset(&out, 0, sizeof out);
  out.fraction = trace.fraction; out.end = trace.end; out.normal = trace.source_plane_q1.normal;
  if (trace.hit.kind == QA_TRACE_HIT_ACTOR) { out.has_actor = true; out.actor = trace.hit.actor; }
  else if (trace.hit.kind == QA_TRACE_HIT_WORLD) out.has_actor = world->world_actor(world->ctx, &out.actor);
  out.start_solid = trace.start_solid; out.all_solid = trace.all_solid;
  /* Sky only matters where the trace stopped; an unobstructed trace skips the contents query. */
  out.sky = ((qa_js_to_int32(trace.has_surface_flags ? trace.surface_flags : 0) & 4) != 0);
  if (!out.sky && trace.fraction < 1) {
    qa_point_contents_query cq; memset(&cq, 0, sizeof cq);
    cq.point = trace.end; cq.target.is_model = false;
    cq.policy.kind = "q1"; cq.policy.move = "normal"; cq.numeric = world->numeric;
    cq.pass_actor = q.pass_actor;
    qa_point_contents_result contents = qa_shared_point_contents(world->scene, cq);
    out.sky = is(contents.kind, "q1") && contents.contents == -6;
  }
  out.in_open = trace.in_open; out.in_water = trace.in_water;
  return out;
}
static qa_q1_contents q1_contents(void *ctx, qa_vec3 point) {
  qa_app_sim_q1_actor_host_env *env = ctx;
  qa_point_contents_query q; memset(&q, 0, sizeof q);
  q.point = point; q.target.is_model = false; q.pass_actor = NULL;
  q.policy.kind = "q1"; q.policy.move = "normal"; q.numeric = env->world.numeric;
  qa_point_contents_result result = qa_shared_point_contents(env->world.scene, q);
  if (!is(result.kind, "q1")) qa_range_error("Q1 actor contents returned another source representation");
  double v = result.contents;
  return v == -2 ? "solid" : v == -3 ? "water" : v == -4 ? "slime" : v == -5 ? "lava" : v == -6 ? "sky" : "empty";
}
/* TS `{ ...bindings, trace, contents }`: the spread methods keep their own
 * receiver, so each forwards to the binding with the binding's ctx. */
#define Q1B(ctx) (((qa_app_sim_q1_actor_host_env *)(ctx))->bindings)
static double q1f_random(void *ctx) { return Q1B(ctx).random(Q1B(ctx).ctx); }
static bool q1f_walk_move(void *ctx, qa_owned_actor actor, double yaw, double distance) { return Q1B(ctx).walk_move(Q1B(ctx).ctx, actor, yaw, distance); }
static void q1f_change_yaw(void *ctx, qa_owned_actor actor) { Q1B(ctx).change_yaw(Q1B(ctx).ctx, actor); }
static void q1f_move_to_goal(void *ctx, qa_owned_actor actor, qa_actor_id goal, double distance, const char *mode) { Q1B(ctx).move_to_goal(Q1B(ctx).ctx, actor, goal, distance, mode); }
static bool q1f_check_bottom(void *ctx, qa_actor_id actor) { return Q1B(ctx).check_bottom(Q1B(ctx).ctx, actor); }
static void q1f_schedule_think(void *ctx, qa_owned_actor actor, double due) { Q1B(ctx).schedule_think(Q1B(ctx).ctx, actor, due); }
static void q1f_cancel_think(void *ctx, qa_owned_actor actor) { Q1B(ctx).cancel_think(Q1B(ctx).ctx, actor); }
static void q1f_emit(void *ctx, qa_q1_event event) { Q1B(ctx).emit(Q1B(ctx).ctx, event); }
static void q1f_transition(void *ctx, qa_transition_intent intent) { Q1B(ctx).transition(Q1B(ctx).ctx, intent); }
static const qa_actor_id *q1f_players(void *ctx, size_t *out_count) { return Q1B(ctx).players(Q1B(ctx).ctx, out_count); }
static bool q1f_check_client(void *ctx, qa_owned_actor observer, qa_actor_id *out) { return Q1B(ctx).check_client(Q1B(ctx).ctx, observer, out); }
static const char *q1f_classname(void *ctx, qa_actor_id actor) { return Q1B(ctx).classname(Q1B(ctx).ctx, actor); }
static void q1f_powerup(void *ctx, qa_owned_actor actor, qa_q1_powerup powerup, double expires) { Q1B(ctx).powerup(Q1B(ctx).ctx, actor, powerup, expires); }
static void q1f_weapon_impact(void *ctx, qa_actor_id owner, qa_vec3 origin) { Q1B(ctx).weapon_impact(Q1B(ctx).ctx, owner, origin); }
static double q1f_weapon_volume(void *ctx, qa_actor_id actor) { return Q1B(ctx).weapon_volume(Q1B(ctx).ctx, actor); }
static qa_q1_monster_target_result q1f_monster_target(void *ctx, qa_actor_id actor) { return Q1B(ctx).monster_target(Q1B(ctx).ctx, actor); }
static void q1f_set_gravity(void *ctx, qa_actor_id actor, double scale) { Q1B(ctx).set_gravity(Q1B(ctx).ctx, actor, scale); }
static void q1f_control_player(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 view_offset) { Q1B(ctx).control_player(Q1B(ctx).ctx, actor, origin, angles, view_offset); }
static bool q1f_source_target(void *ctx, qa_actor_id actor, qa_q1_source_target *out) { return Q1B(ctx).source_target(Q1B(ctx).ctx, actor, out); }
static double q1f_powerup_expires(void *ctx, qa_actor_id actor, qa_q1_powerup powerup) { return Q1B(ctx).powerup_expires(Q1B(ctx).ctx, actor, powerup); }
static double q1f_source_damage_multiplier(void *ctx, qa_actor_id attacker) { return Q1B(ctx).source_damage_multiplier(Q1B(ctx).ctx, attacker); }
static qa_q1_pusher_services q1f_pusher_services(void *ctx, qa_q1_services *game) { return Q1B(ctx).pusher_services(Q1B(ctx).ctx, game); }
static qa_vec3 q1f_punch_read(void *ctx, qa_actor_id actor) { return Q1B(ctx).punch_read(Q1B(ctx).ctx, actor); }
static void q1f_punch_write(void *ctx, qa_actor_id actor, qa_vec3 angles) { Q1B(ctx).punch_write(Q1B(ctx).ctx, actor, angles); }
static void q1f_register_entity(void *ctx, qa_q1_actor *entity, qa_q1_services *services) { Q1B(ctx).register_entity(Q1B(ctx).ctx, entity, services); }
#define Q1F(member) out.member = bindings.member != NULL ? q1f_##member : NULL
qa_q1_host qa_app_sim_create_q1_actor_host(qa_app_sim_q1_actor_host_env *env, qa_q1_host bindings, qa_app_sim_actor_host_world world) {
  env->bindings = bindings; env->world = world;
  qa_q1_host out = bindings;
  out.ctx = env;
  out.trace = q1_trace; out.contents = q1_contents;
  Q1F(random); Q1F(walk_move); Q1F(change_yaw); Q1F(move_to_goal); Q1F(check_bottom);
  Q1F(schedule_think); Q1F(cancel_think); Q1F(emit); Q1F(transition); Q1F(players);
  Q1F(check_client); Q1F(classname); Q1F(powerup); Q1F(weapon_impact); Q1F(weapon_volume);
  Q1F(monster_target); Q1F(set_gravity); Q1F(control_player); Q1F(source_target);
  Q1F(powerup_expires); Q1F(source_damage_multiplier); Q1F(pusher_services);
  Q1F(punch_read); Q1F(punch_write); Q1F(register_entity);
  return out;
}
#undef Q1F
#undef Q1B
static bool visible(qa_app_sim_q2_actor_host_env *env, qa_vec3 first, qa_vec3 second, const char *kind) {
  qa_shared_scene_queries *scene = env->world.scene;
  double from = qa_shared_leaf_cluster(scene, qa_shared_point_leaf(scene, first));
  double to = qa_shared_leaf_cluster(scene, qa_shared_point_leaf(scene, second));
  /* sv_game.c adds the area test ("a door blocks hearing") to both queries. */
  if (!qa_shared_cluster_visible(scene, from, to, kind)) return false;
  double first_area = qa_shared_leaf_area(scene, qa_shared_point_leaf(scene, first));
  double second_area = qa_shared_leaf_area(scene, qa_shared_point_leaf(scene, second));
  return qa_shared_areas_connected(scene, first_area, second_area);
}
static qa_trace_result q2_trace(void *ctx, qa_q2_trace_request request) {
  qa_app_sim_q2_actor_host_env *env = ctx;
  qa_trace_query q; memset(&q, 0, sizeof q);
  q.start = request.start; q.end = request.end;
  if (request.bounds == NULL) q.shape.kind = "point";
  else { q.shape.kind = "box"; q.shape.bounds = *request.bounds; }
  q.target.is_model = false;
  q.policy.kind = "q2"; q.policy.contents_mask = request.mask; q.policy.leaf_contents = env->world.leaf_contents ? env->world.leaf_contents : "merged";
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
  q.policy.kind = "q2"; q.policy.contents_mask = -1; q.policy.leaf_contents = env->world.leaf_contents ? env->world.leaf_contents : "merged";
  q.numeric = env->world.numeric;
  qa_point_contents_result result = qa_shared_point_contents(env->world.scene, q);
  if (!is(result.kind, "q2")) qa_range_error("Q2 actor contents returned another source representation");
  return is(env->world.leaf_contents, "stored") ? result.stored : result.merged;
}
static bool q2_in_pvs(void *ctx, qa_vec3 a, qa_vec3 b) { return visible(ctx, a, b, "pvs"); }
static bool q2_in_phs(void *ctx, qa_vec3 a, qa_vec3 b) { return visible(ctx, a, b, "phs"); }
static bool q2_areas(void *ctx, qa_vec3 a, qa_vec3 b) {
  qa_app_sim_q2_actor_host_env *env = ctx; qa_shared_scene_queries *scene = env->world.scene;
  double first = qa_shared_leaf_area(scene, qa_shared_point_leaf(scene, a));
  double second = qa_shared_leaf_area(scene, qa_shared_point_leaf(scene, b));
  return qa_shared_areas_connected(scene,
      first, second);
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
    if (qa_js_hypot3(dx, dy, dz) <= radius && env->nearby_count < 256)
      env->nearby_storage[env->nearby_count++] = all[i];
  }
  sort_world = &env->world;
  qa_js_sort(env->nearby_storage, env->nearby_count, sizeof env->nearby_storage[0], env->world.source_order ? nearby_cmp : order_cmp);
  if (out_count) *out_count = env->nearby_count;
  return env->nearby_storage;
}
static qa_bounds q2_model_bounds(void *ctx, double model) {
  return qa_shared_model_bounds(((qa_app_sim_q2_actor_host_env *)ctx)->world.scene, model);
}
/* TS `{ ...bindings, trace, pointContents, ... }`: as for Q1, the spread
 * members keep their own receiver, so each forwards with the binding's ctx. */
#define Q2B(ctx) (((qa_app_sim_q2_actor_host_env *)(ctx))->bindings)
static qa_q2_weapon_target_result q2f_weapon_target(void *ctx, qa_actor_id actor) { return Q2B(ctx).weapon_target(Q2B(ctx).ctx, actor); }
static qa_q2_monster_target_result q2f_monster_target(void *ctx, qa_actor_id actor) { return Q2B(ctx).monster_target(Q2B(ctx).ctx, actor); }
static void q2f_register_entity(void *ctx, qa_q2_entity *entity, qa_q2_entity_services *services) { Q2B(ctx).register_entity(Q2B(ctx).ctx, entity, services); }
static double q2f_now(void *ctx) { return Q2B(ctx).now(Q2B(ctx).ctx); }
static double q2f_frame_seconds(void *ctx) { return Q2B(ctx).frame_seconds(Q2B(ctx).ctx); }
static double q2f_gravity(void *ctx) { return Q2B(ctx).gravity(Q2B(ctx).ctx); }
static double q2f_random(void *ctx) { return Q2B(ctx).random(Q2B(ctx).ctx); }
static void q2f_schedule(void *ctx, qa_owned_actor actor, const double *due_seconds) { Q2B(ctx).schedule(Q2B(ctx).ctx, actor, due_seconds); }
static void q2f_touch_triggers(void *ctx, qa_owned_actor actor) { Q2B(ctx).touch_triggers(Q2B(ctx).ctx, actor); }
static const qa_actor_id *q2f_players(void *ctx, size_t *out_count) { return Q2B(ctx).players(Q2B(ctx).ctx, out_count); }
static qa_actor_id q2f_world_actor(void *ctx) { return Q2B(ctx).world_actor(Q2B(ctx).ctx); }
static bool q2f_is_player(void *ctx, qa_actor_id actor) { return Q2B(ctx).is_player(Q2B(ctx).ctx, actor); }
static bool q2f_is_monster(void *ctx, qa_actor_id actor) { return Q2B(ctx).is_monster(Q2B(ctx).ctx, actor); }
static void q2f_set_solid(void *ctx, qa_owned_actor actor, const char *solid, const double *model) { Q2B(ctx).set_solid(Q2B(ctx).ctx, actor, solid, model); }
static void q2f_set_motion(void *ctx, qa_q2_motion motion) { Q2B(ctx).set_motion(Q2B(ctx).ctx, motion); }
static void q2f_set_area_portal(void *ctx, double portal, bool open) { Q2B(ctx).set_area_portal(Q2B(ctx).ctx, portal, open); }
static void q2f_emit(void *ctx, qa_q2_presentation_event event) { Q2B(ctx).emit(Q2B(ctx).ctx, event); }
static qa_q2_player_view_result q2f_player_view_state(void *ctx, qa_actor_id player) { return Q2B(ctx).player_view_state(Q2B(ctx).ctx, player); }
static void q2f_key_consumed(void *ctx, qa_actor_id player) { Q2B(ctx).key_consumed(Q2B(ctx).ctx, player); }
static void q2f_prepare_level_change(void *ctx, const char *map, const qa_q2_landmark_carry *landmark, double server_flags) { Q2B(ctx).prepare_level_change(Q2B(ctx).ctx, map, landmark, server_flags); }
static void q2f_transition(void *ctx, qa_transition_intent intent) { Q2B(ctx).transition(Q2B(ctx).ctx, intent); }
static void q2f_diagnostic(void *ctx, const char *message) { Q2B(ctx).diagnostic(Q2B(ctx).ctx, message); }
#define Q2F(member) out.member = bindings.member != NULL ? q2f_##member : NULL
qa_q2_foundation_host qa_app_sim_create_q2_actor_host(qa_app_sim_q2_actor_host_env *env, qa_q2_foundation_host bindings, qa_app_sim_actor_host_world world) {
  env->bindings = bindings; env->world = world;
  qa_q2_foundation_host out = bindings;
  out.ctx = env;
  out.trace = q2_trace; out.point_contents = q2_contents;
  out.in_pvs = q2_in_pvs; out.in_phs = q2_in_phs; out.areas_connected = q2_areas;
  out.nearby = q2_nearby; out.inline_model_bounds = q2_model_bounds;
  Q2F(weapon_target); Q2F(monster_target); Q2F(register_entity); Q2F(now); Q2F(frame_seconds); Q2F(gravity);
  Q2F(random); Q2F(schedule); Q2F(touch_triggers); Q2F(players); Q2F(world_actor); Q2F(is_player); Q2F(is_monster);
  Q2F(set_solid); Q2F(set_motion); Q2F(set_area_portal); Q2F(emit); Q2F(player_view_state); Q2F(key_consumed);
  Q2F(prepare_level_change); Q2F(transition); Q2F(diagnostic);
  return out;
}
#undef Q2F
#undef Q2B
