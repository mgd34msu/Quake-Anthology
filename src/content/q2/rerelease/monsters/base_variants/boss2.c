/* Port of src/content/q2/rerelease/monsters/base-variants/boss2.ts.
 * Rerelease m_boss2.cpp, including the N64 attacks. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/base_variants/boss2.h"

#include <stdlib.h>

#include "qa/content/q2/base/monsters/boss2.h"
#include "qa/content/q2/base/monsters/boss_common.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/foundation/monsters/perception.h"
#include "qa/content/q2/rerelease/monsters/boss.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/boss2.h"
#include "qa/content/q2/rerelease/monsters/tables/flashes.h"
#include "../rrm_internal.h"

static void attack_machinegun(qa_q2m_context *c) {
  q2rm_set_move(c, q2rm_has(c->entity->spawnflags, 8) ? "boss2_move_attack_hb" : "boss2_move_attack_mg");
}
static void attack_mg_fn(void *ctx, qa_q2m_context *c) { (void)ctx; attack_machinegun(c); }
typedef struct rocket_shot { double flash, lead, spread; bool lower; } rocket_shot;
static void fire_rocket(qa_q2m_context *c, bool predictive) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_vec3 right = qa_math_angle_vectors_rule(q2rm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).right;
  static const rocket_shot shots[4] = {
    {78, -0.1, 0.4, true}, {79, -0.05, 0.025, false}, {80, 0.05, -0.025, false}, {81, 0.1, -0.4, true}};
  for (int i = 0; i < 4; i++) {
    qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, shots[i].flash));
    qa_vec3 direction;
    if (predictive) {
      if (!qa_q2rm_predicted_direction(c, start, 750, false, shots[i].lead, &direction)) continue;
    } else {
      qa_vec3 aim_point = enemy.origin;
      aim_point.z -= shots[i].lower ? 15 : 0;
      direction = qa_normalize3_double_hypot(qa_add3_double(
          qa_normalize3_double_hypot(qa_sub3_double(aim_point, start)), qa_scale3_double(right, shots[i].spread)));
    }
    (void)c->weapons.fire_rocket(c->weapons.ctx, entity, game, start, direction, 50, predictive ? 750 : 500, 70, 50);
    qa_q2rm_monster_flash(c, shots[i].flash, start, direction);
  }
}
static void bullet(qa_q2m_context *c, double flash) {
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, flash));
  qa_vec3 direction;
  if (!qa_q2rm_predicted_direction(c, start, 0, true, -0.2, &direction)) return;
  c->weapons.fire_bullet(c->weapons.ctx, c->entity, c->game, start, direction, 6, 4, 900, 500, 0);
  qa_q2rm_monster_flash(c, flash, start, direction);
}
static void gib(qa_q2m_context *c) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  q2rm_emit_effect(game, "q2:explosion1-big", q2rm_body(game, entity).origin, q2rm_zero(), 1, 0);
  qa_q2bm_stop_loop(c);
  entity->skin = trunc(entity->skin / 2);
  entity->gravity_vector.z = -1;
  qa_q2m_gib_options plain = {false, false, false, false};
  qa_q2m_gib_options metal = {false, true, false, false};
  qa_q2m_gib_options skinned = {false, false, true, false};
  qa_q2m_gib_options skin_up = {false, false, true, true};
  qa_q2m_gib_options skin_metal_head = {true, true, true, false};
  for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", 500, plain);
  for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_metal/tris.md2", 500, metal);
  static const char *const body_parts[3] = {
    "models/monsters/boss2/gibs/chest.md2", "models/monsters/boss2/gibs/engine.md2",
    "models/monsters/boss2/gibs/spine.md2"};
  for (int i = 0; i < 3; i++) (void)qa_q2m_throw_gib(entity, game, body_parts[i], 500, skinned);
  static const char *const up_parts[6] = {
    "models/monsters/boss2/gibs/chaingun.md2", "models/monsters/boss2/gibs/chaingun.md2",
    "models/monsters/boss2/gibs/cpu.md2", "models/monsters/boss2/gibs/rocket.md2",
    "models/monsters/boss2/gibs/wing.md2", "models/monsters/boss2/gibs/wing.md2"};
  for (int i = 0; i < 6; i++) (void)qa_q2m_throw_gib(entity, game, up_parts[i], 500, skin_up);
  static const double factors[3] = {1, 2, 1.35};
  static const char *const arms[2] = {"models/monsters/boss2/gibs/larm.md2", "models/monsters/boss2/gibs/rarm.md2"};
  for (int f = 0; f < 3; f++)
    for (int p = 0; p < 2; p++) {
      qa_q2_entity *piece = qa_q2m_throw_gib(entity, game, arms[p], 500, skin_up);
      if (piece) {
        piece->scale = (entity->scale != 0 ? entity->scale : 1) * factors[f];
        game->show(game->ctx, piece);
      }
    }
  (void)qa_q2m_throw_gib(entity, game, "models/monsters/boss2/gibs/head.md2", 500, skin_metal_head);
  state->dead = true; state->gibbed = true;
}
static bool check_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_attack_chance_profile profile = {0.4, 0.8, 0.8, 0.8, 0, 0};
  return qa_q2m_default_check_attack(c, &profile);
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->ignore_shots = true;
  if (qa_q2bm_boss2_definition.initialize.fn)
    qa_q2bm_boss2_definition.initialize.fn(qa_q2bm_boss2_definition.initialize.ctx, c);
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  bool gun = qa_q2m_target_distance(c) <= 125 || q2rm_rnd(c->game) <= 0.6;
  bool n64 = q2rm_has(c->entity->spawnflags, 8);
  q2rm_set_move(c, gun ? (n64 ? "boss2_move_attack_hb" : "boss2_move_attack_pre_mg")
                       : (n64 ? "boss2_move_attack_rocket2" : "boss2_move_attack_rocket"));
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2 ? 1 : 0;
  if (q2rm_now(game) < state->pain_time) return;
  state->pain_time = q2rm_now(game) + 3;
  q2rm_sound(game, entity, reaction.damage < 10 ? "bosshovr/bhvpain3.wav" : reaction.damage < 30 ? "bosshovr/bhvpain1.wav" : "bosshovr/bhvpain2.wav",
             true, 2, true, 1, true, 0);
  if (!qa_q2rm_reacts_to_pain(c)) return;
  q2rm_set_move(c, reaction.damage < 30 ? "boss2_move_pain_light" : "boss2_move_pain_heavy");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  if (q2rm_has(entity->spawnflags, 65536)) {
    if (qa_q2rm_check_gib(c)) { gib(c); return; }
    if (state->dead) return;
  } else {
    q2rm_sound(game, entity, "bosshovr/bhvdeth1.wav", true, 2, true, 1, true, 0);
    state->dead = true; state->can_take_damage = false; entity->count = 0;
    q2rm_set_damageable(game, entity, false);
    qa_q2_body_changes change;
    memset(&change, 0, sizeof change);
    change.has_velocity = true; change.velocity = q2rm_zero();
    q2rm_move_body(game, entity, change, false, false);
    entity->gravity_vector.z *= 0.3;
  }
  q2rm_set_move(c, "boss2_move_death");
}
static void reattack_mg_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->entity->has_enemy && qa_q2m_in_front(c, c->entity->enemy) && q2rm_rnd(c->game) <= 0.7)
    attack_machinegun(c);
  else q2rm_set_move(c, "boss2_move_attack_post_mg");
}
static void predictive_rocket_fn(void *ctx, qa_q2m_context *c) { (void)ctx; fire_rocket(c, true); }
static void rocket_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  fire_rocket(c, c->entity->has_enemy && c->game->host->is_player(c->game->host->ctx, c->entity->enemy)
      && q2rm_rnd(c->game) < 0.9);
}
static void rocket64_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_vec3 right = qa_math_angle_vectors_rule(q2rm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).right;
  double size = entity->scale != 0 ? entity->scale : 1;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, 78));
  start.z += 10 * size;
  start = qa_sub3_double(start, qa_scale3_double(right, (2 + fmod(entity->count, 4) * 8) * size));
  entity->count++;
  qa_vec3 target;
  if (entity->has_enemy && game->host->is_player(game->host->ctx, entity->enemy) && q2rm_rnd(game) < 0.9)
    target = qa_add3_double(enemy.origin,
        qa_scale3_double(enemy.velocity, qa_hypot3(qa_sub3_double(enemy.origin, start)) / 750 - 0.3));
  else { target = enemy.origin; target.z -= 15; }
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(target, start));
  (void)c->weapons.fire_rocket(c->weapons.ctx, entity, game, start, direction, 35, 750, 55, 35);
  qa_q2rm_monster_flash(c, 78, start, direction);
}
static void bullet_left_fn(void *ctx, qa_q2m_context *c) { (void)ctx; bullet(c, 73); }
static void bullet_right_fn(void *ctx, qa_q2m_context *c) { (void)ctx; bullet(c, 133); }
static void machine_gun_fn(void *ctx, qa_q2m_context *c) { (void)ctx; bullet(c, 73); bullet(c, 133); }
static void hyper_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  double id = q2rm_has(entity->frame, 1) ? qa_q2rm_rereleaseFlash_value.BOSS2_MACHINEGUN_L2
                                          : qa_q2rm_rereleaseFlash_value.BOSS2_MACHINEGUN_R2;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, id));
  qa_q2_entity *enemy_entity = game->entity(game->ctx, entity->has_enemy ? &entity->enemy : NULL);
  qa_vec3 target = enemy.origin;
  target.z += enemy_entity ? enemy_entity->view_height : 22;
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(target, start));
  double effects = fmod(entity->frame, 4) == 0 ? 64 : 0;
  (void)c->weapons.fire_blaster(c->weapons.ctx, entity, game, start, direction, 2, 1000, effects, false, false, 0);
  qa_q2rm_monster_flash(c, id, start, direction);
}
static void shrink_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = q2rm_body(c->game, c->entity).bounds;
  bounds.max.z = 50;
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true; change.bounds = bounds;
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2rm_has(c->entity->spawnflags, 65536)) {
    c->state->dead = false;
    c->state->can_take_damage = true;
    q2rm_set_damageable(c->game, c->entity, true);
    return;
  }
  gib(c);
}
static const qa_q2m_named_handler extra_callbacks[] = {
  {"BossExplode", {NULL, qa_q2rm_boss_explode_handler}},
  {"boss2_attack_mg", {NULL, attack_mg_fn}},
  {"boss2_reattack_mg", {NULL, reattack_mg_fn}},
  {"Boss2PredictiveRocket", {NULL, predictive_rocket_fn}},
  {"Boss2Rocket", {NULL, rocket_fn}},
  {"Boss2Rocket64", {NULL, rocket64_fn}},
  {"boss2_firebullet_left", {NULL, bullet_left_fn}},
  {"boss2_firebullet_right", {NULL, bullet_right_fn}},
  {"Boss2MachineGun", {NULL, machine_gun_fn}},
  {"Boss2HyperBlaster", {NULL, hyper_fn}},
  {"boss2_shrink", {NULL, shrink_fn}},
  {"boss2_dead", {NULL, dead_fn}},
};
static qa_q2m_definition boss2_definition;
static bool boss2_ready;
const qa_q2m_definition *qa_q2rm_get_rerelease_boss2_definition(void) {
  if (boss2_ready) return &boss2_definition;
  boss2_definition = qa_q2bm_boss2_definition;
  boss2_definition.moves = qa_q2rm_boss2_moves;
  boss2_definition.move_count = qa_q2rm_boss2_move_count;
  boss2_definition.has_yaw_speed = true;
  boss2_definition.yaw_speed = 50;
  qa_q2_named_think *think_defs = malloc(sizeof *think_defs);
  if (!think_defs) qa_raise("Error", 0, 0, "Out of memory creating rerelease boss2 definition");
  think_defs[0].name = "BossExplode_think";
  think_defs[0].cb = qa_q2rm_boss_explode_think();
  boss2_definition.has_source_callbacks = true;
  memset(&boss2_definition.source_callbacks, 0, sizeof boss2_definition.source_callbacks);
  boss2_definition.source_callbacks.think = think_defs;
  boss2_definition.source_callbacks.think_count = 1;
  boss2_definition.check_attack_ctx = NULL;
  boss2_definition.check_attack = check_attack_fn;
  boss2_definition.initialize = (qa_q2m_handler){NULL, init_fn};
  boss2_definition.attack = (qa_q2m_handler){NULL, attack_fn};
  boss2_definition.reactions.pain = pain_fn;
  boss2_definition.reactions.die = die_fn;
  boss2_definition.callbacks = qa_q2rm_merge_callbacks(NULL, 
      boss2_definition.callbacks, boss2_definition.callback_count,
      extra_callbacks, sizeof extra_callbacks / sizeof extra_callbacks[0], &boss2_definition.callback_count);
  boss2_ready = true;
  return &boss2_definition;
}
