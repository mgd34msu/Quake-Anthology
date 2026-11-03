/* Port of src/content/q2/base/monsters/berserk.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/berserk.h"

#include <math.h>

#include "base_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/berserk.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->state->stand_ground ? "berserk_move_stand" : "berserk_move_run1");
}
static void melee_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, q2bm_rnd(c->game) < 0.5 ? "berserk_move_attack_spike" : "berserk_move_attack_club");
}
static const qa_q2bm_sound_spec sight_spec = {"berserk/sight.wav", 2, 1};
static const qa_q2bm_sound_spec search_spec = {"berserk/bersrch1.wav", 2, 1};
static const qa_q2bm_sound_spec swing_spec = {"berserk/attack.wav", 1, 1};

static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2bm_damaged_skin(c);
  if (q2bm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  q2bm_sound(c->game, c->entity, "berserk/berpain2.wav", true, 2, false, 0, false, 0);
  if (c->game->options.skill == 3) return;
  q2bm_set_move(c, reaction.damage < 20 || q2bm_rnd(c->game) < 0.5 ? "berserk_move_pain1" : "berserk_move_pain2");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2bm_begin_death_default(c, reaction, "berserk/berdeth2.wav",
                              reaction.pain.damage >= 50 ? "berserk_move_death1" : "berserk_move_death2");
}
static void fidget_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->stand_ground || q2bm_rnd(c->game) > 0.15) return;
  q2bm_set_move(c, "berserk_move_stand_fidget");
  q2bm_sound(c->game, c->entity, "berserk/beridle1.wav", true, 1, true, 1, true, 2);
}
static void spike_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  (void)c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, q2bm_v3(80, 0, -24),
                            15 + floor(q2bm_rnd(c->game) * 6), 400);
}
static void club_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  void * value = c->weapons.ctx;
  qa_q2_entity * value2 = c->entity;
  qa_q2_game_services * value3 = c->game;
  qa_vec3 value4 = q2bm_v3(80, q2bm_body(c->game, c->entity).bounds.min.x, -4);
  double value5 = 5 + floor(q2bm_rnd(c->game) * 6);
  (void)c->weapons.fire_hit(value, value2, value3,
                            value4,
                            value5, 400);
}
static void strike_fn(void *ctx, qa_q2m_context *c) { (void)ctx; (void)c; }

#define CB(name, fn) {name, {NULL, fn}}
static const qa_q2m_named_handler berserk_callbacks[] = {
  {"berserk_stand", {(void *)"berserk_move_stand", qa_q2bm_move_handler_fn}}, CB("berserk_run", run_fn),
  CB("berserk_dead", qa_q2bm_finish_corpse_handler),
  CB("berserk_fidget", fidget_fn),
  {"berserk_swing", {(void *)&swing_spec, qa_q2bm_sound_handler_fn}},
  CB("berserk_attack_spike", spike_fn), CB("berserk_attack_club", club_fn),
  CB("berserk_strike", strike_fn),
};
const qa_q2m_definition qa_q2bm_berserk_definition = {
  .classname = "monster_berserk", .kind = "berserk", .model = "models/monsters/berserk/tris.md2",
  .health = 240, .gib_health = -60, .mass = 250, .bounds = {{-16, -16, -24}, {16, 16, 32}}, .scale = 1,
  .initial_move = "berserk_move_stand",
  .moves = qa_q2bm_berserk_moves, .move_count = 11,
  .callbacks = berserk_callbacks, .callback_count = sizeof berserk_callbacks / sizeof berserk_callbacks[0],
  .stand = {(void *)"berserk_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"berserk_move_walk", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn}, .attack = {NULL, melee_fn}, .melee = {NULL, melee_fn},
  .has_ranged_attack_flag = true, .has_ranged_attack = false,
  .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
  .search = {(void *)&search_spec, qa_q2bm_sound_handler_fn},
  .reactions = {NULL, pain_fn, die_fn},
};
