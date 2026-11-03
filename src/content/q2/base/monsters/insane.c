/* Port of src/content/q2/base/monsters/insane.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/insane.h"
#include "qa/network/common/js_number.h"
#include "qa/foundation/js.h"

#include <math.h>
#include <stdio.h>

#include "base_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/insane.h"
#include "qa/content/q2/foundation/monsters/ai.h"

static void stand_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  uint32_t flags = qa_js_to_uint32(c->entity->spawnflags);
  if ((flags & 8u) != 0) {
    c->state->stand_ground = true;
    q2bm_set_move(c, "insane_move_cross");
    return;
  }
  if ((flags & 20u) == 20u) { q2bm_set_move(c, "insane_move_down"); return; }
  q2bm_set_move(c, q2bm_rnd(c->game) < 0.5 ? "insane_move_stand_normal" : "insane_move_stand_insane");
}
static void walking(qa_q2m_context *c, bool running) {
  uint32_t flags = qa_js_to_uint32(c->entity->spawnflags);
  if ((flags & 16u) != 0 && c->entity->frame == qa_q2bm_insane_frames_value.cr_pain10) {
    q2bm_set_move(c, "insane_move_down");
    return;
  }
  if ((flags & 4u) != 0) {
    q2bm_set_move(c, running ? "insane_move_runcrawl" : "insane_move_crawl");
    return;
  }
  bool normal = q2bm_rnd(c->game) <= 0.5;
  q2bm_set_move(c, running ? (normal ? "insane_move_run_normal" : "insane_move_run_insane")
                           : (normal ? "insane_move_walk_normal" : "insane_move_walk_insane"));
}
static void walk_fn(void *ctx, qa_q2m_context *c) { (void)ctx; walking(c, false); }
static void run_fn(void *ctx, qa_q2m_context *c) { (void)ctx; walking(c, true); }
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if ((qa_js_to_uint32(c->entity->spawnflags) & 8u) == 0) { qa_q2bm_finish_corpse(c); return; }
  c->entity->flags = (double)(qa_js_to_int32(qa_js_to_uint32(c->entity->flags) | 1u));
  c->entity->server_flags = (double)(qa_js_to_int32(qa_js_to_uint32(c->entity->server_flags) | 2u));
  c->state->corpse = true;
  c->game->link(c->game->ctx, c->entity);
  c->game->cancel(c->game->ctx, c->entity);
}
static bool crawling(qa_q2m_context *c) {
  double frame = c->entity->frame;
  return (frame >= qa_q2bm_insane_frames_value.crawl1 && frame <= qa_q2bm_insane_frames_value.crawl9)
      || (frame >= qa_q2bm_insane_frames_value.stand99 && frame <= qa_q2bm_insane_frames_value.stand160);
}
static const qa_q2bm_sound_spec fist_spec = {"insane/insane11.wav", 2, 2};
static const qa_q2bm_sound_spec shake_spec = {"insane/insane5.wav", 2, 2};
static const qa_q2bm_sound_spec moan_spec = {"insane/insane7.wav", 2, 2};

static void initialize_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->good_guy = true;
  uint32_t flags = qa_js_to_uint32(c->entity->spawnflags);
  if ((flags & 16u) != 0) c->state->stand_ground = true;
  if ((flags & 8u) != 0) {
    c->state->locomotion = QA_Q2M_LOCO_FLY;
    c->entity->flags = (double)(qa_js_to_int32(qa_js_to_uint32(c->entity->flags) | (1u | 2048u)));
    qa_q2_body_changes changes;
    memset(&changes, 0, sizeof changes);
    changes.has_bounds = true;
    changes.bounds = (qa_bounds){{-16, 0, 0}, {16, 8, 32}};
    q2bm_move_body(c->game, c->entity, changes, true, false);
  }
}
static void after_spawn_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if ((qa_js_to_uint32(c->entity->spawnflags) & 8u) == 0)
    c->entity->skin = floor(q2bm_rnd(c->game) * 3);
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  if (q2bm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  double variant = 1 + floor(q2bm_rnd(c->game) * 2);
  double hp = qa_q2m_health(c->game, &c->entity->actor.id);
  double band = hp < 25 ? 25 : hp < 50 ? 50 : hp < 75 ? 75 : 100;
  char path[64];
  snprintf(path, sizeof path, "player/male/pain%s_%s.wav", QA_JS_NUMBER(band), QA_JS_NUMBER(variant));
  q2bm_sound(c->game, c->entity, path, true, 2, true, 1, true, 2);
  if (c->game->options.skill == 3) return;
  q2bm_set_move(c, (qa_js_to_uint32(c->entity->spawnflags) & 8u) != 0 ? "insane_move_struggle_cross"
                   : crawling(c) ? "insane_move_crawl_pain" : "insane_move_stand_pain");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  if (qa_q2bm_standard_gib(c, reaction, 2, 4, "models/objects/gibs/head2/tris.md2", 2) || c->state->dead)
    return;
  char path[64];
  snprintf(path, sizeof path, "player/male/death%s.wav", QA_JS_NUMBER(1 + floor(q2bm_rnd(c->game) * 4)));
  q2bm_sound(c->game, c->entity, path, true, 2, true, 1, true, 2);
  c->state->dead = true; c->state->can_take_damage = true;
  q2bm_set_traits_damage(c->game, c->entity, true);
  if ((qa_js_to_uint32(c->entity->spawnflags) & 8u) != 0) dead_fn(NULL, c);
  else q2bm_set_move(c, crawling(c) ? "insane_move_crawl_death" : "insane_move_stand_death");
}
static void scream_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  static const double variants[8] = {1, 2, 3, 4, 6, 8, 9, 10};
  double variant = variants[qa_q2m_record_at(8, floor(q2bm_rnd(c->game) * 8))];
  char path[64];
  snprintf(path, sizeof path, "insane/insane%s.wav", QA_JS_NUMBER(variant));
  q2bm_sound(c->game, c->entity, path, true, 2, true, 1, true, 2);
}
static void cross_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, q2bm_rnd(c->game) < 0.8 ? "insane_move_cross" : "insane_move_struggle_cross");
}
static void checkdown_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if ((qa_js_to_uint32(c->entity->spawnflags) & 32u) != 0 || q2bm_rnd(c->game) >= 0.3) return;
  q2bm_set_move(c, q2bm_rnd(c->game) < 0.5 ? "insane_move_uptodown" : "insane_move_jumpdown");
}
static void checkup_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if ((qa_js_to_uint32(c->entity->spawnflags) & 20u) != 20u && q2bm_rnd(c->game) < 0.5)
    q2bm_set_move(c, "insane_move_downtoup");
}
#define CB(name, fn) {name, {NULL, fn}}
static const qa_q2m_named_handler insane_callbacks[] = {
  CB("insane_stand", stand_fn), CB("insane_walk", walk_fn), CB("insane_run", run_fn), CB("insane_dead", dead_fn),
  {"insane_onground", {(void *)"insane_move_down", qa_q2bm_move_handler_fn}},
  {"insane_fist", {(void *)&fist_spec, qa_q2bm_sound_handler_fn}},
  {"insane_shake", {(void *)&shake_spec, qa_q2bm_sound_handler_fn}},
  {"insane_moan", {(void *)&moan_spec, qa_q2bm_sound_handler_fn}},
  CB("insane_scream", scream_fn), CB("insane_cross", cross_fn),
  CB("insane_checkdown", checkdown_fn), CB("insane_checkup", checkup_fn),
};
const qa_q2m_definition qa_q2bm_insane_definition = {
  .classname = "misc_insane", .kind = "insane", .model = "models/monsters/insane/tris.md2",
  .health = 100, .gib_health = -50, .mass = 300, .bounds = {{-16, -16, -24}, {16, 16, 32}}, .scale = 1,
  .initial_move = "insane_move_stand_normal",
  .moves = qa_q2bm_insane_moves, .move_count = 18,
  .callbacks = insane_callbacks, .callback_count = sizeof insane_callbacks / sizeof insane_callbacks[0],
  .stand = {NULL, stand_fn}, .walk = {NULL, walk_fn}, .run = {NULL, run_fn}, .attack = {NULL, stand_fn},
  .has_ranged_attack_flag = true, .has_ranged_attack = false,
  .initialize = {NULL, initialize_fn}, .after_spawn = {NULL, after_spawn_fn},
  .reactions = {NULL, pain_fn, die_fn},
};
