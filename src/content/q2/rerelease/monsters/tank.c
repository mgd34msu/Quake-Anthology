/* Port of src/content/q2/rerelease/monsters/tank.ts.
 * Rerelease m_tank.cpp, including the Quake II 64 commander.
 * ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/tank.h"

#include <stdlib.h>

#include "qa/content/q2/base/monsters/tank.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/tank.h"
#include "rrm_internal.h"

static bool blind_aim(qa_q2m_context *c, qa_vec3 start, qa_vec3 target, qa_vec3 right, qa_vec3 *out) {
  static const double sides[3] = {0, -20, 20};
  for (int i = 0; i < 3; i++) {
    qa_vec3 end = qa_add3_double(target, qa_scale3_double(right, sides[i]));
    qa_trace_result trace = q2rm_trace(c->game, start, end, NULL, &c->entity->actor.id, (double)0x46004003u);
    if (!trace.start_solid && !trace.all_solid && trace.fraction >= 0.5) {
      *out = qa_normalize3_double_hypot(qa_sub3_double(end, start));
      return true;
    }
  }
  return false;
}
static void init_impl(qa_q2m_context *c) {
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  if (q2rm_has(entity->spawnflags, 8)) {
    entity->max_health = 1500 * qa_q2_number_field(&entity->spawn, "health_multiplier", 1);
    qa_gameplay_authority_set_health(game->host->combat, entity->actor, entity->max_health);
    if (qa_q2_number_field(&entity->spawn, "scale", 0) == 0) {
      entity->scale = 1.5;
      state->scale = 1.5;
      qa_q2_body_changes change;
      memset(&change, 0, sizeof change);
      change.has_bounds = true;
      change.bounds.min = q2rm_v3(-48, -48, -24);
      change.bounds.max = q2rm_v3(48, 48, 108);
      q2rm_move_body(game, entity, change, false, false);
      state->normal_height = 108;
      entity->view_height = 100;
      q2rm_set_mass(game, entity, 750);
    }
  }
}
static void init_fn(void *ctx, qa_q2m_context *c) { (void)ctx; init_impl(c); }
static void commander_init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  init_impl(c);
  c->entity->skin = 2;
  c->entity->count = 1;
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  if (qa_q2m_health(game, entity->has_enemy ? &entity->enemy : NULL) <= 0) {
    state->brutal = false;
    q2rm_set_move(c, "tank_move_attack_strike");
    return;
  }
  if (state->attack_state == QA_Q2M_ATTACK_BLIND) {
    double chance = state->blind_fire_delay < 1 ? 1 : state->blind_fire_delay < 7.5 ? 0.4 : 0.1;
    double choice = q2rm_rnd(game);
    state->blind_fire_delay += 5.2 + q2rm_rnd(game) * 3;
    if (qa_hypot3(state->blind_fire_target) == 0 || choice > chance) return;
    bool rocket = qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, 23));
    bool blaster = qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, 1));
    if (!rocket && !blaster) return;
    state->manual_steering = true;
    if (rocket && blaster ? q2rm_rnd(game) < 0.5 : rocket) q2rm_set_move(c, "tank_move_attack_fire_rocket");
    else {
      q2rm_set_move(c, "tank_move_attack_blast");
      state->next_frame = qa_q2rm_tank_frames_value.attak108;
    }
    double left = q2rm_now(game) + 3;
    double right = q2rm_rnd(game) * 2;
    state->attack_finished = left + right;
    state->pain_time = q2rm_now(game) + 5;
    return;
  }
  double range = qa_hypot3(qa_sub3_double(enemy.origin, q2rm_body(game, entity).origin));
  double choice = q2rm_rnd(game);
  if (range <= 250) {
    qa_q2_entity *enemy_entity = game->entity(game->ctx, entity->has_enemy ? &entity->enemy : NULL);
    bool machinegun = (!enemy_entity || strcmp(enemy_entity->classname, "tesla_mine") != 0)
        && qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, 8));
    if (machinegun && choice < (range <= 125 ? 0.5 : 0.25)) { q2rm_set_move(c, "tank_move_attack_chain"); return; }
  } else {
    bool machinegun = qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, 8));
    bool rocket = qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, 23));
    if (machinegun && choice < 0.33) { q2rm_set_move(c, "tank_move_attack_chain"); return; }
    if (rocket && choice < 0.66) {
      state->pain_time = q2rm_now(game) + 5;
      q2rm_set_move(c, "tank_move_attack_pre_rocket");
      return;
    }
  }
  if (qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, 1))) q2rm_set_move(c, "tank_move_attack_blast");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  const qa_q2rm_tank_frames *frame = &qa_q2rm_tank_frames_value;
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2
      ? q2rm_or(entity->skin, 1) : q2rm_and(entity->skin, ~1);
  if ((!qa_q2rm_chainfist(c) && reaction.damage <= 10) || q2rm_now(game) < state->pain_time) return;
  if (!qa_q2rm_chainfist(c)) {
    if (reaction.damage <= 30 && q2rm_rnd(game) > 0.2) return;
    if ((entity->frame >= frame->attak301 && entity->frame <= frame->attak330)
        || (entity->frame >= frame->attak101 && entity->frame <= frame->attak116)) return;
  }
  state->pain_time = q2rm_now(game) + 3;
  q2rm_snd(c, entity->count != 0 ? "tank/pain.wav" : "tank/tnkpain2.wav", 2);
  if (!qa_q2rm_reacts_to_pain(c)) return;
  state->manual_steering = false;
  q2rm_set_move(c, reaction.damage <= 30 ? "tank_move_pain1" : reaction.damage <= 60 ? "tank_move_pain2" : "tank_move_pain3");
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
    qa_q2m_gib_options metal = {false, true, false, false};
    qa_q2m_gib_options skin_metal = {false, true, true, false};
    qa_q2m_gib_options skin = {false, false, true, false};
    qa_q2m_gib_options skin_head = {true, false, true, false};
    qa_q2m_gib_options skin_up = {false, false, true, true};
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
    for (int i = 0; i < 3; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_metal/tris.md2", damage, metal);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/gear/tris.md2", damage, metal);
    static const char *const limbs[2] = {"models/monsters/tank/gibs/foot.md2", "models/monsters/tank/gibs/thigh.md2"};
    for (int p = 0; p < 2; p++)
      for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, limbs[p], damage, skin_metal);
    (void)qa_q2m_throw_gib(entity, game, "models/monsters/tank/gibs/chest.md2", damage, skin);
    (void)qa_q2m_throw_gib(entity, game, "models/monsters/tank/gibs/head.md2", damage, skin_head);
    if (entity->style == 0) (void)qa_q2m_throw_gib(entity, game, "models/monsters/tank/gibs/barm.md2", damage, skin_up);
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  if (entity->style == 0) {
    entity->style = 1;
    qa_body_state body = q2rm_body(game, entity);
    qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
    qa_q2m_gib_options skin_up = {false, false, true, true};
    qa_q2_entity *arm = qa_q2m_throw_gib(entity, game, "models/monsters/tank/gibs/barm.md2", damage, skin_up);
    if (arm) {
      double x = (q2rm_rnd(game) * 2 - 1) * 15;
      double y = (q2rm_rnd(game) * 2 - 1) * 15;
      arm->angular_velocity = q2rm_v3(x, y, 180);
      arm->skin = trunc(arm->skin / 2);
      qa_vec3 origin = qa_add3_double(body.origin,
          qa_add3_double(qa_scale3_double(axes.right, -16), qa_scale3_double(axes.up, 23)));
      qa_vec3 velocity = qa_add3_double(qa_scale3_double(axes.up, 100), qa_scale3_double(axes.right, -120));
      qa_vec3 angles = body.angles;
      angles.z = -90;
      qa_q2_body_changes change;
      memset(&change, 0, sizeof change);
      change.has_origin = true; change.origin = origin;
      change.has_velocity = true; change.velocity = velocity;
      change.has_angles = true; change.angles = angles;
      q2rm_move_body(game, arm, change, false, false);
    }
  }
  q2rm_snd(c, "tank/death.wav", 2);
  state->dead = true; state->can_take_damage = true;
  q2rm_set_damageable(game, entity, true);
  q2rm_set_move(c, "tank_move_death");
}
static void blind_check_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->manual_steering)
    c->state->ideal_yaw = qa_vector_to_angles_with_rule(
        qa_sub3_double(c->state->blind_fire_target, q2rm_body(c->game, c->entity).origin),
        QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_corpse(c);
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2rm_v3(-16, -16, -16);
  change.bounds.max = q2rm_v3(16, 16, 0);
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void shrink_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->server_flags = q2rm_or(c->entity->server_flags, 2);
  qa_bounds bounds = q2rm_body(c->game, c->entity).bounds;
  bounds.max.z = 0;
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true; change.bounds = bounds;
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void reattack_blaster_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->manual_steering) {
    c->state->manual_steering = false;
    q2rm_set_move(c, "tank_move_attack_post_blast");
    return;
  }
  q2rm_set_move(c, qa_q2m_visible_enemy(c)
      && qa_q2m_health(c->game, c->entity->has_enemy ? &c->entity->enemy : NULL) > 0
      && q2rm_rnd(c->game) <= 0.6 ? "tank_move_reattack_blast" : "tank_move_attack_post_blast");
}
static void refire_rocket_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->manual_steering) {
    c->state->manual_steering = false;
    q2rm_set_move(c, "tank_move_attack_post_rocket");
    return;
  }
  q2rm_set_move(c, qa_q2m_health(c->game, c->entity->has_enemy ? &c->entity->enemy : NULL) > 0
      && qa_q2m_visible_enemy(c) && q2rm_rnd(c->game) <= 0.4
      ? "tank_move_attack_fire_rocket" : "tank_move_attack_post_rocket");
}
static void blaster_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  const qa_q2rm_tank_frames *frame = &qa_q2rm_tank_frames_value;
  double id = entity->frame == frame->attak110 ? 1 : entity->frame == frame->attak113 ? 2 : 3;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, id));
  qa_vec3 direction;
  bool have;
  if (c->state->manual_steering) {
    qa_vec3 right = qa_math_angle_vectors_rule(q2rm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).right;
    have = blind_aim(c, start, c->state->blind_fire_target, right, &direction);
  } else have = qa_q2rm_predicted_direction_default(c, start, 0, false, &direction);
  if (!have) return;
  (void)c->weapons.fire_blaster(c->weapons.ctx, entity, game, start, direction, 30, 800, 8, false, false, 0);
  qa_q2rm_monster_flash(c, id, start, direction);
}
static void rocket_fn(void *ctx, qa_q2m_context *c) {
  qa_q2mpm_weapons *weapons = ctx;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  const qa_q2rm_tank_frames *frame = &qa_q2rm_tank_frames_value;
  double id = entity->frame == frame->attak324 ? 23 : entity->frame == frame->attak327 ? 24 : 25;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, id));
  bool heat = q2rm_has(entity->spawnflags, 16);
  double speed = entity->speed != 0 ? entity->speed : heat ? 500 : 650;
  qa_vec3 target;
  if (state->manual_steering) target = state->blind_fire_target;
  else if (q2rm_rnd(game) < 0.66 || start.z < enemy.origin.z + enemy.bounds.min.z) {
    qa_vec3 eye;
    target = qa_q2m_enemy_eye(c, &eye) ? eye : enemy.origin;
  } else {
    target = enemy.origin;
    target.z = enemy.origin.z + enemy.bounds.min.z + 1;
  }
  if (!state->manual_steering && q2rm_rnd(game) < 0.2 + (3 - game->options.skill) * 0.15) {
    qa_q2rm_predicted_aim aim;
    if (qa_q2rm_predict_aim_default(c, start, speed, false, &aim)) target = aim.point;
  }
  qa_vec3 direction;
  bool have;
  if (state->manual_steering) {
    qa_vec3 right = qa_math_angle_vectors_rule(q2rm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).right;
    have = blind_aim(c, start, target, right, &direction);
  } else {
    direction = qa_normalize3_double_hypot(qa_sub3_double(target, start));
    have = true;
  }
  if (!have) return;
  if (!state->manual_steering) {
    qa_trace_result trace = q2rm_trace(game, start, target, NULL, &entity->actor.id, (double)0x46004003u);
    if (trace.fraction <= 0.5 && trace.hit.kind == QA_TRACE_HIT_WORLD) return;
  }
  if (heat)
    (void)weapons->fire_heat_rocket(weapons->ctx, entity, game, start, direction, 50, speed, 70, 50,
                                    true, entity->accel != 0 ? entity->accel : 0.075);
  else
    (void)c->weapons.fire_rocket(c->weapons.ctx, entity, game, start, direction, 50, speed, 70, 50);
  qa_q2rm_monster_flash(c, id, start, direction);
}
static void machine_gun_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 eye;
  if (!qa_q2m_enemy_eye(c, &eye)) return;
  qa_q2_entity *entity = c->entity;
  const qa_q2rm_tank_frames *frame = &qa_q2rm_tank_frames_value;
  double id = 4 + entity->frame - frame->attak406;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2_EDITION_RERELEASE, id));
  qa_vec3 angles = q2rm_body(c->game, entity).angles;
  qa_vec3 aim_angles = {
    qa_vector_to_angles_with_rule(qa_sub3_double(eye, start), QA_VECTOR_TO_ANGLES_NATIVE_AI).x,
    entity->frame <= frame->attak415 ? angles.y - 8 * (entity->frame - frame->attak411)
                                     : angles.y + 8 * (entity->frame - frame->attak419), 0};
  qa_vec3 direction = qa_math_angle_vectors_rule(aim_angles, QA_ANGLE_RULE_NATIVE).forward;
  c->weapons.fire_bullet(c->weapons.ctx, entity, c->game, start, direction, 20, 4, 300, 500, 0);
  qa_q2rm_monster_flash(c, id, start, direction);
}

size_t qa_q2rm_create_rerelease_tank_definitions(qa_q2mpm_arena *arena, qa_q2mpm_weapons weapons, qa_q2m_definition out[2]) {
  qa_q2mpm_weapons *box = qa_q2mpm_arena_alloc(arena, sizeof *box);
  if (!box) qa_raise("Error", 0, 0, "Out of memory creating rerelease tank definitions");
  *box = weapons;
  const qa_q2m_named_handler extra[] = {
    {"tank_blind_check", {NULL, blind_check_fn}},
    {"tank_dead", {NULL, dead_fn}},
    {"tank_shrink", {NULL, shrink_fn}},
    {"tank_reattack_blaster", {NULL, reattack_blaster_fn}},
    {"tank_refire_rocket", {NULL, refire_rocket_fn}},
    {"TankBlaster", {NULL, blaster_fn}},
    {"TankRocket", {box, rocket_fn}},
    {"TankMachineGun", {NULL, machine_gun_fn}},
  };
  qa_q2m_definition tank = qa_q2bm_tank_definition;
  tank.moves = qa_q2rm_tank_moves;
  tank.move_count = qa_q2rm_tank_move_count;
  tank.has_blind_fire_flag = true;
  tank.blind_fire = true;
  tank.initialize = (qa_q2m_handler){NULL, init_fn};
  tank.blocked_ctx = NULL;
  tank.blocked = qa_q2rm_blocked_check_platform_handler;
  tank.attack = (qa_q2m_handler){NULL, attack_fn};
  tank.reactions.pain = pain_fn;
  tank.reactions.die = die_fn;
  tank.callbacks = qa_q2rm_merge_callbacks(arena, tank.callbacks, tank.callback_count,
                                           extra, sizeof extra / sizeof extra[0], &tank.callback_count);
  out[0] = tank;
  out[1] = tank;
  out[1].classname = "monster_tank_commander";
  out[1].health = 1000;
  out[1].gib_health = -225;
  out[1].initialize = (qa_q2m_handler){NULL, commander_init_fn};
  return 2;
}

static void stand_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)ctx;
  qa_q2_think self = {NULL, stand_think_fn};
  entity->frame = entity->frame == qa_q2rm_tank_frames_value.stand30
      ? qa_q2rm_tank_frames_value.stand01 : entity->frame + 1;
  game->show(game->ctx, entity);
  game->schedule(game->ctx, entity, 0.1, self);
}
static void stand_use_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game,
                         const qa_actor_id *other, const qa_actor_id *activator) {
  (void)ctx; (void)other; (void)activator;
  q2rm_emit_effect(game, "q2:boss-teleport", q2rm_body(game, entity).origin, q2rm_zero(), 1, 0);
  game->remove(game->ctx, entity);
}
static qa_q2_callback_definitions stand_callbacks(void *ctx) {
  qa_q2rm_tank_stand_module *module = ctx;
  qa_q2_callback_definitions definitions;
  memset(&definitions, 0, sizeof definitions);
  definitions.think = module->think_defs; definitions.think_count = 1;
  definitions.use = module->use_defs; definitions.use_count = 1;
  return definitions;
}
static bool stand_spawn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  (void)ctx;
  if (strcmp(entity->classname, "monster_tank_stand") != 0) return false;
  if (game->options.mode == QA_Q2_MODE_DEATHMATCH) { game->remove(game->ctx, entity); return true; }
  double scale_field = qa_q2_number_field(&entity->spawn, "scale", 0);
  entity->scale = scale_field != 0 ? scale_field : 1.5;
  entity->model = "models/monsters/tank/tris.md2";
  entity->frame = qa_q2rm_tank_frames_value.stand01;
  entity->skin = 2;
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = qa_scale3_double(q2rm_v3(-32, -32, -16), entity->scale);
  change.bounds.max = qa_scale3_double(q2rm_v3(32, 32, 64), entity->scale);
  q2rm_move_body(game, entity, change, false, false);
  game->motion(game->ctx, entity, "step");
  game->solid(game->ctx, entity, "box");
  game->show(game->ctx, entity);
  entity->use.ctx = NULL;
  entity->use.fn = stand_use_fn;
  qa_q2_think think = {NULL, stand_think_fn};
  game->schedule(game->ctx, entity, 0.1, think);
  return true;
}
void qa_q2rm_create_tank_stand_module(qa_q2rm_tank_stand_module *out) {
  memset(out, 0, sizeof *out);
  out->think_defs[0].name = "rerelease.tank_stand.Think_TankStand";
  out->think_defs[0].cb.ctx = NULL;
  out->think_defs[0].cb.fn = stand_think_fn;
  out->use_defs[0].name = "rerelease.tank_stand.Use_Boss3";
  out->use_defs[0].cb.ctx = NULL;
  out->use_defs[0].cb.fn = stand_use_fn;
}
qa_q2_spawn_module qa_q2rm_tank_stand_as_spawn(qa_q2rm_tank_stand_module *module) {
  module->spawn_callbacks = stand_callbacks(module);
  qa_q2_spawn_module spawn = {module, &module->spawn_callbacks, NULL, stand_spawn};
  return spawn;
}
