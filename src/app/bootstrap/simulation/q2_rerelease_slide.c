/* Port of src/app/bootstrap/simulation/q2-rerelease-slide.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q2_rerelease_slide.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
struct qa_app_sim_q2_rerelease_fly_move {
  qa_numeric_operations numeric;
  qa_q2_rerelease_movement movement;
};
typedef struct trace_env {
  qa_app_sim_q2_rerelease_fly_move_services host;
  qa_owned_actor actor;
  qa_q2_vec3 *origin, *velocity;
  qa_trace_hit entities[32]; size_t entity_count;
} trace_env;
static qa_vec3 vector_of(qa_q2_vec3 v) { return (qa_vec3){v[0], v[1], v[2]}; }
static void source_vector(qa_q2_vec3 out, qa_vec3 v) { out[0] = v.x; out[1] = v.y; out[2] = v.z; }
static qa_q2_cplane source_plane(qa_bsp_plane p) {
  qa_q2_cplane out; source_vector(out.normal, p.plane.normal); out.dist = p.plane.distance; out.type = p.type; out.signbits = p.signbits; return out;
}
static qa_bsp_plane scene_plane(qa_q2_cplane p) {
  qa_bsp_plane out; out.plane.normal = vector_of(p.normal); out.plane.distance = p.dist; out.type = p.type; out.signbits = p.signbits; return out;
}
static bool canonical(trace_env *env, qa_trace_hit hit, qa_trace_hit *out) {
  if (hit.kind == QA_TRACE_HIT_NONE) return false;
  for (size_t i = 0; i < env->entity_count; i++) {
    qa_trace_hit c = env->entities[i];
    bool same = (c.kind == QA_TRACE_HIT_WORLD && hit.kind == QA_TRACE_HIT_WORLD && c.model == hit.model)
        || (c.kind == QA_TRACE_HIT_ACTOR && hit.kind == QA_TRACE_HIT_ACTOR && qa_same_actor(c.actor, hit.actor));
    if (same) { *out = c; return true; }
  }
  if (env->entity_count < 32) env->entities[env->entity_count++] = hit;
  *out = hit; return true;
}
static qa_q2_trace trace_fn(void *ctx, qa_q2_vec3 start, qa_q2_vec3 mins, qa_q2_vec3 maxs, qa_q2_vec3 end) {
  trace_env *env = ctx;
  qa_body_state_result current = qa_shared_body_table_read(env->host.bodies, env->actor.id);
  if (current.has_value) {
    qa_body_state b = current.value; b.origin = vector_of(*env->origin); b.velocity = vector_of(*env->velocity);
    qa_shared_body_table_write(env->host.bodies, env->actor, b);
  }
  qa_bounds bounds = {vector_of(mins), vector_of(maxs)};
  qa_trace_result result = env->host.trace(env->host.ctx, vector_of(start), vector_of(end), bounds);
  if (result.kind == NULL || strcmp(result.kind, "q2") != 0) qa_range_error("Rerelease sliding requires Q2 trace fields");
  qa_q2_trace out; memset(&out, 0, sizeof out);
  out.allsolid = result.all_solid; out.startsolid = result.start_solid; out.fraction = result.fraction;
  source_vector(out.endpos, result.end);
  out.plane = source_plane(result.source_plane);
  if (result.surface) { out.has_surface = true; out.surface = *result.surface; out.surface_ref = result.surface; }
  if (result.has_secondary) {
    out.plane2 = source_plane(result.secondary_plane);
    if (result.secondary_surface) { out.has_surface2 = true; out.surface2 = *result.secondary_surface; out.surface2_ref = result.secondary_surface; }
  }
  out.contents = result.contents;
  out.has_ent = canonical(env, result.hit, &out.ent);
  out.source = result;
  return out;
}
qa_app_sim_q2_rerelease_fly_move *qa_app_sim_create_q2_rerelease_fly_move(qa_numeric_operations numeric, qa_q2_rerelease_movement_context *context) {
  qa_app_sim_q2_rerelease_fly_move *m = calloc(1, sizeof *m);
  if (!m) qa_range_error("Out of memory");
  m->numeric = numeric;
  qa_q2_create_rerelease_movement(&m->movement, &m->numeric, context, false, 1);
  return m;
}
void qa_app_sim_q2_rerelease_fly_move_destroy(qa_app_sim_q2_rerelease_fly_move *move) { free(move); }
void qa_app_sim_q2_rerelease_fly_move_run(qa_app_sim_q2_rerelease_fly_move *move, qa_owned_actor actor, double elapsed, qa_app_sim_q2_rerelease_fly_move_services host) {
  qa_body_state_result read = qa_shared_body_table_read(host.bodies, actor.id);
  if (!read.has_value || !qa_session_actor_registry_is_live(host.actors, actor.id)) return;
  qa_body_state body = read.value; body.ground = NULL;
  qa_shared_body_table_write(host.bodies, actor, body);
  qa_q2_vec3 origin, velocity, mins, maxs;
  source_vector(origin, body.origin); source_vector(velocity, body.velocity);
  source_vector(mins, body.bounds.min); source_vector(maxs, body.bounds.max);
  qa_q2_kex_touch_list touch; memset(&touch, 0, sizeof touch);
  trace_env env; memset(&env, 0, sizeof env);
  env.host = host; env.actor = actor; env.origin = &origin; env.velocity = &velocity;
  qa_q2_rerelease_movement_step_slide_move_generic(&move->movement, &origin, &velocity,
      move->numeric.store(move->numeric.ctx, elapsed), mins, maxs, &touch, false, trace_fn, &env);
  read = qa_shared_body_table_read(host.bodies, actor.id);
  if (!read.has_value || !qa_session_actor_registry_is_live(host.actors, actor.id)) return;
  body = read.value; body.origin = vector_of(origin); body.velocity = vector_of(velocity);
  qa_shared_body_table_write(host.bodies, actor, body);
  for (size_t i = 0; i < touch.num && i < 32; i++) {
    qa_q2_trace contact = touch.traces[i];
    if (!qa_session_actor_registry_is_live(host.actors, actor.id)) return;
    qa_trace_result original = contact.source;
    if (original.kind == NULL || strcmp(original.kind, "q2") != 0) qa_range_error("Rerelease contact lost its source trace");
    qa_bsp_plane selected = scene_plane(contact.plane);
    qa_trace_result result = original;
    result.source_plane = selected;
    result.surface = contact.has_surface ? contact.surface_ref : NULL;
    if (result.contact.has_plane) result.contact.plane = selected.plane;
    qa_actor_id other;
    if (!host.hit_actor(host.ctx, result, &other) || !qa_session_actor_registry_is_live(host.actors, other)) continue;
    read = qa_shared_body_table_read(host.bodies, actor.id);
    if (!read.has_value) return;
    if (selected.plane.normal.z > move->numeric.store(move->numeric.ctx, 0.7)) {
      static qa_actor_id ground_storage; ground_storage = other;
      qa_body_state b = read.value; b.ground = &ground_storage;
      qa_shared_body_table_write(host.bodies, actor, b);
    }
    host.impact(host.ctx, result);
    if (!qa_session_actor_registry_is_live(host.actors, actor.id)) return;
    if (host.take_kill_velocity(host.ctx)) {
      read = qa_shared_body_table_read(host.bodies, actor.id);
      if (!read.has_value) return;
      qa_body_state b = read.value; b.velocity = (qa_vec3){0, 0, 0};
      qa_shared_body_table_write(host.bodies, actor, b);
    }
  }
}
