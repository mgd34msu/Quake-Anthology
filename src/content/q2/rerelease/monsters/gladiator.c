/* Port of src/content/q2/rerelease/monsters/gladiator.ts.
 * Rerelease m_gladiator.cpp, including the Xatrix plasma variant.
 * ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/gladiator.h"

#include <stdlib.h>

#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/gladiator.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/gladiator.h"
#include "rrm_internal.h"

static void plasma(qa_q2mpm_weapons *weapons, qa_q2m_context *c) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_vec3 eye;
  if (!qa_q2m_enemy_eye(c, &eye)) return;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, 61));
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(entity->pos1, start));
  bool later = entity->frame > qa_q2rm_gladiator_frames_value.attack3;
  (void)weapons->fire_plasma(weapons->ctx, entity, game, start, direction,
                             later ? 17 : 35, 725, later ? 22 : 45, later ? 22 : 45);
  entity->pos1 = eye;
}
static void plasma_fn(void *ctx, qa_q2m_context *c) { plasma(ctx, c); }
static void plasma_check_fn(void *ctx, qa_q2m_context *c) {
  if (c->game->options.skill == 3) plasma(ctx, c);
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_emit_loop(c->game, c->entity, "weapons/rg_hum.wav", 1, "start");
}
static void gladb_init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->style = 1;
  c->entity->skin = 2;
  qa_q2rm_init_power_armor(c, 250);
  q2rm_emit_loop(c->game, c->entity, "weapons/phaloop.wav", 1, "start");
}
static void restore_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2rm_bind_armor(c, 250);
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state enemy;
  qa_vec3 eye;
  if (!qa_q2m_enemy_body(c, &enemy) || !qa_q2m_enemy_eye(c, &eye)) return;
  qa_q2f_entity *entity = c->entity;
  bool close = qa_hypot3(qa_sub3_double(q2rm_body(c->game, entity).origin, enemy.origin)) <= 112
      && c->state->melee_time <= q2rm_now(c->game);
  if (close || !qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, 61))) return;
  entity->pos1 = eye;
  q2rm_snd(c, entity->style == 1 ? "weapons/plasshot.wav" : "gladiator/railgun.wav", 1);
  q2rm_set_move(c, entity->style == 1 ? "gladb_move_attack_gun" : "gladiator_move_attack_gun");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  bool airborne = q2rm_body(game, entity).velocity.z > 100;
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2
      ? q2rm_or(entity->skin, 1) : q2rm_and(entity->skin, ~1);
  if (q2rm_now(game) < state->pain_time) {
    if (airborne && strcmp(state->move->name, "gladiator_move_pain") == 0)
      q2rm_set_move(c, "gladiator_move_pain_air");
    return;
  }
  state->pain_time = q2rm_now(game) + 3;
  q2rm_snd(c, q2rm_rnd(game) < 0.5 ? "gladiator/pain.wav" : "gladiator/gldpain2.wav", 2);
  if (!qa_q2rm_reacts_to_pain(c)) return;
  q2rm_set_move(c, airborne ? "gladiator_move_pain_air" : "gladiator_move_pain");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  double damage = reaction.pain.damage;
  if (qa_q2rm_check_gib(c)) {
    q2rm_snd(c, "misc/udeath.wav", 2);
    entity->skin = trunc(entity->skin / 2);
    qa_q2m_gib_options plain = {false, false, false, false};
    qa_q2m_gib_options skinned = {false, false, true, false};
    for (int i = 0; i < 2; i++) {
      (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", damage, plain);
      (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
      (void)qa_q2m_throw_gib(entity, game, "models/monsters/gladiatr/gibs/thigh.md2", damage, skinned);
    }
    static const char *const parts[4] = {"larm", "rarm", "chest", "head"};
    static const char *const models[4] = {
      "models/monsters/gladiatr/gibs/larm.md2", "models/monsters/gladiatr/gibs/rarm.md2",
      "models/monsters/gladiatr/gibs/chest.md2", "models/monsters/gladiatr/gibs/head.md2"};
    for (int i = 0; i < 4; i++) {
      qa_q2m_gib_options options = {strcmp(parts[i], "head") == 0, false, true,
                                    strcmp(parts[i], "larm") == 0 || strcmp(parts[i], "rarm") == 0};
      (void)qa_q2m_throw_gib(entity, game, models[i], damage, options);
    }
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  q2rm_snd(c, "gladiator/glddeth2.wav", 4);
  if (q2rm_rnd(game) < 0.5) q2rm_snd(c, "gladiator/death.wav", 2);
  state->dead = true; state->can_take_damage = true;
  q2rm_set_damageable(game, entity, true);
  q2rm_set_move(c, "gladiator_move_death");
}
static void corpse_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_corpse(c); }
static void melee_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 aim = {80, q2rm_body(c->game, c->entity).bounds.min.x, -4};
  bool hit = c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, aim,
                                 20 + floor(q2rm_rnd(c->game) * 5), 300);
  if (!hit) c->state->melee_time = q2rm_now(c->game) + 1.5;
  q2rm_snd(c, hit ? "gladiator/melee2.wav" : "gladiator/melee3.wav", 0);
}
static void gun_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, 61));
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(c->entity->pos1, start));
  c->weapons.fire_rail(c->weapons.ctx, c->entity, c->game, start, direction, 50, 100);
  qa_q2rm_monster_flash(c, 61, start, direction);
}
static void shrink_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->server_flags = q2rm_or(c->entity->server_flags, 2);
  qa_bounds bounds = q2rm_body(c->game, c->entity).bounds;
  bounds.max.z = 0;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true; change.bounds = bounds;
  q2rm_move_body(c->game, c->entity, change, false, false);
}

size_t qa_q2rm_create_rerelease_gladiator_definitions(qa_q2mpm_weapons weapons, qa_q2m_definition out[2]) {
  qa_q2mpm_weapons *box = malloc(sizeof *box);
  if (!box) qa_q2f_error("Out of memory creating rerelease gladiator definitions");
  *box = weapons;
  const qa_q2m_named_handler extra[] = {
    {"gladiator_dead", {NULL, corpse_fn}},
    {"GladiatorMelee", {NULL, melee_fn}},
    {"GladiatorGun", {NULL, gun_fn}},
    {"gladbGun", {box, plasma_fn}},
    {"gladbGun_check", {box, plasma_check_fn}},
    {"gladiator_shrink", {NULL, shrink_fn}},
  };
  qa_q2m_definition gladiator = qa_q2bm_gladiator_definition;
  gladiator.bounds.min = q2rm_v3(-32, -32, -24);
  gladiator.bounds.max = q2rm_v3(32, 32, 42);
  gladiator.moves = qa_q2rm_gladiator_moves;
  gladiator.move_count = qa_q2rm_gladiator_move_count;
  gladiator.blocked_ctx = NULL;
  gladiator.blocked = qa_q2rm_blocked_check_platform_handler;
  gladiator.initialize = (qa_q2m_handler){NULL, init_fn};
  gladiator.attack = (qa_q2m_handler){NULL, attack_fn};
  gladiator.reactions.pain = pain_fn;
  gladiator.reactions.die = die_fn;
  gladiator.callbacks = qa_q2rm_merge_callbacks(gladiator.callbacks, gladiator.callback_count,
                                                extra, sizeof extra / sizeof extra[0], &gladiator.callback_count);
  out[0] = gladiator;
  out[1] = gladiator;
  out[1].classname = "monster_gladb";
  out[1].health = 250;
  out[1].mass = 350;
  out[1].initialize = (qa_q2m_handler){NULL, gladb_init_fn};
  out[1].restore = (qa_q2m_handler){NULL, restore_fn};
  return 2;
}
