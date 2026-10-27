/* Port of src/content/q2/base/monsters/jorg.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/jorg.h"

#include "base_internal.h"
#include "qa/content/q2/base/monsters/boss_common.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/makron.h"
#include "qa/content/q2/base/monsters/tables/boss31.h"
#include "qa/content/q2/foundation/monsters/ai.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->state->stand_ground ? "jorg_move_stand" : "jorg_move_run");
}
static void initialize_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->model2 = "models/monsters/boss3/jorg/tris.md2";
}
static bool check_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  return qa_q2bm_boss_check_attack_default(c);
}
static void search_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double r = q2bm_rnd(c->game);
  q2bm_sound(c->game, c->entity, r <= 0.3 ? "boss3/bs3srch1.wav" : r <= 0.6 ? "boss3/bs3srch2.wav" : "boss3/bs3srch3.wav",
             true, 2, false, 0, false, 0);
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2bm_rnd(c->game) <= 0.75) {
    q2bm_sound(c->game, c->entity, "boss3/bs3atck1.wav", true, 2, false, 0, false, 0);
    qa_q2bm_loop_sound(c, "boss3/w_loop.wav");
    q2bm_set_move(c, "jorg_move_start_attack1");
    return;
  }
  q2bm_sound(c->game, c->entity, "boss3/bs3atck2.wav", true, 2, false, 0, false, 0);
  q2bm_set_move(c, "jorg_move_attack2");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2bm_damaged_skin(c);
  qa_q2bm_stop_loop(c);
  if (q2bm_now(c->game) < c->state->pain_time
      || (reaction.damage <= 40 && q2bm_rnd(c->game) <= 0.6))
    return;
  double frame = c->entity->frame;
  if (frame >= qa_q2bm_boss31_frames_value.attak101 && frame <= qa_q2bm_boss31_frames_value.attak108
      && q2bm_rnd(c->game) <= 0.005)
    return;
  if (frame >= qa_q2bm_boss31_frames_value.attak109 && frame <= qa_q2bm_boss31_frames_value.attak114
      && q2bm_rnd(c->game) <= 0.00005)
    return;
  if (frame >= qa_q2bm_boss31_frames_value.attak201 && frame <= qa_q2bm_boss31_frames_value.attak208
      && q2bm_rnd(c->game) <= 0.005)
    return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  if (c->game->options.skill == 3) return;
  if (reaction.damage > 100 && q2bm_rnd(c->game) > 0.3) return;
  q2bm_sound(c->game, c->entity,
             reaction.damage <= 50 ? "boss3/bs3pain1.wav"
             : reaction.damage <= 100 ? "boss3/bs3pain2.wav" : "boss3/bs3pain3.wav",
             true, 2, false, 0, false, 0);
  q2bm_set_move(c, reaction.damage <= 50 ? "jorg_move_pain1"
                   : reaction.damage <= 100 ? "jorg_move_pain2" : "jorg_move_pain3");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  q2bm_sound(c->game, c->entity, "boss3/bs3deth1.wav", true, 2, false, 0, false, 0);
  qa_q2bm_stop_loop(c);
  c->state->dead = true; c->state->can_take_damage = false; c->entity->count = 0;
  q2bm_set_traits_damage(c->game, c->entity, false);
  q2bm_set_move(c, "jorg_move_death");
}
static const qa_q2bm_sound_spec idle_spec = {"boss3/bs3idle1.wav", 2, 1};
static const qa_q2bm_sound_spec step_left_spec = {"boss3/step1.wav", 4, 1};
static const qa_q2bm_sound_spec step_right_spec = {"boss3/step2.wav", 4, 1};
static const qa_q2bm_sound_spec death_hit_spec = {"boss3/d_hit.wav", 4, 1};
static void dead_fn(void *ctx, qa_q2m_context *c) { (void)ctx; (void)c; }
static void toss_fn(void *ctx, qa_q2m_context *c) { qa_q2bm_makron_toss(c, ctx); }
static void reattack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2m_visible_enemy(c) && q2bm_rnd(c->game) < 0.9) {
    q2bm_set_move(c, "jorg_move_attack1");
    return;
  }
  qa_q2bm_stop_loop(c);
  q2bm_set_move(c, "jorg_move_end_attack1");
}
static void bfg_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, 132, &aim)) return;
  q2bm_sound(c->game, c->entity, "boss3/bs3atck2.wav", true, 2, false, 0, false, 0);
  (void)c->weapons.fire_bfg(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 50, 300, 200);
  qa_q2bm_muzzle(c, 132, aim.direction, aim.start);
}
static void firebullet_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  static const double flashes[2] = {120, 126};
  for (size_t i = 0; i < 2; i++) {
    qa_q2bm_shot_result aim;
    if (!qa_q2bm_shot(c, flashes[i], -0.2, &aim)) return;
    c->weapons.fire_bullet(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 6, 4, 300, 500, 0);
    qa_q2bm_muzzle(c, flashes[i], aim.direction, aim.start);
  }
}
void qa_q2bm_create_jorg_definition(qa_q2bm_jorg_definition *out, qa_q2m_monsters *monsters) {
  qa_q2m_named_handler callbacks[12] = {
    {"jorg_run", {NULL, run_fn}},
    {"jorg_attack1", {(void *)"jorg_move_attack1", qa_q2bm_move_handler_fn}},
    {"BossExplode", {NULL, qa_q2bm_boss_explode_handler}},
    {"jorg_idle", {(void *)&idle_spec, qa_q2bm_sound_handler_fn}},
    {"jorg_step_left", {(void *)&step_left_spec, qa_q2bm_sound_handler_fn}},
    {"jorg_step_right", {(void *)&step_right_spec, qa_q2bm_sound_handler_fn}},
    {"jorg_death_hit", {(void *)&death_hit_spec, qa_q2bm_sound_handler_fn}},
    {"jorg_dead", {NULL, dead_fn}},
    {"MakronToss", {monsters, toss_fn}},
    {"jorg_reattack1", {NULL, reattack_fn}},
    {"jorgBFG", {NULL, bfg_fn}},
    {"jorg_firebullet", {NULL, firebullet_fn}},
  };
  memcpy(out->callbacks, callbacks, sizeof callbacks);
  qa_q2m_definition d = {
    .classname = "monster_jorg", .kind = "jorg", .model = "models/monsters/boss3/rider/tris.md2",
    .health = 3000, .gib_health = -2000, .mass = 1000, .bounds = {{-80, -80, 0}, {80, 80, 140}}, .scale = 1,
    .initial_move = "jorg_move_stand",
    .moves = qa_q2bm_boss31_moves, .move_count = qa_q2bm_boss31_move_count,
    .callbacks = out->callbacks, .callback_count = 12,
    .stand = {(void *)"jorg_move_stand", qa_q2bm_move_handler_fn},
    .walk = {(void *)"jorg_move_walk", qa_q2bm_move_handler_fn},
    .run = {NULL, run_fn}, .attack = {NULL, attack_fn},
    .initialize = {NULL, initialize_fn}, .search = {NULL, search_fn},
    .check_attack = check_attack_fn,
    .reactions = {NULL, pain_fn, die_fn},
  };
  out->definition = d;
}
