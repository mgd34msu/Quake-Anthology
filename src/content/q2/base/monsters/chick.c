/* Port of src/content/q2/base/monsters/chick.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/chick.h"

#include <math.h>

#include "base_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/chick.h"
#include "qa/content/q2/foundation/monsters/ai.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->stand_ground) { q2bm_set_move(c, "chick_move_stand"); return; }
  const char *name = c->state->move->name;
  q2bm_set_move(c, strcmp(name, "chick_move_walk") == 0 || strcmp(name, "chick_move_start_run") == 0
                ? "chick_move_run" : "chick_move_start_run");
}
static const qa_q2bm_sound_spec sight_spec = {"chick/chksght1.wav", 2, 1};
static const qa_q2bm_sound_spec preattack_spec = {"chick/chkatck1.wav", 2, 1};
static const qa_q2bm_sound_spec reload_spec = {"chick/chkatck5.wav", 2, 1};

static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2bm_damaged_skin(c);
  if (q2bm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  double r = q2bm_rnd(c->game);
  q2bm_sound(c->game, c->entity, r < 0.33 ? "chick/chkpain1.wav" : r < 0.66 ? "chick/chkpain2.wav" : "chick/chkpain3.wav",
             true, 2, false, 0, false, 0);
  if (c->game->options.skill == 3) return;
  q2bm_set_move(c, reaction.damage <= 10 ? "chick_move_pain1" : reaction.damage <= 25 ? "chick_move_pain2" : "chick_move_pain3");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  if (qa_q2bm_standard_gib_default(c, reaction) || c->state->dead) return;
  bool first = q2bm_rnd(c->game) < 0.5;
  qa_q2bm_begin_death_default(c, reaction, first ? "chick/chkdeth1.wav" : "chick/chkdeth2.wav",
                              first ? "chick_move_death1" : "chick_move_death2");
}
static void dodge_fn(void *ctx, qa_q2m_context *c, qa_actor_id attacker, double eta,
                     const qa_trace_result *trace, bool gravity) {
  (void)ctx; (void)eta; (void)trace; (void)gravity;
  if (q2bm_rnd(c->game) > 0.25) return;
  if (!c->entity->has_enemy) { c->entity->has_enemy = true; c->entity->enemy = attacker; }
  q2bm_set_move(c, "chick_move_duck");
}
static void moan_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_sound(c->game, c->entity, q2bm_rnd(c->game) < 0.5 ? "chick/chkidle1.wav" : "chick/chkidle2.wav",
             true, 2, true, 1, true, 2);
}
static void fidget_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!c->state->stand_ground && q2bm_rnd(c->game) <= 0.3) q2bm_set_move(c, "chick_move_fidget");
}
static void duck_down_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->ducked) return;
  qa_q2m_set_duck(c, true);
  c->state->pause_time = q2bm_now(c->game) + 1;
}
static void duck_hold_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->hold_frame = q2bm_now(c->game) < c->state->pause_time;
}
static void duck_up_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_set_duck(c, false); }
static void rocket_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, 57, &aim)) return;
  (void)c->weapons.fire_rocket(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 50, 500, 70, 50);
  qa_q2bm_muzzle(c, 57, aim.direction, aim.start);
}
static void slash_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_sound(c->game, c->entity, "chick/chkatck3.wav", true, 1, false, 0, false, 0);
  (void)c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game,
                            q2bm_v3(80, q2bm_body(c->game, c->entity).bounds.min.x, 10),
                            10 + floor(q2bm_rnd(c->game) * 6), 100);
}
static void rerocket_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, qa_q2bm_alive_enemy(c) && qa_q2m_target_distance(c) >= 80 && qa_q2m_visible_enemy(c)
                       && q2bm_rnd(c->game) <= 0.6
                   ? "chick_move_attack1" : "chick_move_end_attack1");
}
static void reslash_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, qa_q2bm_alive_enemy(c) && qa_q2m_target_distance(c) < 80 && q2bm_rnd(c->game) <= 0.9
                   ? "chick_move_slash" : "chick_move_end_slash");
}
#define CB(name, fn) {name, {NULL, fn}}
static const qa_q2m_named_handler chick_callbacks[] = {
  {"chick_stand", {(void *)"chick_move_stand", qa_q2bm_move_handler_fn}},
  CB("chick_run", run_fn), CB("chick_dead", qa_q2bm_finish_corpse_handler),
  CB("ChickMoan", moan_fn), CB("chick_fidget", fidget_fn),
  CB("chick_duck_down", duck_down_fn), CB("chick_duck_hold", duck_hold_fn), CB("chick_duck_up", duck_up_fn),
  {"Chick_PreAttack1", {(void *)&preattack_spec, qa_q2bm_sound_handler_fn}},
  {"ChickReload", {(void *)&reload_spec, qa_q2bm_sound_handler_fn}},
  CB("ChickRocket", rocket_fn), CB("ChickSlash", slash_fn),
  {"chick_attack1", {(void *)"chick_move_attack1", qa_q2bm_move_handler_fn}},
  {"chick_slash", {(void *)"chick_move_slash", qa_q2bm_move_handler_fn}},
  CB("chick_rerocket", rerocket_fn), CB("chick_reslash", reslash_fn),
};
const qa_q2m_definition qa_q2bm_chick_definition = {
  .classname = "monster_chick", .kind = "chick", .model = "models/monsters/bitch/tris.md2",
  .health = 175, .gib_health = -70, .mass = 200, .bounds = {{-16, -16, 0}, {16, 16, 56}}, .scale = 1,
  .initial_move = "chick_move_stand",
  .moves = qa_q2bm_chick_moves, .move_count = 17,
  .callbacks = chick_callbacks, .callback_count = sizeof chick_callbacks / sizeof chick_callbacks[0],
  .stand = {(void *)"chick_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"chick_move_walk", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn},
  .attack = {(void *)"chick_move_start_attack1", qa_q2bm_move_handler_fn},
  .melee = {(void *)"chick_move_start_slash", qa_q2bm_move_handler_fn},
  .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
  .reactions = {NULL, pain_fn, die_fn},
  .dodge = dodge_fn,
};
