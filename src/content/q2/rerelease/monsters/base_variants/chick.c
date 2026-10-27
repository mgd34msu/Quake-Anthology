/* Port of src/content/q2/rerelease/monsters/base-variants/chick.ts.
 * Rerelease m_chick.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/base_variants/chick.h"

#include <stdlib.h>

#include "qa/content/q2/base/monsters/chick.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/chick.h"
#include "../rrm_internal.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_finish_dodge(c);
  const char *name = c->state->move->name;
  q2rm_set_move(c, c->state->stand_ground ? "chick_move_stand"
      : (strcmp(name, "chick_move_walk") == 0 || strcmp(name, "chick_move_start_run") == 0)
          ? "chick_move_run" : "chick_move_start_run");
}
static void rocket(qa_q2m_context *c, qa_q2mpm_weapons *weapons) {
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_q2f_game *game = c->game;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, 57));
  qa_vec3 right = qa_math_angle_vectors_rule(q2rm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).right;
  qa_vec3 target = state->manual_steering ? state->blind_fire_target : enemy.origin;
  bool heat = entity->skin > 1;
  double speed = heat ? 500 : 650;
  qa_vec3 point = target;
  if (!state->manual_steering) {
    if (q2rm_rnd(game) < 0.33 || start.z < enemy.origin.z + enemy.bounds.min.z) {
      qa_q2f_entity *enemy_entity = game->entity(game->ctx, entity->has_enemy ? &entity->enemy : NULL);
      point.z = target.z + (enemy_entity ? enemy_entity->view_height : 22);
    } else point.z = enemy.origin.z + enemy.bounds.min.z + 1;
  }
  if (!state->manual_steering && q2rm_rnd(game) < 0.35) {
    qa_q2rm_predicted_aim aim;
    if (qa_q2rm_predict_aim_default(c, start, speed, false, &aim)) point = aim.point;
  }
  qa_trace_result obstruction = q2rm_trace(game, start, point, NULL, &entity->actor.id, (double)0x46004003u);
  if (state->manual_steering) {
    if (obstruction.start_solid || obstruction.all_solid || obstruction.fraction < 0.5) {
      point = qa_add3_double(target, qa_scale3_double(right, -10));
      obstruction = q2rm_trace(game, start, point, NULL, &entity->actor.id, (double)0x46004003u);
    }
    if (obstruction.start_solid || obstruction.all_solid || obstruction.fraction < 0.5) {
      point = qa_add3_double(target, qa_scale3_double(right, 10));
      obstruction = q2rm_trace(game, start, point, NULL, &entity->actor.id, (double)0x46004003u);
    }
    if (obstruction.start_solid || obstruction.all_solid || obstruction.fraction < 0.5) return;
  } else if (obstruction.fraction <= 0.5 && obstruction.hit.kind != QA_TRACE_HIT_NONE) {
    bool blocked = obstruction.hit.kind == QA_TRACE_HIT_WORLD;
    if (!blocked && obstruction.hit.kind == QA_TRACE_HIT_ACTOR) {
      qa_q2f_entity *hit = game->entity(game->ctx, &obstruction.hit.actor);
      blocked = hit && hit->solid == QA_Q2F_SOLID_BRUSH;
    }
    if (blocked) return;
  }
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(point, start));
  if (heat)
    (void)weapons->fire_heat_rocket(weapons->ctx, entity, game, start, direction, 50, speed, 70, 50,
                                    true, state->manual_steering ? 0.075 : 0.15);
  else
    (void)c->weapons.fire_rocket(c->weapons.ctx, entity, game, start, direction, 50, speed, 70, 50);
  qa_q2rm_monster_flash(c, 57, start, direction);
}
static void rocket_fn(void *ctx, qa_q2m_context *c) { rocket(c, ctx); }
static void melee_fn(void *ctx, qa_q2m_context *c) { (void)ctx; q2rm_set_move(c, "chick_move_start_slash"); }
static void sight_fn(void *ctx, qa_q2m_context *c) { (void)ctx; q2rm_snd(c, "chick/chksght1.wav", 2); }
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, 57))) return;
  qa_q2m_finish_dodge(c);
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  if (state->attack_state == QA_Q2M_ATTACK_BLIND) {
    double chance = state->blind_fire_delay < 1 ? 1 : state->blind_fire_delay < 7.5 ? 0.4 : 0.1;
    double choice = q2rm_rnd(game);
    state->blind_fire_delay += 5.5 + q2rm_rnd(game);
    if (qa_hypot3(state->blind_fire_target) == 0 || choice > chance) return;
    state->manual_steering = true;
    state->attack_finished = q2rm_now(game) + q2rm_rnd(game) * 2;
  }
  q2rm_set_move(c, "chick_move_start_attack1");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_q2m_finish_dodge(c);
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2
      ? q2rm_or(entity->skin, 1) : q2rm_and(entity->skin, ~1);
  if (q2rm_now(game) < state->pain_time) return;
  state->pain_time = q2rm_now(game) + 3;
  double choice = q2rm_rnd(game);
  q2rm_snd(c, choice < 0.33 ? "chick/chkpain1.wav" : choice < 0.66 ? "chick/chkpain2.wav" : "chick/chkpain3.wav", 2);
  if (!qa_q2rm_reacts_to_pain(c)) return;
  state->manual_steering = false;
  q2rm_set_move(c, reaction.damage <= 10 ? "chick_move_pain1" : reaction.damage <= 25 ? "chick_move_pain2" : "chick_move_pain3");
  if (state->ducked) qa_q2m_set_duck(c, false);
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
    qa_q2m_gib_options skin_up = {false, false, true, true};
    qa_q2m_gib_options skinned = {false, false, true, false};
    qa_q2m_gib_options head = {true, false, true, false};
    for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", damage, plain);
    for (int i = 0; i < 3; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
    static const char *const models[3] = {
      "models/monsters/bitch/gibs/arm.md2", "models/monsters/bitch/gibs/foot.md2",
      "models/monsters/bitch/gibs/tube.md2"};
    for (int i = 0; i < 3; i++) (void)qa_q2m_throw_gib(entity, game, models[i], damage, skin_up);
    (void)qa_q2m_throw_gib(entity, game, "models/monsters/bitch/gibs/chest.md2", damage, skinned);
    (void)qa_q2m_throw_gib(entity, game, "models/monsters/bitch/gibs/head.md2", damage, head);
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  state->dead = true; state->can_take_damage = true;
  q2rm_set_damageable(game, entity, true);
  bool first = q2rm_rnd(game) >= 0.5;
  q2rm_snd(c, first ? "chick/chkdeth1.wav" : "chick/chkdeth2.wav", 2);
  q2rm_set_move(c, first ? "chick_move_death1" : "chick_move_death2");
}
static bool duck_fn(void *ctx, qa_q2m_context *c, double eta) {
  (void)ctx; (void)eta;
  const char *name = c->state->move->name;
  if (strcmp(name, "chick_move_start_attack1") == 0 || strcmp(name, "chick_move_attack1") == 0) {
    c->dispatch(c->impl_ctx, c, "monster_duck_up");
    return false;
  }
  q2rm_set_move(c, "chick_move_duck");
  return true;
}
static bool sidestep_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  const char *name = c->state->move->name;
  if (strcmp(name, "chick_move_start_attack1") == 0 || strcmp(name, "chick_move_attack1") == 0
      || strcmp(name, "chick_move_pain3") == 0) return false;
  if (strcmp(name, "chick_move_run") != 0) q2rm_set_move(c, "chick_move_run");
  return true;
}
static void pre_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_snd(c, "chick/chkatck1.wav", 2);
  if (c->state->manual_steering)
    c->state->ideal_yaw = qa_vector_to_angles_with_rule(
        qa_sub3_double(c->state->blind_fire_target, q2rm_body(c->game, c->entity).origin),
        QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
}
static void slash_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_snd(c, "chick/chkatck3.wav", 1);
  qa_vec3 aim = {80, q2rm_body(c->game, c->entity).bounds.min.x, 10};
  (void)c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, aim,
                            10 + floor(q2rm_rnd(c->game) * 6), 100);
}
static void rerocket_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->manual_steering) {
    c->state->manual_steering = false;
    q2rm_set_move(c, "chick_move_end_attack1");
    return;
  }
  q2rm_set_move(c, qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, 57)) && qa_q2bm_alive_enemy(c)
      && qa_q2m_target_distance(c) > 80 && qa_q2m_visible_enemy(c) && q2rm_rnd(c->game) <= 0.7
      ? "chick_move_attack1" : "chick_move_end_attack1");
}
static void reslash_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_set_move(c, qa_q2bm_alive_enemy(c) && qa_q2m_target_distance(c) <= 80 && q2rm_rnd(c->game) <= 0.9
      ? "chick_move_slash" : "chick_move_end_slash");
}
static void shrink_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = q2rm_body(c->game, c->entity).bounds;
  c->entity->server_flags = q2rm_or(c->entity->server_flags, 2);
  bounds.max.z = 12;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true; change.bounds = bounds;
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_corpse(c);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2rm_v3(-16, -16, 0);
  change.bounds.max = q2rm_v3(16, 16, 8);
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void heat_after_spawn_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->skin = 2;
}

size_t qa_q2rm_create_rerelease_chick_definitions(qa_q2mpm_weapons weapons, qa_q2m_definition out[2]) {
  qa_q2mpm_weapons *box = malloc(sizeof *box);
  if (!box) qa_q2f_error("Out of memory creating rerelease chick definitions");
  *box = weapons;
  const qa_q2m_definition *base = &qa_q2bm_chick_definition;
  const qa_q2m_named_handler extra[] = {
    {"chick_run", {NULL, run_fn}},
    {"ChickRocket", {box, rocket_fn}},
    {"Chick_PreAttack1", {NULL, pre_attack_fn}},
    {"ChickSlash", {NULL, slash_fn}},
    {"chick_rerocket", {NULL, rerocket_fn}},
    {"chick_reslash", {NULL, reslash_fn}},
    {"chick_shrink", {NULL, shrink_fn}},
    {"chick_dead", {NULL, dead_fn}},
  };
  /* TS builds this definition literally from base fields. */
  qa_q2m_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.classname = base->classname;
  definition.kind = base->kind;
  definition.model = base->model;
  definition.health = 175; definition.gib_health = -70; definition.mass = 200;
  definition.bounds = base->bounds;
  definition.scale = 1;
  definition.initial_move = base->initial_move;
  definition.stand = base->stand;
  definition.walk = base->walk;
  definition.melee = (qa_q2m_handler){NULL, melee_fn};
  definition.sight = (qa_q2m_handler){NULL, sight_fn};
  definition.moves = qa_q2rm_chick_moves;
  definition.move_count = qa_q2rm_chick_move_count;
  definition.has_blind_fire_flag = true;
  definition.blind_fire = true;
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.attack = (qa_q2m_handler){NULL, attack_fn};
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.duck_ctx = NULL; definition.duck = duck_fn;
  definition.sidestep_ctx = NULL; definition.sidestep = sidestep_fn;
  definition.blocked_ctx = NULL; definition.blocked = qa_q2rm_blocked_check_platform_handler;
  definition.callbacks = qa_q2rm_merge_callbacks(base->callbacks, base->callback_count,
                                                 extra, sizeof extra / sizeof extra[0], &definition.callback_count);
  out[0] = definition;
  out[1] = definition;
  out[1].classname = "monster_chick_heat";
  out[1].after_spawn = (qa_q2m_handler){NULL, heat_after_spawn_fn};
  return 2;
}
