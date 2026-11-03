/* Port of src/content/q2/rerelease/monsters/guncmdr.ts.
 * Rerelease m_guncmdr.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/guncmdr.h"

#include <stdio.h>
#include <stdlib.h>

#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/rerelease/monsters/berserk.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/flashes.h"
#include "qa/content/q2/rerelease/monsters/tables/guncmdr.h"
#include "rrm_internal.h"
#include "qa/foundation/js.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_finish_dodge(c);
  q2rm_set_move(c, c->state->stand_ground ? "guncmdr_move_stand" : "guncmdr_move_run");
}
static bool behind(qa_q2m_context *c, const qa_actor_id *actor) {
  if (!actor) return false;
  qa_body_state_result other = qa_shared_body_table_read(c->game->host->bodies, *actor);
  if (!other.has_value) return false;
  qa_body_state body = q2rm_body(c->game, c->entity);
  qa_vec3 delta = qa_sub3_double(other.value.origin, body.origin);
  delta.z = 0;
  return qa_dot3_double(qa_normalize3_double_hypot(delta),
                        qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward) < -0.4;
}
static bool can_advance(qa_q2m_context *c) {
  qa_body_state body = q2rm_body(c->game, c->entity);
  qa_vec3 end = qa_add3_double(body.origin,
      qa_scale3_double(qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward, 8));
  return q2rm_trace(c->game, body.origin, end, &body.bounds, &c->entity->actor.id, (double)0x2020003u).fraction == 1;
}
static void fire_chain(qa_q2m_context *c, bool has_immediate, bool immediate) {
  qa_body_state enemy;
  const char *name = !c->state->stand_ground && qa_q2m_enemy_body(c, &enemy)
      && qa_q2m_target_distance(c) > 400 && can_advance(c) ? "guncmdr_move_fire_chain_run" : "guncmdr_move_fire_chain";
  c->set_move(c->impl_ctx, c, name, has_immediate, immediate);
}
static void attack_impl(qa_q2m_context *c) {
  qa_q2m_finish_dodge(c);
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_q2_game_services *game = c->game;
  double distance = qa_q2m_target_distance(c);
  qa_body_state body = q2rm_body(game, c->entity);
  if (distance < 80 && c->state->melee_time < q2rm_now(game)) { q2rm_set_move(c, "guncmdr_move_attack_kick"); return; }
  if ((distance <= 100 || q2rm_rnd(game) < 0.5)
      && qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, qa_q2rm_rereleaseFlash_value.GUNCMDR_CHAINGUN_1))) {
    q2rm_set_move(c, "guncmdr_move_attack_chain");
    return;
  }
  qa_vec3 aim = qa_normalize3_double_hypot(qa_sub3_double(enemy.origin, body.origin));
  qa_vec3 mortar_offset = qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, qa_q2rm_rereleaseFlash_value.GUNCMDR_GRENADE_MORTAR_1);
  qa_vec3 front_offset = qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, qa_q2rm_rereleaseFlash_value.GUNCMDR_GRENADE_FRONT_1);
  qa_vec3 out;
  if ((distance >= 525 || fabs(body.origin.z + body.bounds.min.z - enemy.origin.z - enemy.bounds.max.z) > 64)
      && qa_q2m_clear_shot(c, mortar_offset)
      && qa_q2rm_calculate_pitch_to_fire_default(c, enemy.origin, qa_q2m_project_flash_default(c, mortar_offset),
                                                 aim, 850, 2.5, true, &out)) {
    q2rm_set_move(c, "guncmdr_move_attack_mortar");
    qa_q2m_set_duck(c, true);
    return;
  }
  if (qa_q2m_clear_shot(c, front_offset) && !c->state->stand_ground
      && qa_q2rm_calculate_pitch_to_fire_default(c, enemy.origin, qa_q2m_project_flash_default(c, front_offset),
                                                 aim, 600, 2.5, false, &out)) {
    q2rm_set_move(c, "guncmdr_move_attack_grenade_back");
    return;
  }
  if (c->state->stand_ground) q2rm_set_move(c, "guncmdr_move_attack_chain");
}
static void attack_fn(void *ctx, qa_q2m_context *c) { (void)ctx; attack_impl(c); }
static void jump_impl(qa_q2m_context *c, bool high) {
  qa_body_state body = q2rm_body(c->game, c->entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  qa_vec3 velocity = qa_add3_double(body.velocity,
      qa_add3_double(qa_scale3_double(axes.forward, high ? 150 : 100), qa_scale3_double(axes.up, high ? 400 : 300)));
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_velocity = true; change.velocity = velocity;
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static bool sidestep_impl(qa_q2m_context *c) {
  const char *current = c->state->move->name;
  const char *side = c->state->lefty ? "left" : "right";
  char buffer[64];
  if (strcmp(current, "guncmdr_move_fire_chain") == 0 || strcmp(current, "guncmdr_move_fire_chain_run") == 0) {
    (void)snprintf(buffer, sizeof buffer, "guncmdr_move_fire_chain_dodge_%s", side);
    c->set_move(c->impl_ctx, c, buffer, true, false);
  } else if (strcmp(current, "guncmdr_move_attack_grenade_back") == 0) {
    c->entity->count = c->entity->frame;
    (void)snprintf(buffer, sizeof buffer, "guncmdr_move_attack_grenade_back_dodge_%s", side);
    c->set_move(c->impl_ctx, c, buffer, true, false);
  } else if (strcmp(current, "guncmdr_move_attack_mortar") == 0) {
    c->entity->count = c->entity->frame;
    c->set_move(c->impl_ctx, c, "guncmdr_move_attack_mortar_dodge", true, false);
  } else if (strcmp(current, "guncmdr_move_run") == 0) q2rm_set_move(c, "guncmdr_move_run");
  else return false;
  return true;
}
static bool sidestep_fn(void *ctx, qa_q2m_context *c) { (void)ctx; return sidestep_impl(c); }
static void init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  entity->scale = 1.25;
  entity->skin = 2;
  state->scale = (float)((float)1.15f * 1.25);
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2rm_v3(-20, -20, -30);
  change.bounds.max = q2rm_v3(20, 20, 45);
  q2rm_move_body(game, entity, change, false, false);
  state->normal_height = 45;
  entity->view_height = 37;
  q2rm_set_mass(game, entity, 255 * 1.25);
  qa_q2rm_init_power_armor(c, 200);
}
static bool duck_fn(void *ctx, qa_q2m_context *c, double eta) {
  (void)ctx; (void)eta;
  const char *current = c->state->move->name;
  if (strcmp(current, "guncmdr_move_jump") == 0 || strcmp(current, "guncmdr_move_jump2") == 0) return false;
  if (strstr(current, "_dodge")) { qa_q2m_set_duck(c, false); return false; }
  q2rm_set_move(c, "guncmdr_move_duck_attack");
  return true;
}
static bool blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  (void)ctx;
  if (qa_q2rm_blocked_check_platform(c, distance)) return true;
  qa_q2rm_jump_navigation navigation;
  memset(&navigation, 0, sizeof navigation);
  qa_q2rm_jump_result result = qa_q2rm_blocked_check_jump(c, distance, 192, 40,
                                                          !q2rm_has(c->entity->spawnflags, 8), navigation);
  if (result == QA_Q2RM_JUMP_NONE) return false;
  qa_body_state enemy;
  if (result != QA_Q2RM_JUMP_TURN && qa_q2m_enemy_body(c, &enemy)) {
    qa_q2m_finish_dodge(c);
    q2rm_set_move(c, result == QA_Q2RM_JUMP_UP ? "guncmdr_move_jump2" : "guncmdr_move_jump");
  }
  return true;
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_q2m_finish_dodge(c);
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2
      ? q2rm_or(entity->skin, 1) : q2rm_and(entity->skin, ~1);
  const char *name = state->move->name;
  if (strcmp(name, "guncmdr_move_jump") == 0 || strcmp(name, "guncmdr_move_jump2") == 0
      || strcmp(name, "guncmdr_move_duck_attack") == 0) return;
  if (q2rm_now(game) < state->pain_time) {
    if (q2rm_rnd(game) < 0.3 && reaction.attacker)
      c->dodge(c->impl_ctx, c, *reaction.attacker, game->host->frame_seconds(game->host->ctx), NULL, true, false);
    return;
  }
  state->pain_time = q2rm_now(game) + 3;
  q2rm_snd(c, q2rm_rnd(game) < 0.5 ? "guncmdr/gcdrpain2.wav" : "guncmdr/gcdrpain1.wav", 2);
  if (!qa_q2rm_reacts_to_pain(c)) {
    if (q2rm_rnd(game) < 0.3 && reaction.attacker)
      c->dodge(c->impl_ctx, c, *reaction.attacker, game->host->frame_seconds(game->host->ctx), NULL, true, false);
    return;
  }
  if (reaction.damage < 35) {
    double choice = floor(q2rm_rnd(game) * 4);
    char buffer[32];
    (void)snprintf(buffer, sizeof buffer, "guncmdr_move_pain%d", choice == 0 ? 3 : choice == 1 ? 2 : choice == 2 ? 1 : 7);
    q2rm_set_move(c, buffer);
  } else {
    q2rm_set_move(c, behind(c, reaction.attacker) ? "guncmdr_move_pain6"
        : q2rm_rnd(game) < 0.5 ? "guncmdr_move_pain4" : "guncmdr_move_pain5");
    state->pain_time += 1.5;
  }
  state->manual_steering = false;
  if (state->ducked) qa_q2m_set_duck(c, false);
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  double damage = reaction.pain.damage;
  if (qa_q2rm_check_gib(c)) {
    q2rm_snd(c, "misc/udeath.wav", 2);
    entity->skin = trunc(entity->skin / 2);
    qa_q2m_gib_options plain = {false, false, false, false};
    for (int i = 0; i < 2; i++) {
      (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", damage, plain);
      (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
    }
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/gear/tris.md2", damage, plain);
    static const char *const parts[4] = {"chest", "garm", "gun", "foot"};
    static const char *const models[4] = {
      "models/monsters/gunner/gibs/chest.md2", "models/monsters/gunner/gibs/garm.md2",
      "models/monsters/gunner/gibs/gun.md2", "models/monsters/gunner/gibs/foot.md2"};
    for (int i = 0; i < 4; i++) {
      qa_q2m_gib_options options = {false, false, true,
                                    strcmp(parts[i], "garm") == 0 || strcmp(parts[i], "gun") == 0};
      (void)qa_q2m_throw_gib(entity, game, models[i], damage, options);
    }
    qa_q2m_gib_options head = {true, false, true, false};
    (void)qa_q2m_throw_gib(entity, game, strcmp(state->move->name, "guncmdr_move_death5") != 0
        ? "models/objects/gibs/sm_meat/tris.md2" : "models/monsters/gunner/gibs/head.md2", damage, head);
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  q2rm_snd(c, "guncmdr/gcdrdeath1.wav", 2);
  state->dead = true; state->can_take_damage = true;
  q2rm_set_damageable(game, entity, true);
  const qa_q2rm_guncmdr_frames *frame = &qa_q2rm_guncmdr_frames_value;
  if ((strcmp(state->move->name, "guncmdr_move_pain5") == 0 && entity->frame < frame->c_pain508)
      || (strcmp(state->move->name, "guncmdr_move_pain6") == 0 && entity->frame < frame->c_pain607)) return;
  qa_body_state body = q2rm_body(game, entity);
  if (fabs(body.origin.z + entity->view_height - reaction.point.z) <= 4 && body.velocity.z < 65) {
    q2rm_set_move(c, "guncmdr_move_death5");
    qa_q2m_gib_options plain = {false, false, false, false};
    qa_q2_entity *head = qa_q2m_throw_gib(entity, game, "models/monsters/gunner/gibs/head.md2", damage, plain);
    qa_body_state_result inflictor;
    memset(&inflictor, 0, sizeof inflictor);
    if (reaction.inflictor) inflictor = qa_shared_body_table_read(game->host->bodies, *reaction.inflictor);
    if (head) {
      qa_vec3 from = inflictor.has_value ? inflictor.value.origin : body.origin;
      qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(body.origin, from));
      head->angular_velocity = qa_scale3_double(head->angular_velocity, 0.15);
      qa_vec3 velocity = qa_scale3_double(direction, 100);
      velocity.z = 200;
      qa_vec3 origin = qa_add3_double(body.origin, q2rm_v3(0, 0, 24));
      qa_q2_body_changes change;
      memset(&change, 0, sizeof change);
      change.has_origin = true; change.origin = origin;
      change.has_angles = true; change.angles = body.angles;
      change.has_velocity = true; change.velocity = velocity;
      q2rm_move_body(game, head, change, false, false);
    }
  } else if (behind(c, reaction.inflictor)) {
    double left2 = q2rm_rnd(game);
    double right2 = (strcmp(state->move->name, "guncmdr_move_pain6") == 0 ? 2 : 3);
    double choice = floor(left2 * right2);
    q2rm_set_move(c, choice == 0 ? "guncmdr_move_death3" : choice == 1 ? "guncmdr_move_death7" : "guncmdr_move_pain6");
  } else {
    double left = q2rm_rnd(game);
    double right = (strcmp(state->move->name, "guncmdr_move_pain5") == 0 ? 1 : 2);
    double choice = floor(left * right);
    q2rm_set_move(c, choice == 0 ? "guncmdr_move_death4" : "guncmdr_move_pain5");
  }
}
static void fidget_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!c->state->stand_ground && !c->entity->has_enemy && q2rm_rnd(c->game) <= 0.05)
    q2rm_set_move(c, "guncmdr_move_fidget");
}
static void pain5_death1_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2m_health(c->game, &c->entity->actor.id) < 0) q2rm_set_move_imm(c, "guncmdr_move_death1", false);
}
static void pain5_death2_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2m_health(c->game, &c->entity->actor.id) < 0 && q2rm_rnd(c->game) < 0.5)
    q2rm_set_move_imm(c, "guncmdr_move_death2", false);
}
static void pain6_death6_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2m_health(c->game, &c->entity->actor.id) < 0) q2rm_set_move_imm(c, "guncmdr_move_death6", false);
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_corpse(c);
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = qa_scale3_double(q2rm_v3(-16, -16, -24), c->entity->scale);
  change.bounds.max = qa_scale3_double(q2rm_v3(16, 16, -8), c->entity->scale);
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void shrink_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->server_flags = q2rm_or(c->entity->server_flags, 2);
  qa_bounds bounds = q2rm_body(c->game, c->entity).bounds;
  bounds.max.z = -4 * c->entity->scale;
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true; change.bounds = bounds;
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void fire_chain_fn(void *ctx, qa_q2m_context *c) { (void)ctx; fire_chain(c, true, true); }
static void refire_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_finish_dodge(c);
  c->state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
  if (qa_q2m_health(c->game, c->entity->has_enemy ? &c->entity->enemy : NULL) > 0
      && qa_q2m_visible_enemy(c) && q2rm_rnd(c->game) <= 0.5) fire_chain(c, true, false);
  else c->set_move(c->impl_ctx, c, "guncmdr_move_endfire_chain", true, false);
}
static void cmdr_fire_fn(void *ctx, qa_q2m_context *c) {
  qa_q2mpm_weapons *weapons = ctx;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  const qa_q2rm_guncmdr_frames *frame = &qa_q2rm_guncmdr_frames_value;
  double id = entity->frame >= frame->c_attack401 && entity->frame <= frame->c_attack505
      ? qa_q2rm_rereleaseFlash_value.GUNCMDR_CHAINGUN_2 : qa_q2rm_rereleaseFlash_value.GUNCMDR_CHAINGUN_1;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, id));
  qa_vec3 aim;
  if (!qa_q2rm_predicted_direction(c, start, 800, false, q2rm_rnd(game) * 0.3, &aim)) return;
  double x2 = aim.x + (q2rm_rnd(game) * 2 - 1) * 0.025;
  double y2 = aim.y + (q2rm_rnd(game) * 2 - 1) * 0.025;
  double z2 = aim.z + (q2rm_rnd(game) * 2 - 1) * 0.025;
  qa_vec3 direction = {x2, y2, z2};
  (void)weapons->fire_flechette(weapons->ctx, entity, game, start, direction, 4, 800, 2);
  qa_q2rm_monster_flash(c, id, start, direction);
}
typedef struct cmdr_shot { double frame, spread, id; int kind; } cmdr_shot; /* kind 0 mortar 1 front 2 crouch */
static void cmdr_grenade_fn(void *ctx, qa_q2m_context *c) {
  qa_q2mpm_weapons *weapons = ctx;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  const qa_q2rm_guncmdr_frames *frame = &qa_q2rm_guncmdr_frames_value;
  const qa_q2rm_rereleaseFlash *flash = &qa_q2rm_rereleaseFlash_value;
  const cmdr_shot shots[9] = {
    {frame->c_attack205, -0.1, flash->GUNCMDR_GRENADE_MORTAR_1, 0},
    {frame->c_attack208, 0, flash->GUNCMDR_GRENADE_MORTAR_2, 0},
    {frame->c_attack211, 0.1, flash->GUNCMDR_GRENADE_MORTAR_3, 0},
    {frame->c_attack304, -0.1, flash->GUNCMDR_GRENADE_FRONT_1, 1},
    {frame->c_attack307, 0, flash->GUNCMDR_GRENADE_FRONT_2, 1},
    {frame->c_attack310, 0.1, flash->GUNCMDR_GRENADE_FRONT_3, 1},
    {frame->c_attack911, 0.25, flash->GUNCMDR_GRENADE_CROUCH_1, 2},
    {frame->c_attack912, 0, flash->GUNCMDR_GRENADE_CROUCH_2, 2},
    {frame->c_attack913, -0.25, flash->GUNCMDR_GRENADE_CROUCH_3, 2},
  };
  const cmdr_shot *shot = NULL;
  for (int i = 0; i < 9; i++) if (shots[i].frame == entity->frame) { shot = &shots[i]; break; }
  if (!shot) return;
  bool blind = state->manual_steering && !qa_q2m_visible_enemy(c);
  qa_vec3 target = blind ? state->blind_fire_target : enemy.origin;
  if (blind && qa_hypot3(target) == 0) return;
  qa_body_state body = q2rm_body(game, entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, shot->id));
  qa_vec3 delta = qa_sub3_double(target, body.origin);
  double pitch = 0;
  if (shot->kind != 2) {
    double distance = qa_hypot3(delta);
    if (distance > 512 && delta.z < 64 && delta.z > -64) delta.z += distance - 512;
    pitch = qa_js_max(-0.5, qa_js_min(0.4, qa_normalize3_double_hypot(delta).z));
    if (enemy.origin.z + enemy.bounds.min.z - body.origin.z - body.bounds.max.z > 16 && shot->kind == 0)
      pitch += 0.5;
  }
  if (shot->kind == 1) pitch -= 0.05;
  qa_vec3 direction;
  if (shot->kind == 2) {
    if (!qa_q2rm_predicted_direction_default(c, start, 800, false, &direction)) return;
  } else direction = qa_add3_double(axes.forward, qa_scale3_double(axes.up, pitch));
  qa_vec3 aim = qa_normalize3_double_hypot(qa_add3_double(direction, qa_scale3_double(axes.right, shot->spread)));
  if (shot->kind == 2) {
    for (int i = 0; i < 3; i++) {
      qa_vec3 ripper_dir = qa_add3_double(aim, qa_scale3_double(axes.right, -0.25 + 0.125 * (i + 1)));
      (void)weapons->fire_ion_ripper(weapons->ctx, entity, game, start, ripper_dir, 15, 800, 0x100000);
    }
  } else {
    double speed = shot->kind == 0 ? 850 : 600;
    qa_vec3 predicted;
    bool have = qa_q2rm_calculate_pitch_to_fire_default(c, target, start, aim, speed, 2.5, shot->kind == 0, &predicted);
    double right = (q2rm_rnd(game) * 2 - 1) * 10;
    double up = !have ? 200 + (q2rm_rnd(game) * 2 - 1) * 10 : q2rm_rnd(game) * 10;
    qa_q2w_grenade_adjustment adjustment = {right, up, qa_q2rm_host_gravity(game)};
    (void)c->weapons.fire_grenade(c->weapons.ctx, entity, game, start, have ? predicted : aim, 50, speed, 2.5, 90,
                                  false, false, true, &adjustment);
  }
  qa_q2rm_monster_flash(c, shot->id, start, aim);
}
static void mortar_resume_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_set_move(c, "guncmdr_move_attack_mortar");
  c->state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
  c->entity->frame = c->entity->count;
}
static void back_resume_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_set_move(c, "guncmdr_move_attack_grenade_back");
  c->state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
  c->entity->frame = c->entity->count;
}
static void kick_finished_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->melee_time = q2rm_now(c->game) + 3;
  attack_impl(c);
}
static void kick_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  if (c->weapons.fire_hit(c->weapons.ctx, entity, game, q2rm_v3(80, 0, -32), 15, 400)
      && entity->has_enemy && game->host->is_player(game->host->ctx, entity->enemy)) {
    qa_body_state_result body = qa_shared_body_table_read(game->host->bodies, entity->enemy);
    qa_owned_actor actor; bool has_actor = qa_session_actor_registry_resolve_owned(game->host->actors, entity->enemy, &actor);
    if (body.has_value && has_actor && body.value.velocity.z < 270) {
      qa_body_state written = body.value;
      written.velocity.z = 270;
      qa_shared_body_table_write(game->host->bodies, actor, written);
    }
  }
}
static void jump_now_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_impl(c, false); }
static void jump2_now_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_impl(c, true); }
static void wait_land_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->next_frame = q2rm_body(c->game, c->entity).ground || qa_q2rm_monster_jump_finished(c)
      ? c->entity->frame + 1 : c->entity->frame;
}
static void counter_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_body_state body = q2rm_body(game, entity);
  qa_vec3 direction = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward;
  qa_vec3 end = qa_q2m_project_flash_default(c, q2rm_v3(20, 0, 14));
  qa_trace_result trace = q2rm_trace(game, body.origin, end, NULL, &entity->actor.id, 3);
  q2rm_emit_effect(game, "q2:berserk-slam", trace.end, direction, 1, 0);
  qa_q2rm_slam_radius_damage(c, trace.end, 15, 250, 200);
}
static const qa_q2bm_sound_spec sight_spec = {"guncmdr/sight1.wav", 2, 1};
static const qa_q2bm_sound_spec search_spec = {"guncmdr/gcdrsrch1.wav", 2, 1};
static const qa_q2bm_sound_spec idle_spec = {"guncmdr/gcdridle1.wav", 2, 2};
static const qa_q2bm_sound_spec open_spec = {"guncmdr/gcdratck1.wav", 2, 2};

qa_q2m_definition qa_q2rm_create_gun_commander_definition(qa_q2mpm_arena *arena, qa_q2mpm_weapons weapons) {
  qa_q2mpm_weapons *box = qa_q2mpm_arena_alloc(arena, sizeof *box);
  if (!box) qa_raise("Error", 0, 0, "Out of memory creating gun commander definition");
  *box = weapons;
  qa_q2m_named_handler *callbacks = malloc(20 * sizeof *callbacks);
  if (!callbacks) qa_raise("Error", 0, 0, "Out of memory creating gun commander definition");
  callbacks[0] = (qa_q2m_named_handler){"guncmdr_stand", {(void *)"guncmdr_move_stand", qa_q2bm_move_handler_fn}};
  callbacks[1] = (qa_q2m_named_handler){"guncmdr_run", {NULL, run_fn}};
  callbacks[2] = (qa_q2m_named_handler){"guncmdr_idlesound", {(void *)&idle_spec, qa_q2bm_sound_handler_fn}};
  callbacks[3] = (qa_q2m_named_handler){"guncmdr_opengun", {(void *)&open_spec, qa_q2bm_sound_handler_fn}};
  callbacks[4] = (qa_q2m_named_handler){"guncmdr_fidget", {NULL, fidget_fn}};
  callbacks[5] = (qa_q2m_named_handler){"guncmdr_pain5_to_death1", {NULL, pain5_death1_fn}};
  callbacks[6] = (qa_q2m_named_handler){"guncmdr_pain5_to_death2", {NULL, pain5_death2_fn}};
  callbacks[7] = (qa_q2m_named_handler){"guncmdr_pain6_to_death6", {NULL, pain6_death6_fn}};
  callbacks[8] = (qa_q2m_named_handler){"guncmdr_dead", {NULL, dead_fn}};
  callbacks[9] = (qa_q2m_named_handler){"guncmdr_shrink", {NULL, shrink_fn}};
  callbacks[10] = (qa_q2m_named_handler){"guncmdr_fire_chain", {NULL, fire_chain_fn}};
  callbacks[11] = (qa_q2m_named_handler){"guncmdr_refire_chain", {NULL, refire_fn}};
  callbacks[12] = (qa_q2m_named_handler){"GunnerCmdrFire", {box, cmdr_fire_fn}};
  callbacks[13] = (qa_q2m_named_handler){"GunnerCmdrGrenade", {box, cmdr_grenade_fn}};
  callbacks[14] = (qa_q2m_named_handler){"guncmdr_grenade_mortar_resume", {NULL, mortar_resume_fn}};
  callbacks[15] = (qa_q2m_named_handler){"guncmdr_grenade_back_dodge_resume", {NULL, back_resume_fn}};
  callbacks[16] = (qa_q2m_named_handler){"guncmdr_kick_finished", {NULL, kick_finished_fn}};
  callbacks[17] = (qa_q2m_named_handler){"guncmdr_kick", {NULL, kick_fn}};
  callbacks[18] = (qa_q2m_named_handler){"guncmdr_jump_now", {NULL, jump_now_fn}};
  callbacks[19] = (qa_q2m_named_handler){"guncmdr_jump2_now", {NULL, jump2_now_fn}};
  const qa_q2m_named_handler extra[2] = {
    {"guncmdr_jump_wait_land", {NULL, wait_land_fn}},
    {"GunnerCmdrCounter", {NULL, counter_fn}},
  };
  size_t callback_count = 0;
  qa_q2m_named_handler *merged = qa_q2rm_merge_callbacks(arena, callbacks, 20, extra, 2, &callback_count);
  free(callbacks);
  qa_q2m_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.classname = "monster_guncmdr";
  definition.kind = "guncmdr";
  definition.model = "models/monsters/gunner/tris.md2";
  definition.health = 325; definition.gib_health = -175; definition.mass = 255;
  definition.scale = (float)1.15;
  definition.bounds.min = q2rm_v3(-16, -16, -24);
  definition.bounds.max = q2rm_v3(16, 16, 36);
  definition.initial_move = "guncmdr_move_stand";
  definition.moves = qa_q2rm_guncmdr_moves;
  definition.move_count = qa_q2rm_guncmdr_move_count;
  definition.callbacks = merged;
  definition.callback_count = callback_count;
  definition.stand = qa_q2bm_move_handler("guncmdr_move_stand");
  definition.walk = qa_q2bm_move_handler("guncmdr_move_walk");
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.attack = (qa_q2m_handler){NULL, attack_fn};
  definition.sidestep_ctx = NULL; definition.sidestep = sidestep_fn;
  definition.sight = qa_q2bm_sound_handler(&sight_spec);
  definition.search = qa_q2bm_sound_handler(&search_spec);
  definition.initialize = (qa_q2m_handler){NULL, init_fn};
  definition.restore = (qa_q2m_handler){NULL, qa_q2rm_bind_armor_handler_200};
  definition.duck_ctx = NULL; definition.duck = duck_fn;
  definition.blocked_ctx = NULL; definition.blocked = blocked_fn;
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  return definition;
}
