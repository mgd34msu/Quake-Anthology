/* Port of src/content/q1/missionpacks/world/rogue-hazards.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_hazards.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/foundation/support.h"
#include <stdlib.h>

typedef struct rh_ctx { qa_q1mp_game *game; } rh_ctx;
void qa_q1mp_rogue_earthquake(qa_q1mp_game *game, qa_actor_id actor, double intensity) {
  qa_owned_actor owner; qa_q1b_body body;
  if (!game->base->host->actors_resolve_owned(game->base->host->ctx, actor, &owner) || !qa_q1mp_body_read(game, actor, &body) || !body.ground.has) return;
  body.velocity = qa_q1mp_vadd(body.velocity, QA_Q1MP_V3(
    qa_q1mp_random(game)*intensity*2-intensity, qa_q1mp_random(game)*intensity*2-intensity, qa_q1mp_random(game)*intensity*2-intensity));
  game->base->host->bodies_write(game->base->host->ctx, owner, &body);
}
static void quake_stop(rh_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  if (g->world) qa_q1mp_world_number(g->world, "rogue:earthquake_active", 0);
  qa_q1mp_world_later(c->game, e, (e->spawnflags & 1) != 0 ? qa_q1mp_random(c->game) * e->wait : e->wait, "rogue:quake_start");
}
static void quake_rumble(rh_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  if (e->attack_finished < g->time) { quake_stop(c, g, e); return; }
  g->sound(g, e, "equake/rumble.wav", QA_Q1MP_CH("voice"), 0, 1);
  qa_q1mp_world_later(c->game, e, 1, "rogue:quake_rumble");
}
static void quake_stop_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { quake_stop(vctx, g, e); }
static void quake_rumble_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { quake_rumble(vctx, g, e); }
static void quake_start_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  rh_ctx *c = vctx;
  if (g->world) qa_q1mp_world_number(g->world, "rogue:earthquake_active", 1);
  e->attack_finished = g->time + ((e->spawnflags & 1) != 0 ? qa_q1mp_random(c->game) * e->delay : e->delay);
  quake_rumble(c, g, e);
}
static void earthquake_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  rh_ctx *c = vctx;
  if (e->delay == 0) e->delay = 20;
  if (e->wait == 0) e->wait = 60;
  if (qa_q1mp_number(e, "weapon") == 0) qa_q1mp_world_number(e, "weapon", 40);
  if (g->world) { qa_q1mp_world_number(g->world, "rogue:earthquake_active", 0); qa_q1mp_world_number(g->world, "rogue:earthquake_intensity", qa_q1mp_number(e,"weapon") * 0.5); }
  g->set_bounds(g, e, QA_Q1MP_POINT);
  qa_q1mp_world_later(c->game, e, 1, "rogue:quake_stop");
}
static void eq_field_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) { (void)vctx; (void)g; (void)o; (void)a; e->delay = e->delay == 0 ? 1 : 0; }
static void eq_field_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; rh_ctx *c = vctx;
  if (e->delay == 0) return;
  if (e->attack_finished < g->time) { g->sound(g, e, "equake/rumble.wav", QA_Q1MP_CH("voice"), 1, 1); e->attack_finished = g->time + 1; }
  if (g->is_player(g, QA_Q1MP_SOME(other))) qa_q1mp_rogue_earthquake(c->game, other, qa_q1mp_number(e, "weapon"));
}
static void trigger_eq_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  rh_ctx *c = vctx;
  double w = qa_q1mp_number(e, "weapon"); qa_q1mp_world_number(e, "weapon", (w != 0 ? w : 40) * 0.5);
  e->delay = e->targetname[0] == '\0' ? 1 : 0;
  e->touch = g->named.touch(g->named.ctx, e, "rogue:earthquake_field");
  if (e->targetname[0]) e->use = g->named.use(g->named.ctx, e, "rogue:earthquake_field");
  qa_q1mp_world_trigger(c->game, e);
}
static void eq_kill_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)vctx; (void)e; (void)hn; (void)n; (void)s;
  if (!g->is_player(g, QA_Q1MP_SOME(other))) return;
  qa_q1mp_actor *ents[256]; size_t total = g->entities(g, ents, 256);
  for (size_t i = 0; i < total; i++) if (qa_q1mp_streq(ents[i]->classname, "earthquake")) {
    if (g->world) qa_q1mp_world_number(g->world, "rogue:earthquake_active", 0);
    g->remove(g, ents[i]); break;
  }
}
static void eq_kill_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  rh_ctx *c = vctx; e->touch = g->named.touch(g->named.ctx, e, "rogue:earthquake_kill");
  qa_q1mp_world_trigger(c->game, e);
}
typedef struct saw_ctx { rh_ctx base; bool flying; } saw_ctx;
static saw_ctx saw_ctxs[2];
static void saw_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  saw_ctx *c = vctx; bool flying = c->flying; qa_q1mp_game *game = c->base.game;
  e->frame = flying ? 1 : 0;
  if (qa_q1mp_number(e, "pain_finished") < g->time) {
    if (game->emit_sound_event) game->emit_sound_event(game->ctx, e->actor.id, "buzz/buzz1.wav", QA_Q1MP_CH("voice"), 0.2, 1);
    qa_q1mp_world_number(e, "pain_finished", g->time + 1);
  }
  qa_q1b_body body = g->body(g, e);
  if (flying) {
    qa_q1mp_opt_actor goal_ref = qa_q1b_references_get(e, "goalentity");
    qa_q1mp_actor *goal = goal_ref.has ? g->entity(g, goal_ref) : NULL;
    if (!goal) goal = g->world;
    if (!goal) qa_range_error("Buzzsaw requires worldspawn");
    g->set_origin(g, e, qa_q1mp_vadd(body.origin, qa_q1mp_vscale(qa_q1mp_normalize(qa_q1mp_vsub(g->body(g, goal).origin, body.origin)), e->speed)));
  }
  qa_vec3 angles = body.angles; angles.x = body.angles.x - 60; qa_q1mp_set_angles_b(game, e, angles);
  e->angular_velocity.x = 60;
  qa_q1mp_world_later(game, e, 0.1, flying ? "rogue:saw_fly" : "rogue:saw_stand");
}
static void saw_start_impl(rh_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  e->touch = g->named.touch(g->named.ctx, e, "rogue:saw_touch"); e->use.has = false;
  qa_q1mp_actor *target = qa_q1mp_find0(c->game, e->target);
  qa_q1mp_opt_actor t = target ? QA_Q1MP_SOME(target->actor.id) : QA_Q1MP_NONE;
  qa_q1b_references_set(e, "goalentity", t); qa_q1b_references_set(e, "movetarget", t);
  qa_q1mp_world_later(c->game, e, 0.1, e->target[0] == '\0' ? "rogue:saw_stand" : "rogue:saw_fly");
}
static void saw_start_action(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { saw_start_impl(vctx, g, e); }
static void saw_start_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) { (void)o; (void)a; saw_start_impl(vctx, g, e); }
static void saw_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; rh_ctx *c = vctx;
  qa_q1mp_actor *oe = g->entity(g, QA_Q1MP_SOME(other));
  if (!g->is_player(g, QA_Q1MP_SOME(other)) && !(oe && (oe->movement_flags & 32) != 0)) return;
  if (e->attack_finished < g->time) { qa_q1mp_sound_ch(c->game, e, "buzz/buzz.wav", "weapon"); e->attack_finished = g->time + 2; }
  qa_q1b_game_damage(g, other, e->actor.id, QA_Q1MP_SOME(e->actor.id), qa_q1mp_number(e, "currentammo"));
  qa_q1mp_opt_actor goal_ref = qa_q1b_references_get(e, "goalentity");
  qa_q1mp_actor *goal = goal_ref.has ? g->entity(g, goal_ref) : NULL;
  if (!goal) goal = g->world;
  qa_q1b_body body; qa_owned_actor owner;
  bool has_b = qa_q1mp_body_read(c->game, other, &body), has_o = g->host->actors_resolve_owned(g->host->ctx, other, &owner);
  if (!goal || !has_b || !has_o) return;
  qa_vec3 direction = qa_q1mp_vscale(qa_q1mp_normalize(qa_q1mp_vsub(g->body(g, goal).origin, g->body(g, e).origin)), 200);
  qa_q1mp_effect_a(c->game, "meat-spray", g->body(g, e).origin, QA_Q1MP_SOME(other), 0);
  direction.z = 200; body.velocity = direction; g->host->bodies_write(g->host->ctx, owner, &body);
}
static void buzzsaw_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  rh_ctx *c = vctx;
  qa_q1mp_set_model(e, "progs/buzzsaw.mdl"); qa_q1b_actor_set_damageable(g, e, false);
  e->solid = "trigger"; e->movement = "fly";
  double yaw = g->body(g, e).angles.y;
  if (yaw == 0 || yaw == 180) g->set_bounds(g, e, (qa_bounds){{-18,0,-18},{18,0,18}});
  else if (yaw == 90 || yaw == 270) g->set_bounds(g, e, (qa_bounds){{0,-18,-18},{0,18,18}});
  else qa_range_error("Buzzsaw: Not at 90 degree angle!");
  if (e->speed == 0) e->speed = 10;
  if (qa_q1mp_number(e, "currentammo") == 0) qa_q1mp_world_number(e, "currentammo", 10);
  qa_q1mp_world_number(e, "pain_finished", g->time + qa_q1mp_random(c->game) * 2);
  if (e->targetname[0] == '\0') { qa_q1mp_world_later(c->game, e, 0.2, "rogue:saw_start"); return; }
  e->use = g->named.use(g->named.ctx, e, "rogue:saw_start");
}
static void trail_fire(rh_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  qa_q1mp_game *game = c->game;
  if (!qa_q1mp_streq(e->classname, "ltrail_end")) {
    qa_q1mp_sound_ch(game, e, "weapons/lhit.wav", "voice");
    qa_q1mp_actor *target = qa_q1mp_find0(game, e->target);
    if (!target) target = g->world;
    if (!target) qa_range_error("Lightning trail requires worldspawn");
    qa_vec3 start = g->body(g, e).origin, end = g->body(g, target).origin;
    if (game->emit_beam) game->emit_beam(game->ctx, "lightning2", e->actor.id, start, end);
    qa_vec3 side = {-(end.y-start.y)*16, -(end.y-start.y)*16, 0};
    qa_vec3 offsets[3] = {QA_Q1MP_ZERO, side, qa_q1mp_vscale(side,-1)}; qa_actor_id hit[8]; size_t hits = 0;
    for (int i = 0; i < 3; i++) {
      qa_q1b_trace trace = qa_q1mp_trace(game, qa_q1mp_vadd(start,offsets[i]), qa_q1mp_vadd(end,offsets[i]), QA_Q1MP_SOME(e->actor.id), true, false);
      if (!trace.actor.has) continue;
      bool seen = false;
      for (size_t j = 0; j < hits; j++) if (qa_same_actor(hit[j], trace.actor.id)) seen = true;
      if (seen) continue;
      if (hits < 8) hit[hits++] = trace.actor.id;
      if (qa_q1mp_can_damage_actor(game, trace.actor.id)) {
        if (game->emit_particles) game->emit_particles(game->ctx, trace.end, QA_Q1MP_V3(0,0,100), 225, qa_q1mp_number(e,"currentammo") * 4);
        qa_q1b_game_damage(g, trace.actor.id, e->actor.id, QA_Q1MP_SOME(e->actor.id), qa_q1mp_number(e,"currentammo"));
      }
    }
  }
  if (qa_q1mp_number(e, "items") < g->time) qa_q1mp_world_later(game, e, qa_q1mp_number(e,"frags"), "rogue:ltrail_chain");
  else qa_q1mp_world_later(game, e, 0.05, "rogue:ltrail_fire");
}
static void trail_fire_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { trail_fire(vctx, g, e); }
static void ltrail_chain_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx; g->use_targets(g, e, e->activator);
  e->think = g->named.action(g->named.ctx, e, "SUB_Null");
}
static void ltrail_use_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor other, qa_q1mp_opt_actor a) {
  rh_ctx *c = vctx; e->activator = a;
  if ((e->spawnflags & 1) != 0) {
    qa_q1mp_actor *oe = other.has ? g->entity(g, other) : NULL;
    if (!(oe && qa_q1mp_streq(oe->classname, "ltrail_end"))) {
      if ((e->spawnflags & 2) != 0) { e->spawnflags -= 2; return; }
      e->spawnflags += 2;
    } else if ((e->spawnflags & 2) == 0) return;
  }
  if (qa_q1mp_streq(e->classname, "ltrail_end")) { qa_q1mp_world_later(c->game, e, qa_q1mp_number(e,"frags"), "rogue:ltrail_chain"); return; }
  qa_q1mp_world_number(e, "items", g->time + qa_q1mp_number(e,"weapon"));
  trail_fire(c, g, e);
  if (qa_q1mp_streq(e->classname, "ltrail_start")) qa_q1mp_world_number(e, "ltrailLastUsed", g->time);
}
static void ltrail_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  rh_ctx *c = vctx;
  e->movement = "none"; e->solid = "bbox"; e->use = g->named.use(g->named.ctx, e, "rogue:ltrail_use");
  if (qa_q1mp_number(e,"currentammo") == 0) qa_q1mp_world_number(e, "currentammo", 25);
  if (qa_q1mp_number(e,"weapon") == 0) qa_q1mp_world_number(e, "weapon", 0.3);
  if (qa_q1mp_number(e,"frags") == 0) qa_q1mp_world_number(e, "frags", 0.3);
  if (qa_q1mp_streq(e->classname, "ltrail_start")) {
    qa_q1mp_world_number(e, "ltrailLastUsed", g->time);
    if ((e->spawnflags & 2) != 0) { qa_q1mp_world_number(e, "items", g->time + 99999999); qa_q1mp_world_later(c->game, e, 0.1, "rogue:ltrail_fire"); }
  }
}
void qa_q1mp_register_rogue_hazards(qa_q1mp_game *game) {
  rh_ctx *c = malloc(sizeof *c); c->game = game; qa_q1b_game *g = game->base;
  qa_q1b_handlers h = {0}; h.ctx = c; h.action = quake_stop_cb;
  g->named.register_named(g->named.ctx, "rogue:quake_stop", &h);
  qa_q1b_handlers hr = {0}; hr.ctx = c; hr.action = quake_rumble_cb;
  g->named.register_named(g->named.ctx, "rogue:quake_rumble", &hr);
  qa_q1b_handlers hs = {0}; hs.ctx = c; hs.action = quake_start_cb;
  g->named.register_named(g->named.ctx, "rogue:quake_start", &hs);
  g->register_spawn(g, "earthquake", earthquake_spawn, c);
  qa_q1b_handlers he = {0}; he.ctx = c; he.use = eq_field_use; he.touch = eq_field_touch;
  g->named.register_named(g->named.ctx, "rogue:earthquake_field", &he);
  g->register_spawn(g, "trigger_earthquake", trigger_eq_spawn, c);
  qa_q1b_handlers hk = {0}; hk.ctx = c; hk.touch = eq_kill_touch;
  g->named.register_named(g->named.ctx, "rogue:earthquake_kill", &hk);
  g->register_spawn(g, "trigger_earthquake_kill", eq_kill_spawn, c);
  for (int i = 0; i < 2; i++) {
    saw_ctxs[i].base.game = game; saw_ctxs[i].flying = i != 0;
    qa_q1b_handlers hw = {0}; hw.ctx = &saw_ctxs[i]; hw.action = saw_cb;
    g->named.register_named(g->named.ctx, i ? "rogue:saw_fly" : "rogue:saw_stand", &hw);
  }
  qa_q1b_handlers hss = {0}; hss.ctx = c; hss.action = saw_start_action; hss.use = saw_start_use;
  g->named.register_named(g->named.ctx, "rogue:saw_start", &hss);
  qa_q1b_handlers hst = {0}; hst.ctx = c; hst.touch = saw_touch;
  g->named.register_named(g->named.ctx, "rogue:saw_touch", &hst);
  g->register_spawn(g, "buzzsaw", buzzsaw_spawn, c);
  qa_q1b_handlers hc = {0}; hc.ctx = c; hc.action = ltrail_chain_cb;
  g->named.register_named(g->named.ctx, "rogue:ltrail_chain", &hc);
  qa_q1b_handlers hf2 = {0}; hf2.ctx = c; hf2.action = trail_fire_cb;
  g->named.register_named(g->named.ctx, "rogue:ltrail_fire", &hf2);
  qa_q1b_handlers hu = {0}; hu.ctx = c; hu.use = ltrail_use_cb;
  g->named.register_named(g->named.ctx, "rogue:ltrail_use", &hu);
  static const char *const trails[] = {"ltrail_start","ltrail_relay","ltrail_end"};
  for (size_t i = 0; i < 3; i++) g->register_spawn(g, trails[i], ltrail_spawn, c);
}
