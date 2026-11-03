/* Port of src/content/q2/base/monsters/hover.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/hover.h"

#include <stdlib.h>

#include "base_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/hover.h"
#include "qa/content/q2/foundation/monsters/ai.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->state->stand_ground ? "hover_move_stand" : "hover_move_run");
}
static const qa_q2bm_sound_spec sight_spec = {"hover/hovsght1.wav", 2, 1};
static void search_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_sound(c->game, c->entity, q2bm_rnd(c->game) < 0.5 ? "hover/hovsrch1.wav" : "hover/hovsrch2.wav",
             true, 2, false, 0, false, 0);
}
static void initialize_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2bm_loop_sound(c, "hover/hovidle1.wav");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2bm_damaged_skin(c);
  if (q2bm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  if (c->game->options.skill == 3) return;
  bool light = reaction.damage <= 25;
  bool first = !light || q2bm_rnd(c->game) < 0.5;
  q2bm_sound(c->game, c->entity, first ? "hover/hovpain1.wav" : "hover/hovpain2.wav",
             true, 2, false, 0, false, 0);
  q2bm_set_move(c, !light ? "hover_move_pain1" : first ? "hover_move_pain3" : "hover_move_pain2");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  if (qa_q2bm_standard_gib(c, reaction, 2, 2, "models/objects/gibs/sm_meat/tris.md2", 1) || c->state->dead)
    return;
  qa_q2bm_begin_death_default(c, reaction,
                              q2bm_rnd(c->game) < 0.5 ? "hover/hovdeth1.wav" : "hover/hovdeth2.wav",
                              "hover_move_death1");
}
static void reattack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, qa_q2bm_alive_enemy(c) && qa_q2m_visible_enemy(c) && q2bm_rnd(c->game) <= 0.6
                   ? "hover_move_attack1" : "hover_move_end_attack");
}
static void fire_blaster_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, 62, &aim)) return;
  (void)c->weapons.fire_blaster(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 1, 1000,
                                c->entity->frame == qa_q2bm_hover_frames_value.attak104 ? 64 : 0,
                                false, false, 0);
  qa_q2bm_muzzle(c, 62, aim.direction, aim.start);
}
/* TS hoverDeadThink (source callback "q2:base/hover_deadthink"); the deadline is entity.timestamp. */
static void dead_think_fn(void *ctx, qa_q2_entity *self, qa_q2_game_services *services) {
  (void)ctx;
  if (services->body(services->ctx, self->actor).ground == NULL && q2bm_now(services) < self->timestamp) {
    qa_q2_think think = {NULL, dead_think_fn};
    services->schedule(services->ctx, self, 0.1, think);
    return;
  }
  q2bm_emit_effect(services, "q2:explosion1", services->body(services->ctx, self->actor).origin, q2bm_zero(), 1, 0);
  services->remove(services->ctx, self);
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  c->state->corpse = true;
  qa_q2_body_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_bounds = true;
  changes.bounds = (qa_bounds){{-16, -16, -24}, {16, 16, -8}};
  q2bm_move_body(game, entity, changes, false, false);
  game->motion(game->ctx, entity, "toss");
  entity->timestamp = q2bm_now(game) + 15;
  qa_q2_think think = {NULL, dead_think_fn};
  game->schedule(game->ctx, entity, 0.1, think);
}
static const qa_q2_named_think hover_thinks[] = {{"q2:base/hover_deadthink", {NULL, dead_think_fn}}};
#define CB(name, fn) {name, {NULL, fn}}
static const qa_q2m_named_handler hover_callbacks[] = {
  CB("hover_run", run_fn),
  {"hover_attack", {(void *)"hover_move_attack1", qa_q2bm_move_handler_fn}},
  CB("hover_reattack", reattack_fn), CB("hover_fire_blaster", fire_blaster_fn), CB("hover_dead", dead_fn),
};
const qa_q2m_definition qa_q2bm_hover_definition = {
  .has_source_callbacks = true, .source_callbacks = {.think = hover_thinks, .think_count = 1},
  .classname = "monster_hover", .kind = "hover", .model = "models/monsters/hover/tris.md2",
  .health = 240, .gib_health = -100, .mass = 150, .bounds = {{-24, -24, -24}, {24, 24, 32}}, .scale = 1,
  .has_locomotion = true, .locomotion = QA_Q2M_LOCO_FLY,
  .initial_move = "hover_move_stand",
  .moves = qa_q2bm_hover_moves, .move_count = 16,
  .callbacks = hover_callbacks, .callback_count = sizeof hover_callbacks / sizeof hover_callbacks[0],
  .stand = {(void *)"hover_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"hover_move_walk", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn},
  .attack = {(void *)"hover_move_start_attack", qa_q2bm_move_handler_fn},
  .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
  .search = {NULL, search_fn},
  .initialize = {NULL, initialize_fn},
  .reactions = {NULL, pain_fn, die_fn},
};
