/* Port of src/content/q2/base/monsters/flipper.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/flipper.h"

#include <math.h>

#include "base_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/flipper.h"

static const qa_q2bm_sound_spec sight_spec = {"flipper/flpsght1.wav", 2, 1};
static const qa_q2bm_sound_spec preattack_spec = {"flipper/flpatck1.wav", 1, 1};

static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2bm_damaged_skin(c);
  if (q2bm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  if (c->game->options.skill == 3) return;
  bool first = ((int)(floor(q2bm_rnd(c->game) * 2) + 1) % 2) == 0;
  q2bm_sound(c->game, c->entity, first ? "flipper/flppain1.wav" : "flipper/flppain2.wav",
             true, 2, false, 0, false, 0);
  q2bm_set_move(c, first ? "flipper_move_pain1" : "flipper_move_pain2");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  if (qa_q2bm_standard_gib(c, reaction, 2, 2, "models/objects/gibs/sm_meat/tris.md2", 1)) return;
  qa_q2bm_begin_death_default(c, reaction, "flipper/flpdeth1.wav", "flipper_move_death");
}
static void bite_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  (void)c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, q2bm_v3(80, 0, 0), 5, 0);
}
static const qa_q2m_named_handler flipper_callbacks[] = {
  {"flipper_run", {(void *)"flipper_move_run_start", qa_q2bm_move_handler_fn}},
  {"flipper_run_loop", {(void *)"flipper_move_run_loop", qa_q2bm_move_handler_fn}},
  {"flipper_dead", {NULL, qa_q2bm_finish_corpse_handler}},
  {"flipper_preattack", {(void *)&preattack_spec, qa_q2bm_sound_handler_fn}},
  {"flipper_bite", {NULL, bite_fn}},
};
const qa_q2m_definition qa_q2bm_flipper_definition = {
  .classname = "monster_flipper", .kind = "flipper", .model = "models/monsters/flipper/tris.md2",
  .health = 50, .gib_health = -30, .mass = 100, .bounds = {{-16, -16, 0}, {16, 16, 32}}, .scale = 1,
  .has_locomotion = true, .locomotion = QA_Q2M_LOCO_SWIM,
  .has_ranged_attack_flag = true, .has_ranged_attack = false,
  .initial_move = "flipper_move_stand",
  .moves = qa_q2bm_flipper_moves, .move_count = 9,
  .callbacks = flipper_callbacks, .callback_count = sizeof flipper_callbacks / sizeof flipper_callbacks[0],
  .stand = {(void *)"flipper_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"flipper_move_walk", qa_q2bm_move_handler_fn},
  .run = {(void *)"flipper_move_start_run", qa_q2bm_move_handler_fn},
  .attack = {(void *)"flipper_move_attack", qa_q2bm_move_handler_fn},
  .melee = {(void *)"flipper_move_attack", qa_q2bm_move_handler_fn},
  .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
  .reactions = {NULL, pain_fn, die_fn},
};
