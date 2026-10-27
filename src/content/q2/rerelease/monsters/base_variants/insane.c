/* Port of src/content/q2/rerelease/monsters/base-variants/insane.ts.
 * Rerelease m_insane.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/base_variants/insane.h"

#include <stdio.h>

#include "qa/content/q2/base/monsters/insane.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/insane.h"
#include "../rrm_internal.h"

static bool crawling(double frame) {
  const qa_q2rm_insane_frames *f = &qa_q2rm_insane_frames_value;
  return (frame >= f->crawl1 && frame <= f->crawl9) || (frame >= f->stand99 && frame <= f->stand160);
}
static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  const qa_q2rm_insane_frames *f = &qa_q2rm_insane_frames_value;
  if (q2rm_has(entity->spawnflags, 16) && entity->frame == f->cr_pain10) {
    q2rm_set_move(c, "insane_move_down");
    return;
  }
  bool crawl = q2rm_has(entity->spawnflags, 4) || crawling(entity->frame)
      || (entity->frame >= f->cr_pain2 && entity->frame <= f->cr_pain10);
  q2rm_set_move(c, crawl ? "insane_move_runcrawl"
      : q2rm_rnd(c->game) <= 0.5 ? "insane_move_run_normal" : "insane_move_run_insane");
}
static void dead_impl(qa_q2m_context *c) {
  if (!q2rm_has(c->entity->spawnflags, 8)) { qa_q2m_corpse(c); return; }
  c->entity->flags = q2rm_or(c->entity->flags, 1);
  c->entity->server_flags = q2rm_or(c->entity->server_flags, 2);
  c->state->corpse = true;
  c->game->link(c->game->ctx, c->entity);
  c->schedule(c->impl_ctx, c, 0.1, "monster_dead_think");
}
static void dead_fn(void *ctx, qa_q2m_context *c) { (void)ctx; dead_impl(c); }
static void vocalize(qa_q2m_context *c, bool scream) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  if (q2rm_has(entity->spawnflags, 64) || state->attack_finished >= q2rm_now(game)) return;
  static const double samples[8] = {1, 2, 3, 4, 6, 8, 9, 10};
  double sample = scream ? samples[qa_q2m_record_at(8, floor(q2rm_rnd(game) * 8))] : 7;
  char path[32];
  (void)snprintf(path, sizeof path, "insane/insane%d.wav", (int)sample);
  q2rm_snd4(c, path, 2, 1, 2);
  state->attack_finished = q2rm_now(game) + 1 + q2rm_rnd(game) * 2;
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->good_guy = true;
  if (q2rm_has(c->entity->spawnflags, 16)) c->state->stand_ground = true;
  if (q2rm_has(c->entity->spawnflags, 8)) {
    c->state->locomotion = QA_Q2M_LOCO_STATIONARY;
    c->entity->flags = q2rm_or(c->entity->flags, 2048 | 262144);
  }
}
static void after_spawn_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->skin = floor(q2rm_rnd(c->game) * 3);
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  const qa_q2rm_insane_frames *f = &qa_q2rm_insane_frames_value;
  if (q2rm_now(game) < state->pain_time) return;
  state->pain_time = q2rm_now(game) + 3;
  double variant = 1 + floor(q2rm_rnd(game) * 2);
  double hp = qa_q2m_health(game, &entity->actor.id);
  int band = hp < 25 ? 25 : hp < 50 ? 50 : hp < 75 ? 75 : 100;
  char path[40];
  (void)snprintf(path, sizeof path, "player/male/pain%d_%d.wav", band, (int)variant);
  q2rm_snd4(c, path, 2, 1, 2);
  q2rm_set_move(c, q2rm_has(entity->spawnflags, 8) ? "insane_move_struggle_cross"
      : crawling(entity->frame) || (entity->frame >= f->stand1 && entity->frame <= f->stand40)
          ? "insane_move_crawl_pain" : "insane_move_stand_pain");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  double damage = reaction.pain.damage;
  if (qa_q2rm_check_gib(c)) {
    q2rm_snd4(c, "misc/udeath.wav", 2, 1, 2);
    qa_q2m_gib_options plain = {false, false, false, false};
    qa_q2m_gib_options head = {true, false, false, false};
    for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", damage, plain);
    for (int i = 0; i < 4; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/head2/tris.md2", damage, head);
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  char path[40];
  (void)snprintf(path, sizeof path, "player/male/death%d.wav", 1 + (int)floor(q2rm_rnd(game) * 4));
  q2rm_snd4(c, path, 2, 1, 2);
  state->dead = true; state->can_take_damage = true;
  q2rm_set_damageable(game, entity, true);
  if (q2rm_has(entity->spawnflags, 8)) dead_impl(c);
  else q2rm_set_move(c, crawling(entity->frame) ? "insane_move_crawl_death" : "insane_move_stand_death");
}
static void shake_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2rm_has(c->entity->spawnflags, 64)) return;
  q2rm_snd4(c, "insane/insane5.wav", 2, 1, 2);
}
static void moan_fn(void *ctx, qa_q2m_context *c) { (void)ctx; vocalize(c, false); }
static void scream_fn(void *ctx, qa_q2m_context *c) { (void)ctx; vocalize(c, true); }
static const qa_q2m_named_handler extra_callbacks[] = {
  {"insane_run", {NULL, run_fn}},
  {"insane_dead", {NULL, dead_fn}},
  {"insane_shake", {NULL, shake_fn}},
  {"insane_moan", {NULL, moan_fn}},
  {"insane_scream", {NULL, scream_fn}},
};
static qa_q2m_definition insane_definition;
static bool insane_ready;
const qa_q2m_definition *qa_q2rm_get_rerelease_insane_definition(void) {
  if (insane_ready) return &insane_definition;
  insane_definition = qa_q2bm_insane_definition;
  insane_definition.moves = qa_q2rm_insane_moves;
  insane_definition.move_count = qa_q2rm_insane_move_count;
  insane_definition.run = (qa_q2m_handler){NULL, run_fn};
  insane_definition.initialize = (qa_q2m_handler){NULL, init_fn};
  insane_definition.after_spawn = (qa_q2m_handler){NULL, after_spawn_fn};
  insane_definition.reactions.pain = pain_fn;
  insane_definition.reactions.die = die_fn;
  insane_definition.callbacks = qa_q2rm_merge_callbacks(
      insane_definition.callbacks, insane_definition.callback_count,
      extra_callbacks, sizeof extra_callbacks / sizeof extra_callbacks[0], &insane_definition.callback_count);
  insane_ready = true;
  return &insane_definition;
}
