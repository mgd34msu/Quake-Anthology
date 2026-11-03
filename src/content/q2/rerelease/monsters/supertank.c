/* Port of src/content/q2/rerelease/monsters/supertank.ts.
 * Rerelease m_supertank.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/supertank.h"

#include <stdlib.h>

#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/supertank.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/rerelease/monsters/boss.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/flashes.h"
#include "qa/content/q2/rerelease/monsters/tables/supertank.h"
#include "rrm_internal.h"

typedef struct supertank_ctx {
  qa_q2mpm_weapons weapons;
  bool is_n64;
} supertank_ctx;

static void gib(qa_q2m_context *c) {
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  q2rm_emit_effect(game, "q2:explosion1-big", q2rm_body(game, entity).origin, q2rm_zero(), 1, 0);
  if (entity->sound && strcmp(entity->sound, "") != 0)
    q2rm_emit_loop(game, entity, entity->sound, 0, "stop");
  entity->sound = "";
  entity->skin = trunc(entity->skin / 2);
  qa_q2m_gib_options plain = {false, false, false, false};
  qa_q2m_gib_options metal = {false, true, false, false};
  for (int i = 0; i < 2; i++) {
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", 500, plain);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_metal/tris.md2", 500, metal);
  }
  static const char *const parts[8] = {"cgun", "chest", "core", "ltread", "rgun", "rtread", "tube", "head"};
  static const char *const models[8] = {
    "models/monsters/boss1/gibs/cgun.md2", "models/monsters/boss1/gibs/chest.md2",
    "models/monsters/boss1/gibs/core.md2", "models/monsters/boss1/gibs/ltread.md2",
    "models/monsters/boss1/gibs/rgun.md2", "models/monsters/boss1/gibs/rtread.md2",
    "models/monsters/boss1/gibs/tube.md2", "models/monsters/boss1/gibs/head.md2"};
  for (int i = 0; i < 8; i++) {
    bool upright = strcmp(parts[i], "ltread") == 0 || strcmp(parts[i], "rgun") == 0
        || strcmp(parts[i], "rtread") == 0 || strcmp(parts[i], "tube") == 0;
    qa_q2m_gib_options options = {strcmp(parts[i], "head") == 0,
                                  strcmp(parts[i], "cgun") == 0 || strcmp(parts[i], "head") == 0,
                                  true, upright};
    (void)qa_q2m_throw_gib(entity, game, models[i], 500, options);
  }
  c->state->gibbed = true;
}
static void init_impl(supertank_ctx *box, qa_q2m_context *c) {
  qa_q2_entity *entity = c->entity;
  if (box->is_n64) {
    entity->spawnflags = q2rm_or(entity->spawnflags, 16);
    entity->count = 10;
  }
  if (q2rm_has(entity->spawnflags, 8)) qa_q2rm_init_power_armor(c, 400);
}
static void init_fn(void *ctx, qa_q2m_context *c) { init_impl(ctx, c); }
static void boss5_init_fn(void *ctx, qa_q2m_context *c) {
  c->entity->spawnflags = q2rm_or(c->entity->spawnflags, 8);
  init_impl(ctx, c);
  c->entity->skin = 2;
}
static void restore_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2rm_has(c->entity->spawnflags, 8)) qa_q2rm_bind_armor(c, 400);
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_q2_game_services *game = c->game;
  double range = qa_q2m_target_distance(c);
  qa_vec3 delta = qa_sub3_double(enemy.origin, q2rm_body(game, c->entity).origin);
  bool chain = qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, 64));
  bool rocket = qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, 70));
  bool grenade = qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE,
      qa_q2rm_rereleaseFlash_value.SUPERTANK_GRENADE_1));
  if (chain && (!rocket || range <= 540 || q2rm_rnd(game) < 0.3)) {
    if (grenade && (range >= 350 || delta.z > 120 || q2rm_rnd(game) < 0.2)) {
      q2rm_set_move(c, "supertank_move_attack4");
      return;
    }
    double left = q2rm_now(game) + 1.5;
    double right = q2rm_rnd(game) * 1.2;
    c->entity->timestamp = left + right;
    q2rm_set_move(c, "supertank_move_attack1");
    return;
  }
  if (rocket) {
    q2rm_set_move(c, grenade && (delta.z > 120 || q2rm_rnd(game) < 0.2)
        ? "supertank_move_attack4" : "supertank_move_attack2");
    return;
  }
  if (grenade) q2rm_set_move(c, "supertank_move_attack4");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2
      ? q2rm_or(entity->skin, 1) : q2rm_and(entity->skin, ~1);
  if (q2rm_now(game) < state->pain_time) return;
  if (!qa_q2rm_chainfist(c)) {
    if (reaction.damage <= 25 && q2rm_rnd(game) < 0.2) return;
    if (entity->frame >= qa_q2rm_supertank_frames_value.attak2_1
        && entity->frame <= qa_q2rm_supertank_frames_value.attak2_14) return;
  }
  q2rm_snd(c, reaction.damage <= 10 ? "bosstank/btkpain1.wav" : reaction.damage <= 25 ? "bosstank/btkpain3.wav" : "bosstank/btkpain2.wav", 2);
  state->pain_time = q2rm_now(game) + 3;
  if (!qa_q2rm_reacts_to_pain(c)) return;
  q2rm_set_move(c, reaction.damage <= 10 ? "supertank_move_pain1" : reaction.damage <= 25 ? "supertank_move_pain2" : "supertank_move_pain3");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  if (q2rm_has(entity->spawnflags, 1 << 16)) {
    if (qa_q2rm_check_gib(c)) { gib(c); state->dead = true; return; }
    if (state->dead) return;
  } else {
    q2rm_snd(c, "bosstank/btkdeth1.wav", 2);
    state->dead = true; state->can_take_damage = false;
    q2rm_set_damageable(game, entity, false);
  }
  q2rm_set_move(c, "supertank_move_death");
}
static void boss_loop_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!q2rm_has(c->entity->spawnflags, 16)) return;
  if (c->entity->count != 0) c->entity->count--;
  else c->entity->spawnflags = q2rm_and(c->entity->spawnflags, ~16);
  c->state->next_frame = qa_q2rm_supertank_frames_value.death_19;
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!q2rm_has(c->entity->spawnflags, 1 << 16)) { gib(c); return; }
  c->state->dead = false;
  c->state->can_take_damage = true;
  q2rm_set_damageable(c->game, c->entity, true);
}
static void reattack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_set_move(c, qa_q2m_visible_enemy(c)
      && (c->entity->timestamp >= q2rm_now(c->game) || q2rm_rnd(c->game) < 0.3)
      ? "supertank_move_attack1" : "supertank_move_end_attack1");
}
static void machine_gun_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_body_state body = q2rm_body(c->game, c->entity);
  double id = 64 + c->entity->frame - qa_q2rm_supertank_frames_value.attak1_1;
  qa_vec3 angles = {0, body.angles.y, 0};
  qa_vec3 start = qa_q2m_project_flash(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, id), angles);
  qa_vec3 direction;
  if (!qa_q2rm_predicted_direction(c, start, 0, true, -0.1, &direction)) return;
  c->weapons.fire_bullet(c->weapons.ctx, c->entity, c->game, start, direction, 6, 4, 900, 1500, 0);
  qa_q2rm_monster_flash(c, id, start, direction);
}
static void rocket_fn(void *ctx, qa_q2m_context *c) {
  supertank_ctx *box = ctx;
  qa_vec3 eye;
  if (!qa_q2m_enemy_eye(c, &eye)) return;
  double id = c->entity->frame == qa_q2rm_supertank_frames_value.attak2_8 ? 70
      : c->entity->frame == qa_q2rm_supertank_frames_value.attak2_11 ? 71 : 72;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, id));
  bool heat = q2rm_has(c->entity->spawnflags, 8);
  qa_vec3 direction;
  if (heat) direction = qa_normalize3_double_hypot(qa_sub3_double(eye, start));
  else if (!qa_q2rm_predicted_direction_default(c, start, 750, false, &direction)) return;
  if (heat)
    (void)box->weapons.fire_heat_rocket(box->weapons.ctx, c->entity, c->game, start, direction, 40, 500, 60, 40, false, 0);
  else
    (void)c->weapons.fire_rocket(c->weapons.ctx, c->entity, c->game, start, direction, 50, 750, 70, 50);
  qa_q2rm_monster_flash(c, id, start, direction);
}
static void grenade_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  double id = entity->frame == qa_q2rm_supertank_frames_value.attak4_1
      ? qa_q2rm_rereleaseFlash_value.SUPERTANK_GRENADE_1 : qa_q2rm_rereleaseFlash_value.SUPERTANK_GRENADE_2;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, id));
  qa_q2rm_predicted_aim target;
  if (!qa_q2rm_predict_aim(c, start, 0, false, (q2rm_rnd(game) * 2 - 1) * 0.1, &target)) return;
  for (double speed = 500; speed < 1000; speed += 100) {
    qa_vec3 direction;
    if (!qa_q2rm_calculate_pitch_to_fire_default(c, target.point, start, target.direction, speed, 2.5, true, &direction))
      continue;
    qa_q2w_grenade_adjustment adjustment = {0, 0, qa_q2rm_host_gravity(game)};
    (void)c->weapons.fire_grenade(c->weapons.ctx, entity, game, start, direction, 50, speed, 2.5, 90,
                                  false, false, true, &adjustment);
    qa_q2rm_monster_flash(c, id, start, direction);
    return;
  }
}

size_t qa_q2rm_create_rerelease_supertank_definitions(qa_q2mpm_arena *arena, qa_q2mpm_weapons weapons, bool is_n64,
                                                      qa_q2m_definition out[2]) {
  supertank_ctx *box = qa_q2mpm_arena_alloc(arena, sizeof *box);
  qa_q2_named_think *think_defs = qa_q2mpm_arena_alloc(arena, sizeof *think_defs);
  if (!box || !think_defs) qa_raise("Error", 0, 0, "Out of memory creating rerelease supertank definitions");
  box->weapons = weapons;
  box->is_n64 = is_n64;
  think_defs[0].name = "BossExplode_think";
  think_defs[0].cb = qa_q2rm_boss_explode_think();
  const qa_q2m_named_handler extra[] = {
    {"BossExplode", {NULL, qa_q2rm_boss_explode_handler}},
    {"BossLoop", {NULL, boss_loop_fn}},
    {"supertank_dead", {NULL, dead_fn}},
    {"supertank_reattack1", {NULL, reattack_fn}},
    {"supertankMachineGun", {NULL, machine_gun_fn}},
    {"supertankRocket", {box, rocket_fn}},
    {"supertankGrenade", {NULL, grenade_fn}},
  };
  qa_q2m_definition tank = qa_q2bm_supertank_definition;
  tank.moves = qa_q2rm_supertank_moves;
  tank.move_count = qa_q2rm_supertank_move_count;
  tank.blocked_ctx = NULL;
  tank.blocked = qa_q2rm_blocked_check_platform_handler;
  tank.has_source_callbacks = true;
  memset(&tank.source_callbacks, 0, sizeof tank.source_callbacks);
  tank.source_callbacks.think = think_defs;
  tank.source_callbacks.think_count = 1;
  tank.initialize = (qa_q2m_handler){box, init_fn};
  tank.restore = (qa_q2m_handler){NULL, restore_fn};
  tank.attack = (qa_q2m_handler){NULL, attack_fn};
  tank.reactions.pain = pain_fn;
  tank.reactions.die = die_fn;
  tank.callbacks = qa_q2rm_merge_callbacks(arena, tank.callbacks, tank.callback_count,
                                           extra, sizeof extra / sizeof extra[0], &tank.callback_count);
  out[0] = tank;
  out[1] = tank;
  out[1].classname = "monster_boss5";
  out[1].initialize = (qa_q2m_handler){box, boss5_init_fn};
  return 2;
}
