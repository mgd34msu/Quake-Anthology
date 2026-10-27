/* Port of src/content/q1/missionpacks/world/rogue-time.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_time.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/foundation/support.h"
#include <stdlib.h>

typedef struct rt_ctx { qa_q1mp_game *game; } rt_ctx;
static qa_q1b_basis pure_vectors(qa_vec3 a) { return qa_math_angle_vectors_rule(a, QA_ANGLE_RULE_Q1); }
static void chunk(rt_ctx *c, qa_q1b_game *g, qa_q1b_actor *explosion, qa_q1b_actor *machine) {
  qa_q1b_basis basis = pure_vectors(g->body(g, machine).angles);
  qa_q1mp_actor *gib = g->create(g, "time_machine_gib");
  gib->solid = "none"; gib->movement = "toss"; qa_q1mp_set_model(gib, "progs/timegib.mdl");
  qa_q1b_body_patch p = {0};
  p.has_origin = true; p.origin = qa_q1mp_vsub(qa_q1mp_vadd(g->body(g, machine).origin, qa_q1mp_vscale(basis.forward, 84)), qa_q1mp_vscale(basis.up, 136));
  p.has_velocity = true; p.velocity = qa_q1mp_vscale(basis.up, -50);
  p.has_angles = true; p.angles = g->body(g, machine).angles; qa_q1mp_patch(c->game, gib, &p);
  gib->angular_velocity = QA_Q1MP_V3(300,300,300);
  g->sound(g, explosion, "weapons/r_exp3.wav", QA_Q1MP_CH("weapon"), 0, 1);
  qa_q1mp_effect_a(c->game, "explosion", g->body(g, gib).origin, QA_Q1MP_NONE, 0);
  machine->frame = 1; qa_q1mp_world_later(c->game, gib, 5, "SUB_Remove"); g->link(g, gib);
}
static void pain_impl(rt_ctx *c, qa_q1b_game *g, qa_q1b_actor *machine) {
  double health = g->health(g, machine->actor.id);
  if (health > 1100 && qa_q1mp_number(machine, "pain_finished") > g->time) return;
  if (qa_q1mp_random(c->game) < 0.4) {
    qa_q1mp_world_number(machine, "pain_finished", g->time + 2);
    double random = qa_q1mp_random(c->game);
    qa_q1b_basis basis = pure_vectors(g->body(g, machine).angles);
    qa_q1mp_actor *explosion = g->create(g, "time_machine_pain");
    explosion->owner = QA_Q1MP_SOME(machine->actor.id); qa_q1mp_set_target(explosion, machine->target);
    qa_vec3 offset = random < 0.33 ? qa_q1mp_vsub(qa_q1mp_vscale(basis.forward,80), qa_q1mp_vscale(basis.up,64))
      : random < 0.66 ? qa_q1mp_vsub(qa_q1mp_vscale(basis.right,80), qa_q1mp_vscale(basis.up,24))
      : qa_q1mp_vsub(qa_q1mp_vsub(qa_q1mp_vscale(basis.forward,64), qa_q1mp_vscale(basis.up,48)), qa_q1mp_vscale(basis.right,48));
    g->set_origin(g, explosion, qa_q1mp_vadd(g->body(g, machine).origin, offset));
    qa_q1mp_world_later(c->game, explosion, 0.2 + qa_q1mp_random(c->game) * 0.3, "rogue:time_boom");
  }
  if (health < 1000) {
    qa_q1mp_world_number(machine, "pain_finished", 0);
    machine->pain.has = false; machine->die.has = false;
    if (g->world) qa_q1mp_world_number(g->world, "rogue:cutscene_running", 1);
  }
}
void qa_q1mp_crash_time_machine(qa_q1mp_game *game) {
  qa_q1b_game *g = game->base;
  qa_q1mp_opt_actor ref = g->world ? qa_q1b_references_get(g->world, "rogue:theMachine") : QA_Q1MP_NONE;
  qa_q1mp_actor *machine = ref.has ? g->entity(g, ref) : NULL;
  if (!machine) qa_range_error("Rogue time_crash requires item_time_machine");
  qa_q1b_actor_set_damageable(g, machine, false);
  machine->movement = "fly"; machine->solid = "none"; machine->angular_velocity = QA_Q1MP_V3(15,0,5);
  qa_q1b_body_patch p = {0}; p.has_velocity = true; p.velocity = QA_Q1MP_V3(0,0,-50);
  p.has_bounds = true; p.bounds = QA_Q1MP_POINT; qa_q1mp_patch(game, machine, &p);
  qa_q1mp_world_later(game, machine, 0.1, "rogue:time_fall");
  qa_q1mp_set_target(machine, "timeramp");
  g->use_targets(g, machine, machine->activator);
}
static void pain_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor a, double d) { (void)a; (void)d; pain_impl(vctx, g, e); }
static void die_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor a) { (void)a; pain_impl(vctx, g, e); }
static void stop_shake_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)vctx; g->use_targets(g, e, e->activator); g->remove(g, e); }
static void boom_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  rt_ctx *c = vctx; g->use_targets(g, e, e->activator);
  qa_q1mp_actor *machine = g->entity(g, e->owner);
  if (!machine) qa_range_error("Time machine explosion lost its machine");
  if (g->health(g, machine->actor.id) < 1250 && machine->frame > 0) { if (machine->skin < 2) { machine->frame = 2; machine->skin = 2; } }
  else if (g->health(g, machine->actor.id) < 1500 && machine->frame == 0) { chunk(c, g, e, machine); machine->frame = 1; machine->skin = 1; }
  g->sound(g, e, "weapons/r_exp3.wav", QA_Q1MP_CH("weapon"), 0, 1);
  if (qa_q1mp_random(c->game) < 0.5) qa_q1mp_effect_a(c->game, "explosion", g->body(g, e).origin, QA_Q1MP_NONE, 0);
  else if (c->game->emit_colored_explosion) c->game->emit_colored_explosion(c->game->ctx, g->body(g, e).origin, 244, 3);
  qa_q1mp_set_model(e, "progs/s_explod.spr"); e->frame = 0; e->solid = "none"; e->movement = "none"; e->touch.has = false;
  qa_q1mp_set_velocity(c->game, e, QA_Q1MP_ZERO);
  qa_q1mp_world_later(c->game, e, 0.1, "base:explosion_frame"); g->link(g, e);
  qa_q1mp_actor *stop = g->create(g, "time_stop_shake"); qa_q1mp_set_target(stop, e->target);
  qa_q1mp_world_later(c->game, stop, 0.7, "rogue:time_stop_shake");
}
static void fall_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  rt_ctx *c = vctx; qa_q1b_body body = g->body(g, e);
  if (qa_q1mp_number(e, "pain_finished") == 0) {
    if (body.origin.z < -20) { qa_q1mp_effect_a(c->game, "lava-splash", qa_q1mp_vadd(body.origin, QA_Q1MP_V3(0,0,-80)), QA_Q1MP_NONE, 0); qa_q1mp_world_number(e, "pain_finished", 1); }
  } else if (qa_q1mp_random(c->game) < 0.3) qa_q1mp_effect_a(c->game, "explosion", body.origin, QA_Q1MP_NONE, 0);
  qa_vec3 v = body.velocity; v.z = body.velocity.z - 5; qa_q1mp_set_velocity(c->game, e, v);
  qa_q1mp_world_later(c->game, e, 0.1, "rogue:time_fall");
}
static void crash_action(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { (void)g; (void)e; qa_q1mp_crash_time_machine(((rt_ctx *)vctx)->game); }
static void crash_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) { (void)g; (void)e; (void)o; (void)a; qa_q1mp_crash_time_machine(((rt_ctx *)vctx)->game); }
static void crash_pain(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor a, double d) { (void)g; (void)e; (void)a; (void)d; qa_q1mp_crash_time_machine(((rt_ctx *)vctx)->game); }
static void crash_die(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor a) { (void)g; (void)e; (void)a; qa_q1mp_crash_time_machine(((rt_ctx *)vctx)->game); }
static void machine_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx;
  if (g->options.deathmatch != 0) { g->remove(g, e); return; }
  qa_q1mp_set_model(e, "progs/timemach.mdl"); e->solid = "slidebox"; e->movement = "fly"; e->max_health = 1600;
  qa_q1b_actor_set_damageable(g, e, true); e->movement_flags |= 32;
  e->angular_velocity = QA_Q1MP_V3(0,60,0); g->host->combat_set_health(g->host->ctx, e->actor, 1600);
  e->pain = g->named.pain(g->named.ctx, e, "rogue:time_pain");
  e->die = g->named.die(g->named.ctx, e, "rogue:time_pain");
  if (g->world) qa_q1b_references_set(g->world, "rogue:theMachine", QA_Q1MP_SOME(e->actor.id));
  g->set_bounds(g, e, (qa_bounds){{-64,-64,-144},{64,64,0}});
}
static void core_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx;
  if (g->options.deathmatch != 0) { g->remove(g, e); return; }
  qa_q1mp_set_model(e, "progs/timecore.mdl"); e->solid = "none"; e->movement = "fly"; e->angular_velocity = QA_Q1MP_V3(60,60,60);
}
void qa_q1mp_register_rogue_time(qa_q1mp_game *game) {
  rt_ctx *c = malloc(sizeof *c); c->game = game; qa_q1b_game *g = game->base;
  qa_q1b_handlers h = {0}; h.ctx = c; h.pain = pain_cb; h.die = die_cb;
  g->named.register_named(g->named.ctx, "rogue:time_pain", &h);
  qa_q1b_handlers hs = {0}; hs.ctx = c; hs.action = stop_shake_cb;
  g->named.register_named(g->named.ctx, "rogue:time_stop_shake", &hs);
  qa_q1b_handlers hb = {0}; hb.ctx = c; hb.action = boom_cb;
  g->named.register_named(g->named.ctx, "rogue:time_boom", &hb);
  qa_q1b_handlers hf = {0}; hf.ctx = c; hf.action = fall_cb;
  g->named.register_named(g->named.ctx, "rogue:time_fall", &hf);
  qa_q1b_handlers hc = {0}; hc.ctx = c; hc.action = crash_action; hc.use = crash_use; hc.pain = crash_pain; hc.die = crash_die;
  g->named.register_named(g->named.ctx, "rogue:time_crash", &hc);
  g->register_spawn(g, "item_time_machine", machine_spawn, c);
  g->register_spawn(g, "item_time_core", core_spawn, c);
}
