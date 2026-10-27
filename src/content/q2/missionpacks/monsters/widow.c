/* Port of src/content/q2/missionpacks/monsters/widow.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/widow.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/missionpacks/monsters/power_armor.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/spawn.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_widow.h"
#include "qa/content/q2/missionpacks/monsters/widow/common.h"
#include "qa/content/q2/missionpacks/monsters/widow/death.h"

typedef struct widow_bundle {
  qa_q2m_monsters *monsters;
  qa_q2mpm_weapons weapons;
  qa_q2mpm_services services;
  qa_q2mpm_monster_state *source;
  qa_q2f_think power_think;
} widow_bundle;
static const double sweep_angles[] = {32, 26, 20, 10, 0, -6.5, -13, -27, -41};
static double target_angle(qa_q2m_context *c) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return 0;
  qa_body_state body = q2mpm_body(c->game, c->entity);
  double angle = body.angles.y - qa_vector_to_angles_with_rule(qa_sub3_double(body.origin, enemy.origin),
                                                               QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  if (angle < 0) angle += 360;
  return angle - 180;
}
static double torso_frame(qa_q2m_context *c) {
  const qa_q2mpm_rogue_widow_frames *f = &qa_q2mpm_rogue_widow_frames_value;
  double angle = target_angle(c);
  if (angle >= 105 || angle <= -75) {
    q2mpm_set_move(c, angle >= 105 ? "widow_move_attack_post_blaster_r" : "widow_move_attack_post_blaster_l");
    c->state->manual_steering = false;
    return 0;
  }
  for (double index = 0; index < 17; index++)
    if (angle >= 95 - index * 10) return f->fired03 + index;
  return f->fired20;
}
static void run_move(qa_q2m_context *c) {
  c->state->hold_frame = false;
  q2mpm_set_move(c, c->state->stand_ground ? "widow_move_stand" : "widow_move_run");
}
static void run_fn(void *ctx, qa_q2m_context *c) { (void)ctx; run_move(c); }
static void rail_move(qa_q2m_context *c) {
  q2mpm_snd(c, "gladiator/railgun.wav", 1);
  q2mpm_set_move(c, "widow_move_attack_pre_rail");
}
static void blaster(widow_bundle *bundle, qa_q2m_context *c) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  const qa_q2mpm_rogue_widow_frames *f = &qa_q2mpm_rogue_widow_frames_value;
  qa_body_state enemy;
  qa_vec3 eye;
  if (!qa_q2m_enemy_body(c, &enemy) || !qa_q2m_enemy_eye(c, &eye)) return;
  bundle->source->widow_shots_fired++;
  double effect = fmod(bundle->source->widow_shots_fired, 4) == 0 ? 8 : 0;
  if (entity->frame >= f->spawn05 && entity->frame <= f->spawn13) {
    double index = entity->frame - f->spawn05;
    double flash = 156 + index;
    qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, flash));
    qa_body_state body = q2mpm_body(game, entity);
    qa_vec3 angles = body.angles;
    angles.x += qa_vector_to_angles_with_rule(qa_sub3_double(enemy.origin, start), QA_VECTOR_TO_ANGLES_NATIVE_AI).x;
    angles.y = body.angles.y - q2mpm_at_d(sweep_angles, 9, index);
    qa_vec3 direction = qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_NATIVE).forward;
    (void)bundle->weapons.fire_blaster2(bundle->weapons.ctx, entity, game, start, direction,
                                        10 * bundle->source->widow_damage_multiplier, 1000, effect);
    qa_q2mpm_monster_flash(c, flash, start, direction);
    return;
  }
  if (entity->frame >= f->fired02a && entity->frame <= f->fired20) {
    state->manual_steering = true;
    double torso = torso_frame(c);
    state->next_frame = torso != 0 ? torso : entity->frame;
    double flash = entity->frame == f->fired02a ? 175 : 165 + entity->frame - f->fired03;
    qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, flash));
    qa_vec3 aim;
    if (!qa_q2mpm_predicted_direction(c, start, 1000, true, q2mpm_rnd(game) * 0.1 - 0.05, &aim)) return;
    qa_vec3 angles = qa_vector_to_angles_with_rule(aim, QA_VECTOR_TO_ANGLES_NATIVE_AI);
    double yaw = q2mpm_body(game, entity).angles.y;
    double aim_angle = 100 - 10 * (flash - 165);
    if (aim_angle <= 0) aim_angle += 360;
    double enemy_angle = yaw - angles.y;
    if (enemy_angle <= 0) enemy_angle += 360;
    double error = aim_angle - enemy_angle;
    qa_vec3 shot_angles = angles;
    shot_angles.y = error > 15 ? yaw - aim_angle + 15 : error < -15 ? yaw - aim_angle - 15 : angles.y;
    qa_vec3 direction = qa_math_angle_vectors_rule(shot_angles, QA_ANGLE_RULE_NATIVE).forward;
    (void)bundle->weapons.fire_blaster2(bundle->weapons.ctx, entity, game, start, direction,
                                        10 * bundle->source->widow_damage_multiplier, 1000, effect);
    qa_q2mpm_monster_flash(c, flash, start, direction);
    return;
  }
  if (entity->frame >= f->run01 && entity->frame <= f->run08) {
    double flash = 183 + entity->frame - f->run01;
    qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, flash));
    qa_vec3 direction = qa_sub3_double(eye, start);
    (void)bundle->weapons.fire_blaster2(bundle->weapons.ctx, entity, game, start, direction,
                                        10 * bundle->source->widow_damage_multiplier, 1000, effect);
    qa_q2mpm_monster_flash(c, flash, start, direction);
  }
}
static void blaster_fn(void *ctx, qa_q2m_context *c) { blaster(ctx, c); }
static bool check_attack(widow_bundle *bundle, qa_q2m_context *c) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  const qa_q2mpm_rogue_widow_frames *f = &qa_q2mpm_rogue_widow_frames_value;
  qa_body_state enemy;
  qa_vec3 eye;
  if (!qa_q2m_enemy_body(c, &enemy) || !qa_q2m_enemy_eye(c, &eye) || !entity->has_enemy) return false;
  qa_q2mpm_widow_powerups(c, bundle->services, bundle->source);
  if (strcmp(state->move->name, "widow_move_run") == 0
      && ((entity->frame >= f->walk04 && entity->frame <= f->walk08) || entity->frame == f->walk12)) return false;
  double distance = qa_q2m_target_distance(c);
  if (q2mpm_rnd(game) < 0.8 && qa_q2mpm_widow_slots_left(c) >= 2 && distance > 150) {
    qa_q2mpm_state_get(bundle->source, entity)->blocked = true;
    state->attack_state = QA_Q2M_ATTACK_MISSILE;
    return true;
  }
  if (qa_q2m_health(game, &entity->enemy) > 0) {
    qa_vec3 origin = q2mpm_body(game, entity).origin;
    qa_vec3 start = origin;
    start.z += entity->view_height;
    qa_trace_result trace = q2mpm_trace(game, start, eye, NULL, &entity->actor.id,
                                        (double)(1u | 0x2000000u | 8u | 16u));
    if (trace.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(trace.hit.actor, entity->enemy)) {
      if (game->host->is_player(game->host->ctx, entity->enemy) && qa_q2mpm_widow_slots_left(c) >= 2) {
        state->attack_state = QA_Q2M_ATTACK_BLIND;
        return true;
      }
      qa_q2f_entity *enemy_entity = game->entity(game->ctx, &entity->enemy);
      if (!enemy_entity || enemy_entity->solid != QA_Q2F_SOLID_NONE || trace.fraction < 1) return false;
    }
  }
  state->ideal_yaw = qa_vector_to_angles_with_rule(qa_sub3_double(enemy.origin, q2mpm_body(game, entity).origin),
                                                   QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  if (distance <= 100) {
    if (game->options.skill == 0 && floor(q2mpm_rnd(game) * 4) != 0) return false;
    state->attack_state = QA_Q2M_ATTACK_MELEE;
    return true;
  }
  if (q2mpm_now(game) < state->attack_finished) return false;
  double chance = state->stand_ground ? 0.4 : distance < 80 ? 0.8 : distance < 500 ? 0.7 : distance < 1000 ? 0.6 : 0.5;
  qa_q2f_entity *enemy_entity = game->entity(game->ctx, &entity->enemy);
  if (q2mpm_rnd(game) < chance || (enemy_entity && enemy_entity->solid == QA_Q2F_SOLID_NONE)) {
    state->attack_state = QA_Q2M_ATTACK_MISSILE;
    return true;
  }
  return false;
}
static bool check_attack_fn(void *ctx, qa_q2m_context *c) { return check_attack(ctx, c); }
static void attack_move(widow_bundle *bundle, qa_q2m_context *c) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  const qa_q2mpm_rogue_widow_frames *f = &qa_q2mpm_rogue_widow_frames_value;
  qa_q2mpm_rogue_state *own = qa_q2mpm_state_get(bundle->source, entity);
  bool blocked = own->blocked;
  bool anger = state->target_anger;
  state->has_move_target = false;
  own->blocked = false;
  state->target_anger = false;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  if (bundle->services.bad_area(bundle->services.ctx, entity->actor.id)) {
    if (q2mpm_rnd(game) < 0.1 || q2mpm_now(game) < entity->timestamp) q2mpm_set_move(c, "widow_move_attack_pre_blaster");
    else rail_move(c);
    return;
  }
  bool rail_frames = entity->frame == f->walk13 || (entity->frame >= f->walk01 && entity->frame <= f->walk03);
  bool blaster_frames = entity->frame >= f->walk09 && entity->frame <= f->walk12;
  qa_q2mpm_widow_slots(c);
  if ((state->attack_state == QA_Q2M_ATTACK_BLIND || blocked) && qa_q2mpm_widow_slots_left(c) >= 2) {
    q2mpm_set_move(c, "widow_move_spawn");
    return;
  }
  if (qa_q2m_target_distance(c) > 300 && !anger && q2mpm_rnd(game) < 0.5 && !blocked) {
    q2mpm_set_move(c, "widow_move_run_attack");
    return;
  }
  if (blaster_frames) {
    if (qa_q2mpm_widow_slots_left(c) >= 2) { q2mpm_set_move(c, "widow_move_spawn"); return; }
    if (state->pause_time + 2 <= q2mpm_now(game)) { q2mpm_set_move(c, "widow_move_attack_pre_blaster"); return; }
  }
  if (rail_frames && q2mpm_now(game) >= entity->timestamp) { rail_move(c); return; }
  if (blaster_frames || rail_frames) return;
  double luck = q2mpm_rnd(game);
  if (qa_q2mpm_widow_slots_left(c) >= 2) {
    if (luck <= 0.4 && state->pause_time + 2 <= q2mpm_now(game)) {
      q2mpm_set_move(c, "widow_move_attack_pre_blaster");
      return;
    }
    if (luck <= 0.7 && q2mpm_now(game) >= entity->timestamp) { rail_move(c); return; }
    q2mpm_set_move(c, "widow_move_spawn");
    return;
  }
  if (q2mpm_now(game) < entity->timestamp) { q2mpm_set_move(c, "widow_move_attack_pre_blaster"); return; }
  if (luck <= 0.5 || q2mpm_now(game) + 2 >= state->pause_time) rail_move(c);
  else q2mpm_set_move(c, "widow_move_attack_pre_blaster");
}
static void attack_fn(void *ctx, qa_q2m_context *c) { attack_move(ctx, c); }
static void stand_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_snd(c, "widow/laugh.wav", 2);
  q2mpm_set_move(c, "widow_move_stand");
}
static void sight_fn(void *ctx, qa_q2m_context *c) { (void)ctx; c->state->pause_time = 0; }
static void init_fn(void *ctx, qa_q2m_context *c) {
  widow_bundle *bundle = ctx;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  entity->max_health = 2000 + 1000 * game->options.skill
      + (strcmp(game->options.mode, "coop") == 0 ? 500 * game->options.skill : 0);
  game->host->combat.set_health(game->host->combat.ctx, entity->actor, entity->max_health);
  entity->laser_immune = true;
  c->state->ignore_shots = true;
  entity->prethink = bundle->power_think;
  qa_q2mpm_widow_slots(c);
  bundle->source->widow_damage_multiplier = 1;
  if (game->options.skill == 3) qa_q2mpm_monster_power_armor(c, "shield", 500);
}
static bool blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  widow_bundle *bundle = ctx;
  (void)distance;
  if (strcmp(c->state->move->name, "widow_move_run_attack") == 0) {
    c->state->target_anger = true;
    if (check_attack(bundle, c)) attack_move(bundle, c);
    else run_move(c);
    return true;
  }
  return qa_q2mpm_rogue_blocked_check_shot(c, 0.25 + 0.05 * c->game->options.skill, bundle->source);
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2bm_damaged_skin(c);
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  double skill = game->options.skill;
  if (skill == 3 || q2mpm_now(game) < state->pain_time) return;
  if (state->pause_time == 100000000) state->pause_time = 0;
  state->pain_time = q2mpm_now(game) + 5;
  if (reaction.damage < 15) { q2mpm_snd4(c, "widow/bw1pain1.wav", 2, 1, 0); return; }
  if (q2mpm_rnd(game) < (reaction.damage < 75 ? 0.6 - 0.2 * skill : 0.75 - 0.1 * skill)) {
    state->manual_steering = false;
    q2mpm_set_move(c, reaction.damage < 75 ? "widow_move_pain_light" : "widow_move_pain_heavy");
  }
  (void)entity;
  q2mpm_snd4(c, reaction.damage < 75 ? "widow/bw1pain2.wav" : "widow/bw1pain3.wav", 2, 1, 0);
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  widow_bundle *bundle = ctx;
  (void)reaction;
  c->state->dead = true;
  c->state->can_take_damage = false;
  q2mpm_set_damageable(c->game, c->entity, false);
  c->entity->count = 0;
  qa_q2mpm_widow_clear_powerups(c, bundle->source);
  q2mpm_set_move(c, "widow_move_death");
}
static void step_fn(void *ctx, qa_q2m_context *c) { (void)ctx; q2mpm_snd(c, "widow/bwstep3.wav", 4); }
static void stepshoot_fn(void *ctx, qa_q2m_context *c) {
  q2mpm_snd(c, "widow/bwstep3.wav", 4);
  blaster(ctx, c);
}
static void start_run_5_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_set_move(c, "widow_move_run");
  c->state->next_frame = qa_q2mpm_rogue_widow_frames_value.walk05;
}
static void start_run_10_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_set_move(c, "widow_move_run");
  c->state->next_frame = qa_q2mpm_rogue_widow_frames_value.walk10;
}
static void start_run_12_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_set_move(c, "widow_move_run");
  c->state->next_frame = qa_q2mpm_rogue_widow_frames_value.walk12;
}
static void attack_blaster_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->pause_time = q2mpm_now(c->game) + 1 + 2 * q2mpm_rnd(c->game);
  q2mpm_set_move(c, "widow_move_attack_blaster");
  c->state->next_frame = torso_frame(c);
}
static void reattack_blaster_fn(void *ctx, qa_q2m_context *c) {
  blaster(ctx, c);
  const char *name = c->state->move->name;
  if (strcmp(name, "widow_move_attack_post_blaster_r") == 0 || strcmp(name, "widow_move_attack_post_blaster_l") == 0
      || c->state->pause_time >= q2mpm_now(c->game)) return;
  c->state->manual_steering = false;
  q2mpm_set_move(c, "widow_move_attack_post_blaster");
}
static void save_loc_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 eye;
  if (qa_q2m_enemy_eye(c, &eye)) c->entity->pos1 = eye;
}
static void rail_fn(void *ctx, qa_q2m_context *c) {
  widow_bundle *bundle = ctx;
  const char *name = c->state->move->name;
  double flash = strcmp(name, "widow_move_attack_rail_l") == 0 ? 154
      : strcmp(name, "widow_move_attack_rail_r") == 0 ? 155 : 150;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(c->game->options.edition, flash));
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(c->entity->pos1, start));
  c->weapons.fire_rail(c->weapons.ctx, c->entity, c->game, start, direction,
                       50 * bundle->source->widow_damage_multiplier, 100);
  qa_q2mpm_monster_flash(c, flash, start, direction);
  c->entity->timestamp = q2mpm_now(c->game) + 3;
}
static void start_rail_fn(void *ctx, qa_q2m_context *c) { (void)ctx; c->state->manual_steering = true; }
static void rail_done_fn(void *ctx, qa_q2m_context *c) { (void)ctx; c->state->manual_steering = false; }
static void attack_rail_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double angle = target_angle(c);
  q2mpm_set_move(c, angle < -15 ? "widow_move_attack_rail_l" : angle > 15 ? "widow_move_attack_rail_r"
                                                                            : "widow_move_attack_rail");
}
static void ready_spawn_fn(void *ctx, qa_q2m_context *c) {
  widow_bundle *bundle = ctx;
  blaster(bundle, c);
  qa_q2mpm_widow_summon(c, bundle->monsters, false, true);
}
static void spawn_check_fn(void *ctx, qa_q2m_context *c) {
  widow_bundle *bundle = ctx;
  blaster(bundle, c);
  qa_q2mpm_widow_summon(c, bundle->monsters, false, false);
}
static void kick_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state enemy;
  bool has_enemy = qa_q2m_enemy_body(c, &enemy);
  (void)c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, q2mpm_v3(100, 0, 4),
                            50 + floor(q2mpm_rnd(c->game) * 6), !has_enemy || enemy.ground == NULL ? 250 : 500);
}
static void spawn_out_start_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->wait = q2mpm_now(c->game) + 2;
  qa_q2mpm_widow_effect(c->game, qa_q2mpm_widow_project(c->entity, c->game, q2mpm_v3(12.58, -43.71, 68.88)),
                        "q2:widowbeamout", 20001);
  qa_q2mpm_widow_effect(c->game, qa_q2mpm_widow_project(c->entity, c->game, q2mpm_v3(3.43, 58.72, 68.41)),
                        "q2:widowbeamout", 20002);
  q2mpm_snd(c, "misc/bwidowbeamout.wav", 2);
}
static void spawn_out_do_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2mpm_widow_effect(c->game, qa_q2mpm_widow_project(c->entity, c->game, q2mpm_v3(12.58, -43.71, 68.88)),
                        "q2:widowsplash", 1);
  qa_q2mpm_widow_effect(c->game, qa_q2mpm_widow_project(c->entity, c->game, q2mpm_v3(3.43, 58.72, 68.41)),
                        "q2:widowsplash", 1);
  qa_vec3 origin = q2mpm_body(c->game, c->entity).origin;
  origin.z += 36;
  qa_q2mpm_widow_effect(c->game, origin, "q2:bosstport", 1);
  qa_q2mpm_spawn_widow_legs(c->entity, c->game);
  c->game->remove(c->game->ctx, c->entity);
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = {{-56, -56, 0}, {56, 56, 80}};
  qa_q2bm_finish_corpse_bounds(c, bounds);
}
qa_q2m_definition qa_q2mpm_create_widow_definition(qa_q2m_monsters *monsters, qa_q2mpm_weapons weapons,
                                                   qa_q2mpm_services services, qa_q2mpm_monster_state *source) {
  widow_bundle *bundle = malloc(sizeof *bundle);
  if (!bundle) qa_q2f_error("Out of memory creating widow definition");
  bundle->monsters = monsters;
  bundle->weapons = weapons;
  bundle->services = services;
  bundle->source = source;
  bundle->power_think = qa_q2mpm_widow_power_think(monsters, source);
  qa_q2m_named_handler *callbacks = malloc(23 * sizeof *callbacks);
  if (!callbacks) qa_q2f_error("Out of memory creating widow definition");
  size_t n = 0;
  callbacks[n++] = (qa_q2m_named_handler){"widow_run", {NULL, run_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"WidowBlaster", {bundle, blaster_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_step", {NULL, step_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_stepshoot", {bundle, stepshoot_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_start_run_5", {NULL, start_run_5_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_start_run_10", {NULL, start_run_10_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_start_run_12", {NULL, start_run_12_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_attack_blaster", {NULL, attack_blaster_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_reattack_blaster", {bundle, reattack_blaster_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"WidowSaveLoc", {NULL, save_loc_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"WidowRail", {bundle, rail_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_start_rail", {NULL, start_rail_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_rail_done", {NULL, rail_done_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_attack_rail", {NULL, attack_rail_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_start_spawn", {NULL, start_rail_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_done_spawn", {NULL, rail_done_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_ready_spawn", {bundle, ready_spawn_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_spawn_check", {bundle, spawn_check_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_attack_kick", {NULL, kick_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"spawn_out_start", {NULL, spawn_out_start_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"spawn_out_do", {NULL, spawn_out_do_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_dead", {NULL, dead_fn}};
  qa_q2f_callback_definitions debris = qa_q2mpm_widow_debris_callbacks();
  qa_q2f_callback_definitions spawn = qa_q2mpm_rogue_spawn_callbacks();
  size_t think_count = debris.think_count + spawn.think_count + 1;
  qa_q2f_named_think *thinks = malloc(think_count * sizeof *thinks);
  if (!thinks) qa_q2f_error("Out of memory creating widow definition");
  size_t at = 0;
  for (size_t i = 0; i < debris.think_count; i++) thinks[at++] = debris.think[i];
  for (size_t i = 0; i < spawn.think_count; i++) thinks[at++] = spawn.think[i];
  thinks[at++] = (qa_q2f_named_think){"q2:rogue/widow_powerups", bundle->power_think};
  qa_q2m_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.classname = "monster_widow";
  definition.kind = "widow";
  definition.model = "models/monsters/blackwidow/tris.md2";
  definition.health = 2000;
  definition.gib_health = -5000;
  definition.mass = 1500;
  definition.bounds.min = q2mpm_v3(-40, -40, 0);
  definition.bounds.max = q2mpm_v3(40, 40, 144);
  definition.scale = 1;
  definition.has_yaw_speed = true;
  definition.yaw_speed = 30;
  definition.initial_move = "widow_move_stand";
  definition.moves = qa_q2mpm_rogue_widow_moves;
  definition.move_count = qa_q2mpm_rogue_widow_move_count;
  definition.callbacks = callbacks;
  definition.callback_count = n;
  definition.has_source_callbacks = true;
  definition.source_callbacks = debris;
  definition.source_callbacks.think = thinks;
  definition.source_callbacks.think_count = at;
  definition.stand = (qa_q2m_handler){NULL, stand_fn};
  definition.walk = qa_q2bm_move_handler("widow_move_walk");
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.sight = (qa_q2m_handler){NULL, sight_fn};
  definition.melee = qa_q2bm_move_handler("widow_move_attack_kick");
  definition.attack = (qa_q2m_handler){bundle, attack_fn};
  definition.initialize = (qa_q2m_handler){bundle, init_fn};
  definition.restore = (qa_q2m_handler){NULL, qa_q2mpm_widow_restore_armor_handler};
  definition.reactions.ctx = bundle;
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.blocked_ctx = bundle;
  definition.blocked = blocked_fn;
  definition.check_attack_ctx = bundle;
  definition.check_attack = check_attack_fn;
  return definition;
}
