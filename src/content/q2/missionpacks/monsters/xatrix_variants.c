/* Port of src/content/q2/missionpacks/monsters/xatrix-variants.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/xatrix_variants.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/boss_common.h"
#include "qa/content/q2/base/monsters/brain.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/supertank.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/infantry.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/missionpacks/monsters/dabeam.h"
#include "qa/content/q2/missionpacks/monsters/power_armor.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_brain.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_infantry.h"

const qa_vec3 qa_q2mpm_xatrix_brain_right_eye[11] = {
  {0.7467, 0.23837, 34.16769}, {-1.07639, 0.23837, 33.386372}, {-1.3355, 5.3343, 32.17717},
  {-0.17536, 8.84637, 30.635479}, {-2.75759, 7.80461, 30.15086}, {-5.57509, 5.15284, 30.05616},
  {-7.01755, 3.26247, 30.552521}, {-7.91574, 0.6388, 33.176189}, {-3.91539, 8.28573, 33.976349},
  {-0.91354, 10.93303, 34.141811}, {-0.3699, 8.9239, 34.189079},
};
const qa_vec3 qa_q2mpm_xatrix_brain_left_eye[11] = {
  {-3.36471, 0.32775, 33.938381}, {-5.14045, 0.49348, 32.659851}, {-5.34198, 5.64698, 31.277901},
  {-4.13448, 9.27744, 29.925621}, {-6.59834, 6.81509, 29.32262}, {-8.61084, 2.52965, 29.251591},
  {-9.23136, 0.09328, 29.747959}, {-11.00411, 1.93693, 32.39526}, {-7.87831, 7.64819, 33.148151},
  {-4.94737, 11.43005, 33.31361}, {-4.33282, 9.44457, 33.52634},
};
static bool tongue_allowed(qa_vec3 start, qa_vec3 end) {
  qa_vec3 direction = qa_sub3_double(start, end);
  if (qa_hypot3(direction) > 512) return false;
  double pitch = qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_NATIVE_AI).x;
  if (pitch < -180) pitch += 360;
  return fabs(pitch) <= 30;
}
static void brain_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2mpm_rnd(c->game) >= 0.8) return;
  double distance = qa_q2m_target_distance(c);
  if (distance >= 80 && distance < 500) {
    q2mpm_set_move(c, q2mpm_rnd(c->game) < 0.5 ? "brain_move_attack3" : "brain_move_attack4");
    return;
  }
  if (distance >= 500) q2mpm_set_move(c, "brain_move_attack4");
}
static void brain_pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2bm_damaged_skin(c);
  if (q2mpm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2mpm_now(c->game) + 3;
  double random = q2mpm_rnd(c->game);
  q2mpm_snd(c, random < 0.33 || random >= 0.66 ? "brain/brnpain1.wav" : "brain/brnpain2.wav", 2);
  q2mpm_set_move(c, random < 0.33 ? "brain_move_pain1" : random < 0.66 ? "brain_move_pain2" : "brain_move_pain3");
}
static void tongue_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy) || !entity->has_enemy) return;
  qa_vec3 start = qa_q2m_project_flash_default(c, q2mpm_v3(24, 0, 16));
  qa_vec3 top = enemy.origin; top.z += enemy.bounds.max.z - 8;
  qa_vec3 bottom = enemy.origin; bottom.z += enemy.bounds.min.z + 8;
  if (!tongue_allowed(start, enemy.origin) && !tongue_allowed(start, top) && !tongue_allowed(start, bottom)) return;
  qa_trace_result trace = q2mpm_trace(game, start, enemy.origin, NULL, &entity->actor.id, 0x6000003);
  if (trace.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(trace.hit.actor, entity->enemy)) return;
  q2mpm_snd(c, "brain/brnatck3.wav", 1);
  qa_q2bm_extended_event beam;
  memset(&beam, 0, sizeof beam);
  beam.kind = "monster-beam"; beam.effect = "parasite"; beam.actor = entity->actor.id;
  beam.start = start; beam.end = enemy.origin;
  qa_q2bm_emit_extended(beam);
  (void)q2mpm_damage(game, entity->enemy, entity, &entity->actor.id, 5, 0,
                     qa_sub3_double(start, enemy.origin), enemy.origin, q2mpm_zero(), 36, true, 8);
  qa_body_state body = q2mpm_body(game, entity);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true;
  change.origin = body.origin;
  change.origin.z += 1;
  q2mpm_move_body(game, entity, change, true, false);
  qa_q2f_owned_result target = game->host->actors.resolve_owned(game->host->actors.ctx, entity->enemy);
  if (target.has_value) {
    qa_body_state written = enemy;
    written.velocity = qa_scale3_double(qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward, -1200);
    game->host->bodies.write(game->host->bodies.ctx, target.value, written);
  }
}
static void laserbeam_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  if (q2mpm_rnd(game) > 0.8) q2mpm_snd4(c, "misc/lasfly.wav", 0, 1, 3);
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_vec3 origin = q2mpm_body(game, entity).origin;
  qa_vec3 angles = qa_vector_to_angles_with_rule(qa_sub3_double(enemy.origin, origin), QA_VECTOR_TO_ANGLES_NATIVE_AI);
  qa_angle_vectors basis = qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_NATIVE);
  const qa_vec3 *eyes[2] = {qa_q2mpm_xatrix_brain_right_eye, qa_q2mpm_xatrix_brain_left_eye};
  for (int i = 0; i < 2; i++) {
    qa_vec3 offset = q2mpm_at_v(eyes[i], 11, entity->frame - qa_q2mpm_xatrix_brain_frames_value.walk101);
    qa_vec3 start = qa_add3_double(origin, qa_add3_double(qa_scale3_double(basis.right, offset.x),
        qa_add3_double(qa_scale3_double(basis.forward, offset.y), qa_scale3_double(basis.up, offset.z))));
    (void)qa_q2mpm_monster_dabeam(entity, game, entity->has_enemy ? &entity->enemy : NULL, start, angles, 1, false);
  }
}
static void laser_reattack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  const qa_actor_id *enemy = c->entity->has_enemy ? &c->entity->enemy : NULL;
  if (q2mpm_rnd(c->game) < 0.5 && qa_q2m_visible_enemy(c) && qa_q2m_health(c->game, enemy) > 0)
    c->entity->frame = qa_q2mpm_xatrix_brain_frames_value.walk101;
}
static qa_q2m_definition brain_definition;
static bool brain_ready;
const qa_q2m_definition *qa_q2mpm_get_xatrix_brain_definition(void) {
  if (!brain_ready) {
    brain_definition = qa_q2bm_brain_definition;
    brain_definition.moves = qa_q2mpm_xatrix_brain_moves;
    brain_definition.move_count = qa_q2mpm_xatrix_brain_move_count;
    brain_definition.has_ranged_attack_flag = true;
    brain_definition.has_ranged_attack = true;
    brain_definition.has_source_callbacks = true;
    brain_definition.source_callbacks = qa_q2mpm_monster_dabeam_callbacks();
    brain_definition.attack = (qa_q2m_handler){NULL, brain_attack_fn};
    brain_definition.reactions.ctx = NULL;
    brain_definition.reactions.pain = brain_pain_fn;
    const qa_q2m_named_handler extra[] = {
      {"brain_tounge_attack", {NULL, tongue_fn}},
      {"brain_laserbeam", {NULL, laserbeam_fn}},
      {"brain_laserbeam_reattack", {NULL, laser_reattack_fn}},
    };
    brain_definition.callbacks = qa_q2mpm_merge_callbacks(brain_definition.callbacks, brain_definition.callback_count,
                                                          extra, 3, &brain_definition.callback_count);
    brain_ready = true;
  }
  return &brain_definition;
}

static void infantry_fire(qa_q2m_context *c) {
  if (c->entity->frame != qa_q2mpm_xatrix_infantry_frames_value.attak103) {
    const qa_q2m_handler *handler = qa_q2m_infantry_callback("InfantryMachineGun");
    if (handler) handler->fn(handler->ctx, c);
    return;
  }
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_body_state enemy;
  bool has_enemy = qa_q2m_enemy_body(c, &enemy);
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, 26));
  qa_vec3 direction;
  if (!has_enemy) {
    direction = qa_math_angle_vectors_rule(q2mpm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).forward;
  } else {
    qa_q2f_entity *enemy_entity = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
    qa_vec3 point = qa_add3_double(enemy.origin, qa_scale3_double(enemy.velocity, -0.2));
    point.z += enemy_entity ? enemy_entity->view_height : 22;
    direction = qa_normalize3_double_hypot(qa_sub3_double(point, start));
  }
  c->weapons.fire_bullet(c->weapons.ctx, entity, game, start, direction, 3, 4, 300, 500, 0);
  qa_q2mpm_monster_flash(c, 26, start, direction);
}
static void infantry_fire_fn(void *ctx, qa_q2m_context *c) { (void)ctx; infantry_fire(c); }
static void idle_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_snd4(c, "infantry/infidle1.wav", 2, 1, 2);
  q2mpm_set_move(c, "infantry_move_fidget");
}
static void dodge_fn(void *ctx, qa_q2m_context *c, qa_actor_id attacker, double eta, const qa_trace_result *trace,
                     bool gravity) {
  (void)ctx; (void)eta; (void)trace; (void)gravity;
  if (q2mpm_rnd(c->game) > 0.25) return;
  if (!c->entity->has_enemy) { c->entity->has_enemy = true; c->entity->enemy = attacker; }
  q2mpm_set_move(c, "infantry_move_duck");
}
static void set_firetime_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->pause_time = q2mpm_now(c->game) + (floor(q2mpm_rnd(c->game) * 16) + 5) * 0.1;
}
static void cock_fn(void *ctx, qa_q2m_context *c) { (void)ctx; q2mpm_snd(c, "infantry/infatck3.wav", 1); }
static void fire_hold_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  infantry_fire(c);
  c->state->hold_frame = q2mpm_now(c->game) < c->state->pause_time;
}
static qa_q2m_definition infantry_definition;
static bool infantry_ready;
const qa_q2m_definition *qa_q2mpm_get_xatrix_infantry_definition(void) {
  if (!infantry_ready) {
    memset(&infantry_definition, 0, sizeof infantry_definition);
    infantry_definition.classname = "monster_infantry";
    infantry_definition.kind = "infantry";
    infantry_definition.model = "models/monsters/infantry/tris.md2";
    infantry_definition.health = 100;
    infantry_definition.gib_health = -40;
    infantry_definition.mass = 200;
    infantry_definition.bounds = qa_q2bm_humanoid_bounds;
    infantry_definition.scale = 1;
    infantry_definition.initial_move = "infantry_move_stand";
    infantry_definition.moves = qa_q2mpm_xatrix_infantry_moves;
    infantry_definition.move_count = qa_q2mpm_xatrix_infantry_move_count;
    infantry_definition.stand = (qa_q2m_handler){NULL, qa_q2m_infantry_stand_handler};
    infantry_definition.walk = (qa_q2m_handler){NULL, qa_q2m_infantry_walk_handler};
    infantry_definition.run = (qa_q2m_handler){NULL, qa_q2m_infantry_run_handler};
    infantry_definition.attack = (qa_q2m_handler){NULL, qa_q2m_infantry_attack_handler};
    infantry_definition.sight = (qa_q2m_handler){NULL, qa_q2m_infantry_sight_handler};
    infantry_definition.idle = (qa_q2m_handler){NULL, idle_fn};
    infantry_definition.reactions.pain = qa_q2m_infantry_pain_handler;
    infantry_definition.reactions.die = qa_q2m_infantry_die_handler;
    infantry_definition.dodge = dodge_fn;
    const qa_q2m_named_handler extra[] = {
      {"InfantryMachineGun", {NULL, infantry_fire_fn}},
      {"infantry_set_firetime", {NULL, set_firetime_fn}},
      {"infantry_cock_gun", {NULL, cock_fn}},
      {"infantry_fire", {NULL, fire_hold_fn}},
    };
    infantry_definition.callbacks = qa_q2mpm_merge_callbacks(qa_q2m_infantry_callbacks, qa_q2m_infantry_callback_count,
                                                             extra, 4, &infantry_definition.callback_count);
    infantry_ready = true;
  }
  return &infantry_definition;
}
static void tank_init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (((uint32_t)c->entity->spawnflags & 8u) != 0) qa_q2mpm_monster_power_armor(c, "shield", 400);
}
static void tank_restore_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (((uint32_t)c->entity->spawnflags & 8u) != 0) qa_q2mpm_restore_monster_power_armor(c);
}
size_t qa_q2mpm_create_xatrix_base_variants(qa_q2m_definition *out, qa_q2m_monsters *monsters) {
  out[0] = *qa_q2mpm_get_xatrix_brain_definition();
  out[1] = *qa_q2mpm_get_xatrix_infantry_definition();
  qa_q2m_definition tank = qa_q2bm_supertank_definition;
  tank.initialize = (qa_q2m_handler){NULL, tank_init_fn};
  tank.restore = (qa_q2m_handler){NULL, tank_restore_fn};
  tank.stand = qa_q2bm_move_handler("supertank_move_stand");
  size_t think_room = tank.source_callbacks.think_count + 1;
  qa_q2f_named_think *storage = malloc(think_room * sizeof *storage);
  if (!storage) qa_q2f_error("Out of memory creating xatrix base variants");
  qa_q2bm_with_boss_explosion_callbacks(&out[2], &tank, monsters, storage);
  return 3;
}
