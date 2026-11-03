/* Port of src/content/q3/base/game/hitscan.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/hitscan.h"
#include <string.h>
#include "qa/content/q3/base/game/missile.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/js.h"
static int actor_equal(qa_actor_id a, qa_actor_id b) { return a.slot == b.slot && a.generation == b.generation && a.token == b.token && a.session.token == b.session.token; }
static int team_equal(const char *a, const char *b) { if (!a || !b) return a == b; return strcmp(a, b) == 0; }
bool qa_q3_accuracy_hit(bool team_game, qa_q3_accuracy_subject t, qa_q3_accuracy_subject a) {
  return t.damageable && !actor_equal(t.actor, a.actor) && t.player && a.player && t.health > 0 && (!team_game || !team_equal(t.team, a.team));
}
static qa_vec3 trace_normal(qa_q3_base_actor_trace_result t) { qa_vec3 z = {0, 0, 0}; return t.contact_is_plane ? t.contact_plane.normal : z; }
static double scaled_damage(double amount, qa_q3_bullet_attack *a) { return qa_qvm_float_to_int((float)((float)amount * a->quad)); }
static qa_vec3 reflected_end(qa_vec3 start, qa_vec3 point, qa_vec3 direction) {
  qa_vec3 incoming = qa_sub3(point, start);
  return qa_add3(point, qa_scale3(qa_normalize3(qa_add3(incoming, qa_scale3(direction, (float)(-2 * qa_dot3(incoming, direction))))), 8192));
}
void qa_q3_bullet_fire(qa_q3_bullet_host host, qa_actor_id shooter, qa_q3_bullet_attack *attack, double spread, double amount) {
  qa_q3_ballistic_attack ba = {attack->muzzle, attack->forward, attack->right, attack->up};
  qa_vec3 end = qa_q3_bullet_endpoint(ba, spread, host.random);
  bool has_pass = true; qa_actor_id pass = shooter;
  for (int count = 0; count < 10; count++) {
    qa_q3_base_actor_trace_result trace = host.trace(host.ctx, attack->muzzle, end, has_pass ? &pass : NULL);
    if (qa_js_to_int32(trace.surface_flags) & 0x10) return;
    if (trace.hit_kind != QA_Q3_BASE_HIT_NONE && host.impact) host.impact(host.ctx, trace.end);
    bool has_actor = trace.hit_kind == QA_Q3_BASE_HIT_ACTOR; qa_actor_id actor = trace.hit_actor;
    qa_q3_bullet_target target; bool has_t = has_actor && host.target(host.ctx, actor, &target);
    qa_vec3 point = qa_q3_snap_vector_towards(trace.end, attack->muzzle);
    bool flesh = has_t && target.damageable && target.player;
    qa_q3_bullet_event ev = {point, trace_normal(trace), has_actor, actor, flesh}; host.emit(host.ctx, ev);
    if (flesh && target.accuracy_eligible) host.credit_accuracy_hit(host.ctx);
    if (has_actor && has_t && target.damageable) {
      if (host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && target.player && target.invulnerable) {
        qa_q3_invuln_impact impact = host.invulnerability_impact(host.ctx, actor, attack->forward, point);
        if (impact.kind == QA_Q3_INVULN_HIT) {
          qa_vec3 incoming = qa_sub3(impact.impact_point, attack->muzzle);
          qa_vec3 reflection = qa_add3(incoming, qa_scale3(impact.bounce_direction, (float)(-2 * qa_dot3(incoming, impact.bounce_direction))));
          end = qa_add3(impact.impact_point, qa_scale3(qa_normalize3(reflection), 8192));
          attack->muzzle = impact.impact_point; has_pass = false;
        } else { attack->muzzle = point; has_pass = true; pass = actor; }
        continue;
      }
      host.damage(host.ctx, actor, attack->forward, point, scaled_damage(amount, attack));
    }
    break;
  }
}
bool qa_q3_gauntlet_attack(qa_q3_contact_host host, qa_actor_id shooter, qa_q3_bullet_attack *attack, bool quad_active) {
  qa_q3_base_actor_trace_result trace = host.trace(host.ctx, attack->muzzle, qa_add3(attack->muzzle, qa_scale3(attack->forward, 32)), &shooter);
  if ((qa_js_to_int32(trace.surface_flags) & 0x10) || trace.hit_kind != QA_Q3_BASE_HIT_ACTOR) return false;
  qa_q3_bullet_target target; if (!host.target(host.ctx, trace.hit_actor, &target) || !target.damageable) return false;
  if (host.impact) host.impact(host.ctx, trace.end);
  if (target.player) { qa_q3_contact_event ev = {QA_Q3_CONTACT_HIT, trace.end, trace_normal(trace), {0,0,0}, {0,0,0}, trace.hit_actor}; host.emit(host.ctx, ev); }
  if (quad_active) { qa_q3_contact_event ev = {QA_Q3_CONTACT_GAUNTLET_QUAD, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, trace.hit_actor}; host.emit(host.ctx, ev); }
  host.damage(host.ctx, trace.hit_actor, attack->forward, trace.end, scaled_damage(50, attack)); return true;
}
void qa_q3_lightning_fire(qa_q3_contact_host host, qa_actor_id shooter, qa_q3_bullet_attack *attack) {
  bool has_pass = true; qa_actor_id pass = shooter;
  for (int count = 0; count < 10; count++) {
    qa_q3_base_actor_trace_result trace = host.trace(host.ctx, attack->muzzle, qa_add3(attack->muzzle, qa_scale3(attack->forward, 768)), has_pass ? &pass : NULL);
    if (host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && count != 0) {
      qa_q3_contact_event ev = {QA_Q3_CONTACT_LIGHTNING_REFLECTION, {0,0,0}, {0,0,0}, attack->muzzle, qa_q3_snap_vector(trace.end), pass}; host.emit(host.ctx, ev);
    }
    if (trace.hit_kind == QA_Q3_BASE_HIT_NONE) return;
    if (!(qa_js_to_int32(trace.surface_flags) & 0x10) && host.impact) host.impact(host.ctx, trace.end);
    bool has_actor = trace.hit_kind == QA_Q3_BASE_HIT_ACTOR; qa_actor_id actor = trace.hit_actor;
    qa_q3_bullet_target target; bool has_t = has_actor && host.target(host.ctx, actor, &target);
    if (has_actor && has_t && target.damageable) {
      if (host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && target.player && target.invulnerable) {
        qa_q3_invuln_impact impact = host.invulnerability_impact(host.ctx, actor, attack->forward, trace.end);
        if (impact.kind == QA_Q3_INVULN_HIT) {
          qa_vec3 end = reflected_end(attack->muzzle, impact.impact_point, impact.bounce_direction);
          attack->muzzle = impact.impact_point; attack->forward = qa_normalize3(qa_sub3(end, impact.impact_point)); has_pass = false;
        } else { attack->muzzle = trace.end; has_pass = true; pass = actor; }
        continue;
      }
      host.damage(host.ctx, actor, attack->forward, trace.end, scaled_damage(8, attack));
    }
    qa_q3_bullet_target after; bool has_a = has_actor && host.target(host.ctx, actor, &after);
    if (has_actor && has_a && after.damageable && after.player) {
      qa_q3_contact_event ev = {QA_Q3_CONTACT_HIT, trace.end, trace_normal(trace), {0,0,0}, {0,0,0}, actor}; host.emit(host.ctx, ev);
      if (after.accuracy_eligible) host.credit_accuracy_hit(host.ctx);
    } else if (!(qa_js_to_int32(trace.surface_flags) & 0x10)) {
      qa_q3_contact_event ev = {QA_Q3_CONTACT_MISS, trace.end, trace_normal(trace), {0,0,0}, {0,0,0}, actor}; host.emit(host.ctx, ev);
    }
    break;
  }
}
static bool shotgun_pellet(qa_q3_shotgun_host host, qa_actor_id shooter, qa_q3_bullet_attack *attack, qa_vec3 start, qa_vec3 end) {
  bool has_pass = true; qa_actor_id pass = shooter;
  for (int count = 0; count < 10; count++) {
    qa_q3_base_actor_trace_result trace = host.trace(host.ctx, start, end, has_pass ? &pass : NULL);
    if (qa_js_to_int32(trace.surface_flags) & 0x10) return false;
    if (trace.hit_kind != QA_Q3_BASE_HIT_NONE && host.impact) host.impact(host.ctx, trace.end);
    if (trace.hit_kind != QA_Q3_BASE_HIT_ACTOR) return false;
    qa_actor_id actor = trace.hit_actor; qa_q3_bullet_target target;
    if (!host.target(host.ctx, actor, &target) || !target.damageable) return false;
    if (host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && target.player && target.invulnerable) {
      qa_q3_invuln_impact impact = host.invulnerability_impact(host.ctx, actor, attack->forward, trace.end);
      if (impact.kind == QA_Q3_INVULN_HIT) { end = reflected_end(start, impact.impact_point, impact.bounce_direction); start = impact.impact_point; has_pass = false; }
      else { start = trace.end; has_pass = true; pass = actor; }
      continue;
    }
    host.damage(host.ctx, actor, attack->forward, trace.end, scaled_damage(10, attack));
    qa_q3_bullet_target after; return host.target(host.ctx, actor, &after) && after.accuracy_eligible;
  }
  return false;
}
void qa_q3_shotgun_fire(qa_q3_shotgun_host host, qa_actor_id shooter, qa_q3_bullet_attack *attack) {
  qa_vec3 muzzle = attack->muzzle, direction = qa_q3_snap_vector(qa_scale3(attack->forward, 4096));
  host.begin(host.ctx, muzzle, direction); double seed = qa_game_random_rand(host.random) & 255; host.publish(host.ctx, seed);
  bool hit_client = false; qa_q3_shotgun_endpoints ends = qa_q3_shotgun_endpoints_compute(muzzle, direction, seed);
  for (int i = 0; i < QA_Q3_SHOTGUN_PELLET_COUNT; i++) {
    if (!host.alive(host.ctx)) break;
    if (shotgun_pellet(host, shooter, attack, muzzle, ends.ends[i]) && !hit_client) { hit_client = true; host.credit_accuracy_hit(host.ctx); }
  }
}
static void emit_trail(qa_q3_rail_host host, qa_q3_bullet_attack *attack, qa_vec3 point, qa_q3_rail_impact_kind kind, qa_vec3 normal) {
  qa_q3_rail_trail t = {qa_add3(qa_add3(attack->muzzle, qa_scale3(attack->right, 4)), qa_scale3(attack->up, -1)), point, kind, normal};
  host.trail(host.ctx, t);
}
double qa_q3_rail_fire(qa_q3_rail_host host, qa_actor_id shooter, qa_q3_bullet_attack *attack) {
  qa_vec3 end = qa_add3(attack->muzzle, qa_scale3(attack->forward, 8192));
  bool has_pass = true; qa_actor_id pass = shooter; double hits = 0; int penetrated = 0;
  void *restores[4]; int restore_count = 0; bool has_trace = false; qa_q3_base_actor_trace_result trace;
  memset(&trace, 0, sizeof trace);
  do {
    if (!host.alive(host.ctx)) break;
    trace = host.trace(host.ctx, attack->muzzle, end, has_pass ? &pass : NULL); has_trace = true;
    if (trace.hit_kind != QA_Q3_BASE_HIT_NONE && !(qa_js_to_int32(trace.surface_flags) & 0x10) && host.impact) host.impact(host.ctx, trace.end);
    if (trace.hit_kind != QA_Q3_BASE_HIT_ACTOR) break;
    qa_actor_id actor = trace.hit_actor; qa_q3_bullet_target target; bool has_t = host.target(host.ctx, actor, &target);
    if (has_t && target.damageable) {
      if (host.product == QA_Q3_BASE_PRODUCT_MISSIONPACK && target.player && target.invulnerable) {
        qa_q3_invuln_impact impact = host.invulnerability_impact(host.ctx, actor, attack->forward, trace.end);
        if (impact.kind == QA_Q3_INVULN_HIT) {
          end = reflected_end(attack->muzzle, impact.impact_point, impact.bounce_direction);
          trace.end = qa_q3_snap_vector_towards(trace.end, attack->muzzle);
          qa_vec3 z = {0, 0, 0}; emit_trail(host, attack, trace.end, QA_Q3_RAIL_NONE, z);
          attack->muzzle = impact.impact_point; has_pass = false;
        }
      } else {
        if (target.accuracy_eligible) hits++;
        host.damage(host.ctx, actor, attack->forward, trace.end, scaled_damage(100, attack));
      }
    }
    if (qa_js_to_int32(trace.contents) & 1) break;
    void *restore = host.unlink(host.ctx, actor); if (restore) restores[restore_count++] = restore;
    penetrated++;
  } while (penetrated < 4);
  for (int i = 0; i < restore_count; i++) host.restore(host.ctx, restores[i]);
  if (has_trace) {
    if (qa_js_to_int32(trace.surface_flags) & 0x10) { qa_vec3 z = {0,0,0}; emit_trail(host, attack, qa_q3_snap_vector_towards(trace.end, attack->muzzle), QA_Q3_RAIL_NONE, z); }
    else emit_trail(host, attack, qa_q3_snap_vector_towards(trace.end, attack->muzzle), QA_Q3_RAIL_SURFACE, trace_normal(trace));
  }
  return hits;
}
qa_q3_rail_statistics_result qa_q3_update_rail_statistics(qa_q3_rail_statistics state, double hits, double time) {
  qa_q3_rail_statistics_result r;
  if (hits == 0) { r.state = state; r.state.streak = 0; r.awarded = false; return r; }
  double streak = qa_int32(state.streak + hits); bool awarded = streak >= 2;
  r.state.streak = awarded ? qa_int32(streak - 2) : streak; r.state.hits = qa_int32(state.hits + 1);
  r.state.impressive_count = awarded ? qa_int32(state.impressive_count + 1) : state.impressive_count;
  r.state.reward_until = awarded ? qa_int32(time + 2000) : state.reward_until; r.awarded = awarded; return r;
}
