/* Port of src/content/q2/base/monsters/boss2.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/boss2.h"

#include "base_internal.h"
#include "qa/content/q2/base/monsters/boss_common.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/boss2.h"
#include "qa/content/q2/foundation/monsters/ai.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->state->stand_ground ? "boss2_move_stand" : "boss2_move_run");
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, qa_q2m_target_distance(c) <= 125 || q2bm_rnd(c->game) <= 0.6
                   ? "boss2_move_attack_pre_mg" : "boss2_move_attack_rocket");
}
static bool check_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  return qa_q2bm_boss_check_attack(c, true, false);
}
static void search_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2bm_rnd(c->game) < 0.5) q2bm_sound(c->game, c->entity, "bosshovr/bhvunqv1.wav", true, 2, true, 1, true, 0);
}
static void initialize_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->laser_immune = true;
  qa_q2bm_loop_sound(c, "bosshovr/bhvengn1.wav");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2bm_damaged_skin(c);
  if (q2bm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  q2bm_sound(c->game, c->entity,
             reaction.damage < 10 ? "bosshovr/bhvpain3.wav"
             : reaction.damage < 30 ? "bosshovr/bhvpain1.wav" : "bosshovr/bhvpain2.wav",
             true, 2, true, 1, true, 0);
  q2bm_set_move(c, reaction.damage < 30 ? "boss2_move_pain_light" : "boss2_move_pain_heavy");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  q2bm_sound(c->game, c->entity, "bosshovr/bhvdeth1.wav", true, 2, true, 1, true, 0);
  c->state->dead = true; c->state->can_take_damage = false; c->entity->count = 0;
  q2bm_set_traits_damage(c->game, c->entity, false);
  q2bm_set_move(c, "boss2_move_death");
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = {{-56, -56, 0}, {56, 56, 80}};
  qa_q2bm_finish_corpse_bounds(c, bounds);
}
static void reattack_mg_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->entity->has_enemy && qa_q2m_in_front(c, c->entity->enemy) && q2bm_rnd(c->game) <= 0.7
                   ? "boss2_move_attack_mg" : "boss2_move_attack_post_mg");
}
static void machine_gun_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  static const double flashes[2] = {73, 133};
  for (size_t i = 0; i < 2; i++) {
    qa_q2bm_shot_result aim;
    if (!qa_q2bm_shot(c, flashes[i], -0.2, &aim)) return;
    c->weapons.fire_bullet(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 6, 4, 300, 500, 0);
    qa_q2bm_muzzle(c, flashes[i], aim.direction, aim.start);
  }
}
static void rocket_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  static const double flashes[4] = {78, 79, 80, 81};
  for (size_t i = 0; i < 4; i++) {
    qa_q2bm_shot_result aim;
    if (!qa_q2bm_shot_default(c, flashes[i], &aim)) return;
    (void)c->weapons.fire_rocket(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 50, 500, 70, 50);
    qa_q2bm_muzzle(c, flashes[i], aim.direction, aim.start);
  }
}
#define CB(name, fn) {name, {NULL, fn}}
static const qa_q2m_named_handler boss2_callbacks[] = {
  CB("boss2_run", run_fn),
  {"boss2_attack_mg", {(void *)"boss2_move_attack_mg", qa_q2bm_move_handler_fn}},
  CB("BossExplode", qa_q2bm_boss_explode_handler),
  CB("boss2_dead", dead_fn), CB("boss2_reattack_mg", reattack_mg_fn),
  CB("Boss2MachineGun", machine_gun_fn), CB("Boss2Rocket", rocket_fn),
};
const qa_q2m_definition qa_q2bm_boss2_definition = {
  .classname = "monster_boss2", .kind = "boss2", .model = "models/monsters/boss2/tris.md2",
  .health = 2000, .gib_health = -200, .mass = 1000, .bounds = {{-56, -56, 0}, {56, 56, 80}}, .scale = 1,
  .has_locomotion = true, .locomotion = QA_Q2M_LOCO_FLY,
  .initial_move = "boss2_move_stand",
  .moves = qa_q2bm_boss2_moves, .move_count = 11,
  .callbacks = boss2_callbacks, .callback_count = sizeof boss2_callbacks / sizeof boss2_callbacks[0],
  .stand = {(void *)"boss2_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"boss2_move_walk", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn}, .attack = {NULL, attack_fn},
  .check_attack = check_attack_fn,
  .search = {NULL, search_fn},
  .initialize = {NULL, initialize_fn},
  .reactions = {NULL, pain_fn, die_fn},
};
