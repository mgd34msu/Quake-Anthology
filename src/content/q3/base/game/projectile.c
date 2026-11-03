/* Port of src/content/q3/base/game/projectile.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/projectile.h"
#include "qa/content/q3/base/game/ballistics_math.h"
#include "qa/content/q3/base/game/missile.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/js.h"
qa_q3_launch_result qa_q3_launch_projectile(qa_vec3 start, qa_vec3 direction, double speed, bool gravity, double duration, double time) {
  qa_q3_launch_result r; r.expires = qa_int32(time + duration);
  r.trajectory.type = gravity ? QA_Q3_BASE_TR_GRAVITY : QA_Q3_BASE_TR_LINEAR; r.trajectory.time = qa_int32(time - 50);
  r.trajectory.duration = 0; r.trajectory.base = start; r.trajectory.delta = qa_q3_snap_vector(qa_scale3(direction, speed)); return r;
}
static qa_vec3 trace_normal(qa_q3_base_actor_trace_result t) { qa_vec3 z = {0, 0, 0}; return t.contact_is_plane ? t.contact_plane.normal : z; }
void qa_q3_bounce_projectile(qa_q3_projectile *p, qa_q3_projectile_host host, qa_q3_base_actor_trace_result trace) {
  double hit_time = qa_q3_missile_hit_time(host.previous_time, host.time, trace.fraction);
  qa_vec3 plane = trace_normal(trace); bool half = (qa_js_to_int32(p->flags) & 0x20) != 0;
  qa_vec3 delta = qa_q3_bounce_velocity(qa_q3_base_evaluate_trajectory_delta(p->trajectory, hit_time), plane, half);
  p->trajectory.delta = delta;
  if (half && plane.z > (float)0.2 && qa_length3(delta) < 40) { host.set_origin(host.ctx, trace.end); return; }
  qa_vec3 origin = qa_add3(host.origin(host.ctx), plane); host.move(host.ctx, origin, delta);
  p->trajectory.base = origin; p->trajectory.time = host.time;
}
void qa_q3_explode_projectile(qa_q3_projectile *p, qa_q3_projectile_host host) {
  qa_vec3 origin = qa_q3_snap_vector(qa_q3_base_evaluate_trajectory(p->trajectory, host.time));
  host.set_origin(host.ctx, origin);
  qa_q3_projectile_impact ev = {QA_Q3_IMPACT_IMPACT, {0, 0, 1}, false, {{"", NULL}, 0, 0, NULL}, false, 0};
  host.emit(host.ctx, ev); if (!host.live(host.ctx)) return; host.retain(host.ctx);
  if (p->splash != 0 && host.radius(host.ctx, origin, &p->actor.id)) host.accuracy(host.ctx);
  if (host.live(host.ctx)) host.link(host.ctx);
}
void qa_q3_impact_projectile(qa_q3_projectile *p, qa_q3_projectile_host host, qa_q3_base_actor_trace_result trace) {
  qa_actor_id actor = trace.hit_kind == QA_Q3_BASE_HIT_ACTOR ? trace.hit_actor : host.world_actor(host.ctx);
  qa_vec3 plane = trace_normal(trace); qa_q3_projectile_target target; bool has = host.target(host.ctx, actor, &target);
  if ((!has || !target.damageable) && (qa_js_to_int32(p->flags) & 0x30) != 0) {
    qa_q3_bounce_projectile(p, host, trace);
    qa_q3_projectile_impact ev = {QA_Q3_IMPACT_BOUNCE, plane, false, {{"", NULL}, 0, 0, NULL}, false, 0};
    host.emit(host.ctx, ev); return;
  }
  if (host.has_reflection && has && target.damageable && target.invulnerable && p->weapon != 12) {
    qa_q3_reflection_result effect = host.reflection_impact(host.ctx, actor, qa_normalize3(p->trajectory.delta), p->trajectory.base);
    if (!host.live(host.ctx)) return;
    if (effect.kind == QA_Q3_REFLECTION_HIT) {
      int half = qa_js_to_int32(p->flags) & 0x20; p->flags = qa_js_to_int32(p->flags) & ~0x20;
      qa_q3_base_actor_trace_result t2 = trace; t2.contact_is_plane = true; t2.contact_plane.normal = effect.bounce_direction; t2.contact_plane.distance = 0;
      qa_q3_bounce_projectile(p, host, t2); p->flags = qa_js_to_int32(p->flags) | half;
    }
    p->has_pass = true; p->pass = actor; return;
  }
  bool hit_client = false;
  if (has && target.damageable && p->direct != 0) {
    if (target.accuracy_eligible) { host.accuracy(host.ctx); hit_client = true; }
    qa_vec3 velocity = qa_q3_base_evaluate_trajectory_delta(p->trajectory, host.time);
    if (qa_length3(velocity) == 0) velocity = (qa_vec3){velocity.x, velocity.y, 1};
    host.damage(host.ctx, actor, velocity, p->damage_point); if (!host.live(host.ctx)) return;
  }
  if ((host.has_special && host.special_impact && host.special_impact(host.ctx, trace, actor)) || !host.live(host.ctx)) return;
  qa_q3_projectile_target current; bool has_cur = host.target(host.ctx, actor, &current);
  qa_q3_projectile_impact ev = {QA_Q3_IMPACT_IMPACT, plane, true, actor, has_cur && current.damageable && current.player, trace.surface_flags};
  host.emit(host.ctx, ev); if (!host.live(host.ctx)) return; host.retain(host.ctx);
  qa_vec3 origin = qa_q3_snap_vector_towards(trace.end, p->trajectory.base); host.set_origin(host.ctx, origin);
  if (p->splash != 0 && host.radius(host.ctx, origin, &actor) && !hit_client) host.accuracy(host.ctx);
  if (host.live(host.ctx)) host.link(host.ctx);
}
void qa_q3_step_projectile(qa_q3_projectile *p, qa_q3_projectile_host host) {
  if (!host.live(host.ctx)) return;
  if (host.phase(host.ctx) == QA_Q3_PHASE_EVENT) { if (qa_int32(host.time - host.event_time(host.ctx)) > 300) host.release(host.ctx); return; }
  if (qa_int32(host.time - host.event_time(host.ctx)) > 300 && host.clear_event) host.clear_event(host.ctx);
  if (host.phase(host.ctx) == QA_Q3_PHASE_ATTACHED) { host.think(host.ctx); return; }
  qa_vec3 origin = host.origin(host.ctx), destination = qa_q3_base_evaluate_trajectory(p->trajectory, host.time);
  const qa_actor_id *pass = p->has_pass ? &p->pass : NULL;
  qa_q3_base_actor_trace_result trace = host.trace(host.ctx, origin, destination, pass);
  if (trace.solidity != QA_Q3_BASE_SOLID_CLEAR) { trace = host.trace(host.ctx, origin, origin, pass); trace.fraction = 0; }
  else host.move(host.ctx, trace.end, qa_q3_base_evaluate_trajectory_delta(p->trajectory, host.time));
  host.link(host.ctx);
  if (trace.fraction != 1) {
    if ((qa_js_to_int32(trace.surface_flags) & 16) != 0) { if (host.has_special && host.special_no_impact) host.special_no_impact(host.ctx); if (host.live(host.ctx)) host.release(host.ctx); return; }
    qa_q3_impact_projectile(p, host, trace); if (!host.live(host.ctx) || host.phase(host.ctx) != QA_Q3_PHASE_FLIGHT) return;
  }
  if (host.has_special && host.special_after_move) host.special_after_move(host.ctx);
  if (!host.live(host.ctx)) return;
  host.moved(host.ctx); host.think(host.ctx);
}
