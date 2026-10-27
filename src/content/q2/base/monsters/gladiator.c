/* Port of src/content/q2/base/monsters/gladiator.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/gladiator.h"

#include <math.h>

#include "base_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/gladiator.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->state->stand_ground ? "gladiator_move_stand" : "gladiator_move_run");
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2m_target_distance(c) <= 112) return;
  qa_vec3 eye;
  if (!qa_q2m_enemy_eye(c, &eye)) return;
  c->state->blind_fire_target = eye;
  q2bm_sound(c->game, c->entity, "gladiator/railgun.wav", true, 1, false, 0, false, 0);
  q2bm_set_move(c, "gladiator_move_attack_gun");
}
static const qa_q2bm_sound_spec sight_spec = {"gladiator/sight.wav", 2, 1};
static const qa_q2bm_sound_spec idle_spec = {"gladiator/gldidle1.wav", 2, 2};
static const qa_q2bm_sound_spec search_spec = {"gladiator/gldsrch1.wav", 2, 2};
static const qa_q2bm_sound_spec cleaver_spec = {"gladiator/melee1.wav", 1, 1};

static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2bm_damaged_skin(c);
  bool airborne = q2bm_body(c->game, c->entity).velocity.z > 100;
  if (q2bm_now(c->game) < c->state->pain_time) {
    if (airborne && strcmp(c->state->move->name, "gladiator_move_pain") == 0)
      q2bm_set_move(c, "gladiator_move_pain_air");
    return;
  }
  c->state->pain_time = q2bm_now(c->game) + 3;
  q2bm_sound(c->game, c->entity, q2bm_rnd(c->game) < 0.5 ? "gladiator/pain.wav" : "gladiator/gldpain2.wav",
             true, 2, false, 0, false, 0);
  if (c->game->options.skill == 3) return;
  q2bm_set_move(c, airborne ? "gladiator_move_pain_air" : "gladiator_move_pain");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2bm_begin_death_default(c, reaction, "gladiator/glddeth2.wav", "gladiator_move_death");
}
static void melee_cb_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  bool hit = c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game,
                                 q2bm_v3(80, q2bm_body(c->game, c->entity).bounds.min.x, -4),
                                 20 + floor(q2bm_rnd(c->game) * 5), 300);
  q2bm_sound(c->game, c->entity, hit ? "gladiator/melee2.wav" : "gladiator/melee3.wav",
             true, 0, false, 0, false, 0);
}
static void gun_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(c->game->options.edition, 61));
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(c->state->blind_fire_target, start));
  c->weapons.fire_rail(c->weapons.ctx, c->entity, c->game, start, direction, 50, 100);
  qa_q2bm_muzzle(c, 61, direction, start);
}
#define CB(name, fn) {name, {NULL, fn}}
static const qa_q2m_named_handler gladiator_callbacks[] = {
  CB("gladiator_run", run_fn), CB("gladiator_dead", qa_q2bm_finish_corpse_handler),
  {"gladiator_cleaver_swing", {(void *)&cleaver_spec, qa_q2bm_sound_handler_fn}},
  CB("GaldiatorMelee", melee_cb_fn), CB("GladiatorGun", gun_fn),
};
const qa_q2m_definition qa_q2bm_gladiator_definition = {
  .classname = "monster_gladiator", .kind = "gladiator", .model = "models/monsters/gladiatr/tris.md2",
  .health = 400, .gib_health = -175, .mass = 400, .bounds = {{-32, -32, -24}, {32, 32, 64}}, .scale = 1,
  .initial_move = "gladiator_move_stand",
  .moves = qa_q2bm_gladiator_moves, .move_count = 8,
  .callbacks = gladiator_callbacks, .callback_count = sizeof gladiator_callbacks / sizeof gladiator_callbacks[0],
  .stand = {(void *)"gladiator_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"gladiator_move_walk", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn},
  .melee = {(void *)"gladiator_move_attack_melee", qa_q2bm_move_handler_fn},
  .attack = {NULL, attack_fn},
  .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
  .idle = {(void *)&idle_spec, qa_q2bm_sound_handler_fn},
  .search = {(void *)&search_spec, qa_q2bm_sound_handler_fn},
  .reactions = {NULL, pain_fn, die_fn},
};
