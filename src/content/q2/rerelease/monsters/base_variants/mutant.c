/* Port of src/content/q2/rerelease/monsters/base-variants/mutant.ts.
 * Rerelease m_mutant.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/base_variants/mutant.h"

#include <stdio.h>

#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/mutant.h"
#include "../rrm_internal.h"

static void hit(qa_q2m_context *c, bool right) {
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_bounds bounds = q2rm_body(game, entity).bounds;
  qa_vec3 aim = {80, right ? bounds.max.x : bounds.min.x, 8};
  bool connected = c->weapons.fire_hit(c->weapons.ctx, entity, game, aim,
                                       5 + floor(q2rm_rnd(game) * 10), 100);
  if (!connected) c->state->melee_time = q2rm_now(game) + 1.5;
  q2rm_snd(c, connected ? (right ? "mutant/mutatck3.wav" : "mutant/mutatck2.wav") : "mutant/mutatck1.wav", 1);
}
static void jump_impl(qa_q2m_context *c, bool up) {
  qa_body_state body = q2rm_body(c->game, c->entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  qa_vec3 velocity = qa_add3_double(body.velocity,
      qa_add3_double(qa_scale3_double(axes.forward, up ? 200 : 100), qa_scale3_double(axes.up, up ? 450 : 300)));
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_velocity = true; change.velocity = velocity;
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void touch_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_touch_contact contact) {
  qa_q2m_monsters *monsters = ctx;
  qa_q2m_context *context = qa_q2m_monsters_context(monsters, entity->actor.id);
  if (!context) return;
  if (qa_q2m_health(game, &entity->actor.id) <= 0) { entity->touch.fn = NULL; entity->touch.ctx = NULL; return; }
  qa_body_state body = q2rm_body(game, entity);
  qa_combat_state_result combat = game->host->combat.read(game->host->combat.ctx, contact.other);
  if (entity->style == 1 && combat.has_value && combat.value.can_take_damage && qa_hypot3(body.velocity) > 30) {
    qa_vec3 normal = qa_normalize3_double_hypot(body.velocity);
    qa_vec3 point = qa_add3_double(body.origin, qa_scale3_double(normal, body.bounds.max.x));
    double damage = trunc(40 + q2rm_rnd(game) * 10);
    (void)q2rm_damage(game, contact.other, entity, &entity->actor.id, damage, damage, body.velocity, point, normal, 0, false, 0);
    entity->style = 0;
  }
  if (!qa_q2m_check_bottom(context, q2rm_body(game, entity).origin)) {
    if (q2rm_body(game, entity).ground) {
      context->state->next_frame = qa_q2rm_mutant_frames_value.attack02;
      entity->touch.fn = NULL; entity->touch.ctx = NULL;
    }
    return;
  }
  entity->touch.fn = NULL; entity->touch.ctx = NULL;
}
static void slide_right_fn(void *ctx, qa_q2m_context *c, double distance) {
  (void)ctx;
  (void)qa_q2m_walk_move(c, q2rm_body(c->game, c->entity).angles.y + 90, distance, true, true);
}
static void slide_left_fn(void *ctx, qa_q2m_context *c, double distance) {
  (void)ctx;
  (void)qa_q2m_walk_move(c, q2rm_body(c->game, c->entity).angles.y - 90, distance, true, true);
}
static bool check_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_state *state = c->state;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  if (!qa_q2bm_alive_enemy(c)) return false;
  if (qa_q2m_target_distance(c) <= 80 && state->melee_time <= q2rm_now(game)) {
    state->attack_state = QA_Q2M_ATTACK_MELEE;
    return true;
  }
  if (q2rm_has(entity->spawnflags, 8)) return false;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return false;
  qa_body_state body = q2rm_body(game, entity);
  if (body.origin.z + body.bounds.min.z + 125 < enemy.origin.z + enemy.bounds.min.z) return false;
  qa_vec3 delta = qa_sub3_double(body.origin, enemy.origin);
  double distance = hypot(delta.x, delta.y);
  if ((distance < 100 && state->melee_time <= q2rm_now(game)) || distance > 265
      || state->attack_finished >= q2rm_now(game) || q2rm_rnd(game) >= 0.5) return false;
  state->attack_state = QA_Q2M_ATTACK_MISSILE;
  return true;
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2 ? 1 : 0;
  if (q2rm_now(game) < state->pain_time) return;
  state->pain_time = q2rm_now(game) + 3;
  double choice = q2rm_rnd(game);
  q2rm_snd(c, choice < 0.33 || choice >= 0.66 ? "mutant/mutpain1.wav" : "mutant/mutpain2.wav", 2);
  if (!qa_q2rm_reacts_to_pain(c)) return;
  q2rm_set_move(c, choice < 0.33 ? "mutant_move_pain1" : choice < 0.66 ? "mutant_move_pain2" : "mutant_move_pain3");
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
    for (int i = 0; i < 4; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
    for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/monsters/mutant/gibs/hand.md2", damage, skin_up);
    for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/monsters/mutant/gibs/foot.md2", damage, skinned);
    (void)qa_q2m_throw_gib(entity, game, "models/monsters/mutant/gibs/chest.md2", damage, skinned);
    (void)qa_q2m_throw_gib(entity, game, "models/monsters/mutant/gibs/head.md2", damage, head);
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  q2rm_snd(c, "mutant/mutdeth1.wav", 2);
  state->dead = true; state->can_take_damage = true;
  q2rm_set_damageable(game, entity, true);
  q2rm_set_move(c, q2rm_rnd(game) < 0.5 ? "mutant_move_death1" : "mutant_move_death2");
}
static bool blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  (void)ctx;
  qa_q2rm_jump_navigation navigation;
  memset(&navigation, 0, sizeof navigation);
  qa_q2rm_jump_result result = qa_q2rm_blocked_check_jump(c, distance, 256, 68,
                                                          !q2rm_has(c->entity->spawnflags, 8), navigation);
  if (result != QA_Q2RM_JUMP_NONE) {
    if (result != QA_Q2RM_JUMP_TURN && c->entity->has_enemy)
      q2rm_set_move(c, result == QA_Q2RM_JUMP_UP ? "mutant_move_jump_up" : "mutant_move_jump_down");
    return true;
  }
  return qa_q2rm_blocked_check_platform(c, distance);
}
static void step_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  char path[32];
  (void)snprintf(path, sizeof path, "mutant/step%d.wav", (int)floor(q2rm_rnd(c->game) * 3) + 1);
  q2rm_snd(c, path, 4);
}
static void hit_left_fn(void *ctx, qa_q2m_context *c) { (void)ctx; hit(c, false); }
static void hit_right_fn(void *ctx, qa_q2m_context *c) { (void)ctx; hit(c, true); }
static void check_refire_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2bm_alive_enemy(c) && c->state->melee_time <= q2rm_now(c->game)
      && (q2rm_rnd(c->game) < 0.5 || qa_q2m_target_distance(c) <= 80))
    c->state->next_frame = qa_q2rm_mutant_frames_value.attack09;
}
static void takeoff_fn(void *ctx, qa_q2m_context *c) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  qa_body_state body = q2rm_body(game, entity);
  qa_vec3 forward = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward;
  q2rm_snd(c, "mutant/mutsght1.wav", 2);
  qa_vec3 origin = body.origin;
  origin.z += 1;
  qa_vec3 velocity = qa_scale3_double(forward, 425);
  velocity.z = 160;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = origin;
  change.has_velocity = true; change.velocity = velocity;
  change.has_ground = true; change.ground = NULL;
  q2rm_move_body(game, entity, change, false, false);
  state->ducked = true;
  state->attack_finished = q2rm_now(game) + 3;
  entity->style = 1;
  entity->touch.ctx = ctx;
  entity->touch.fn = touch_fn;
}
static void check_landing_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  (void)qa_q2rm_monster_jump_finished(c);
  if (q2rm_body(c->game, c->entity).ground) {
    q2rm_snd(c, "mutant/thud1.wav", 1);
    c->state->attack_finished = q2rm_now(c->game) + 0.5 + q2rm_rnd(c->game);
    if (qa_q2m_target_distance(c) <= 160) c->melee(c->impl_ctx, c);
    return;
  }
  c->state->next_frame = q2rm_now(c->game) > c->state->attack_finished
      ? qa_q2rm_mutant_frames_value.attack02 : qa_q2rm_mutant_frames_value.attack05;
}
static void jump_down_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_impl(c, false); }
static void jump_up_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_impl(c, true); }
static void wait_land_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->next_frame = !qa_q2rm_monster_jump_finished(c) && !q2rm_body(c->game, c->entity).ground
      ? c->entity->frame : c->entity->frame + 1;
}
static void shrink_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = q2rm_body(c->game, c->entity).bounds;
  c->entity->server_flags = q2rm_or(c->entity->server_flags, 2);
  bounds.max.z = 0;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true; change.bounds = bounds;
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void monster_dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = q2rm_body(c->game, c->entity).bounds;
  qa_q2m_corpse(c);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true; change.bounds = bounds;
  q2rm_move_body(c->game, c->entity, change, false, false);
}

void qa_q2rm_create_rerelease_mutant_definition(qa_q2rm_mutant_definition *out, qa_q2m_monsters *monsters) {
  memset(out, 0, sizeof *out);
  qa_q2bm_create_mutant_definition(&out->base, monsters);
  qa_q2m_definition definition = out->base.definition;
  const qa_q2m_named_handler extra[] = {
    {"mutant_step", {NULL, step_fn}},
    {"mutant_hit_left", {NULL, hit_left_fn}},
    {"mutant_hit_right", {NULL, hit_right_fn}},
    {"mutant_check_refire", {NULL, check_refire_fn}},
    {"mutant_jump_takeoff", {monsters, takeoff_fn}},
    {"mutant_check_landing", {NULL, check_landing_fn}},
    {"mutant_jump_down", {NULL, jump_down_fn}},
    {"mutant_jump_up", {NULL, jump_up_fn}},
    {"mutant_jump_wait_land", {NULL, wait_land_fn}},
    {"mutant_shrink", {NULL, shrink_fn}},
    {"monster_dead", {NULL, monster_dead_fn}},
  };
  size_t count = 0;
  for (size_t i = 0; i < definition.callback_count; i++) out->callbacks[count++] = definition.callbacks[i];
  for (size_t i = 0; i < sizeof extra / sizeof extra[0]; i++) {
    size_t j = 0;
    while (j < count && strcmp(out->callbacks[j].name, extra[i].name) != 0) j++;
    if (j < count) out->callbacks[j] = extra[i];
    else {
      if (count >= 24) qa_q2f_error("Rerelease mutant callback table overflow");
      out->callbacks[count++] = extra[i];
    }
  }
  definition.moves = qa_q2rm_mutant_moves;
  definition.move_count = qa_q2rm_mutant_move_count;
  definition.has_source_callbacks = true;
  memset(&definition.source_callbacks, 0, sizeof definition.source_callbacks);
  out->touch_defs[0].name = "mutant_jump_touch";
  out->touch_defs[0].cb.ctx = monsters;
  out->touch_defs[0].cb.fn = touch_fn;
  definition.source_callbacks.touch = out->touch_defs;
  definition.source_callbacks.touch_count = 1;
  out->ai[0] = (qa_q2m_named_ai){"ai_move_slide_right", NULL, slide_right_fn};
  out->ai[1] = (qa_q2m_named_ai){"ai_move_slide_left", NULL, slide_left_fn};
  definition.ai = out->ai;
  definition.ai_count = 2;
  definition.check_attack_ctx = NULL;
  definition.check_attack = check_attack_fn;
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.blocked_ctx = NULL;
  definition.blocked = blocked_fn;
  definition.callbacks = out->callbacks;
  definition.callback_count = count;
  out->definition = definition;
}
