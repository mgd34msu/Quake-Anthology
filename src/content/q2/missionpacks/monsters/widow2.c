/* Port of src/content/q2/missionpacks/monsters/widow2.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/widow2.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/missionpacks/monsters/power_armor.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/spawn.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_widow2.h"
#include "qa/content/q2/missionpacks/monsters/widow/common.h"
#include "qa/content/q2/missionpacks/monsters/widow/death.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/foundation/js.h"

static const qa_vec3 tongue_offsets[] = {
  {17.48, 0.10, 68.92}, {17.47, 0.29, 68.91}, {17.45, 0.53, 68.87}, {17.42, 0.78, 68.81},
  {17.39, 1.02, 68.75}, {17.37, 1.20, 68.70}, {17.36, 1.24, 68.71}, {17.37, 1.21, 68.72},
};
typedef struct widow2_bundle {
  qa_q2m_monsters *monsters;
  qa_q2mpm_weapons weapons;
  qa_q2mpm_services services;
  qa_q2mpm_monster_state *source;
  qa_q2_think power_think;
  qa_q2_think explode;
} widow2_bundle;
static bool tongue_okay(qa_vec3 start, qa_vec3 end) {
  qa_vec3 delta = qa_sub3_double(start, end);
  double pitch = qa_vector_to_angles_with_rule(delta, QA_VECTOR_TO_ANGLES_NATIVE_AI).x;
  if (pitch < -180) pitch += 360;
  return qa_hypot3(delta) <= 256 && fabs(pitch) <= 30;
}
static void save_beam(qa_q2m_context *c) {
  qa_body_state enemy;
  bool has = qa_q2m_enemy_body(c, &enemy);
  c->entity->pos2 = has ? c->entity->pos1 : q2mpm_zero();
  c->entity->pos1 = has ? enemy.origin : q2mpm_zero();
}
static void save_beam_fn(void *ctx, qa_q2m_context *c) { (void)ctx; save_beam(c); }
static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->hold_frame = false;
  q2mpm_set_move(c, c->state->stand_ground ? "widow2_move_stand" : "widow2_move_run");
}
static void beam_fire(widow2_bundle *bundle, qa_q2m_context *c) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  const qa_q2mpm_rogue_widow2_frames *f = &qa_q2mpm_rogue_widow2_frames_value;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  if (entity->frame >= f->spawn04 && entity->frame <= f->spawn14) {
    double index = entity->frame - f->spawn04;
    double flash = 200 + index;
    qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, flash));
    qa_vec3 angles = q2mpm_body(game, entity).angles;
    angles.x += qa_vector_to_angles_with_rule(qa_sub3_double(enemy.origin, start), QA_VECTOR_TO_ANGLES_NATIVE_AI).x;
    angles.y -= -40 + index * 8;
    qa_vec3 direction = qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_NATIVE).forward;
    bundle->weapons.fire_heat_beam(bundle->weapons.ctx, entity, game, start, direction, q2mpm_zero(), 10, 50);
    qa_q2rm_monster_flash(c, flash, start, direction);
    return;
  }
  save_beam(c);
  bool firing = entity->frame >= f->fireb05 && entity->frame <= f->fireb09;
  double flash = firing ? 195 + entity->frame - f->fireb05 : 195;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, flash));
  qa_vec3 eye;
  double height = (qa_q2m_enemy_eye(c, &eye) ? eye.z : enemy.origin.z) - enemy.origin.z;
  qa_vec3 target = entity->pos2;
  target.z += height - 10;
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(target, start));
  bundle->weapons.fire_heat_beam(bundle->weapons.ctx, entity, game, start, direction, q2mpm_zero(), 10, 50);
  if (firing) qa_q2rm_monster_flash(c, flash, start, direction);
}
static void beam_fn(void *ctx, qa_q2m_context *c) { beam_fire(ctx, c); }
static void tongue_pull(widow2_bundle *bundle, qa_q2m_context *c) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy) || !c->entity->has_enemy) { run_fn(NULL, c); return; }
  const qa_q2mpm_rogue_widow2_frames *f = &qa_q2mpm_rogue_widow2_frames_value;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_vec3 start = qa_q2mpm_widow_project(entity, game, q2mpm_at_v(tongue_offsets, 8, entity->frame - f->tongs01));
  if (!tongue_okay(start, enemy.origin)) return;
  qa_owned_actor actor; bool has_actor = qa_session_actor_registry_resolve_owned(game->host->actors, entity->enemy, &actor);
  if (!has_actor) return;
  qa_vec3 origin = enemy.origin;
  if (enemy.ground != NULL) origin.z += 1;
  qa_body_state body = q2mpm_body(game, entity);
  qa_vec3 delta = qa_sub3_double(body.origin, origin);
  qa_vec3 velocity;
  if (game->host->is_player(game->host->ctx, entity->enemy)) {
    velocity = qa_add3_double(enemy.velocity, qa_scale3_double(qa_normalize3_double_hypot(delta), 1000));
  } else {
    qa_q2m_context *target = qa_q2m_monsters_context(bundle->monsters, entity->enemy);
    if (target) {
      target->state->ideal_yaw = qa_vector_to_angles_with_rule(delta, QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
      qa_q2m_change_yaw(target);
    }
    velocity = qa_scale3_double(qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward, 1000);
  }
  qa_body_state_result current = qa_shared_body_table_read(game->host->bodies, actor.id);
  qa_body_state written = enemy;
  if (current.has_value) written.angles = current.value.angles;
  written.origin = origin;
  written.ground = NULL;
  written.velocity = velocity;
  qa_shared_body_table_write(game->host->bodies, actor, written);
  qa_q2_entity *target = game->entity(game->ctx, &entity->enemy);
  if (target) game->motion(game->ctx, target, target->motion);
}
static void tongue_pull_fn(void *ctx, qa_q2m_context *c) { tongue_pull(ctx, c); }
static bool check_attack_fn(void *ctx, qa_q2m_context *c) {
  widow2_bundle *bundle = ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  qa_body_state enemy;
  qa_vec3 eye;
  if (!qa_q2m_enemy_body(c, &enemy) || !qa_q2m_enemy_eye(c, &eye) || !entity->has_enemy) return false;
  qa_q2mpm_widow_powerups(c, bundle->services, bundle->source);
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
      qa_q2_entity *enemy_entity = game->entity(game->ctx, &entity->enemy);
      if (!enemy_entity || strcmp(enemy_entity->solid, "none") != 0 || trace.fraction < 1) return false;
    }
  }
  state->ideal_yaw = qa_vector_to_angles_with_rule(qa_sub3_double(enemy.origin, q2mpm_body(game, entity).origin),
                                                   QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  if (entity->timestamp < q2mpm_now(game) && distance < 300
      && tongue_okay(qa_q2mpm_widow_project(entity, game, q2mpm_at_v(tongue_offsets, 8, 0)), enemy.origin)) {
    if (game->options.skill == 0 && floor(q2mpm_rnd(game) * 4) != 0) return false;
    state->attack_state = QA_Q2M_ATTACK_MELEE;
    return true;
  }
  if (q2mpm_now(game) < state->attack_finished) return false;
  double chance = state->stand_ground ? 0.4 : distance < 1000 ? 0.8 : 0.5;
  qa_q2_entity *enemy_entity = game->entity(game->ctx, &entity->enemy);
  if (q2mpm_rnd(game) < chance || (enemy_entity && strcmp(enemy_entity->solid, "none") == 0)) {
    state->attack_state = QA_Q2M_ATTACK_MISSILE;
    return true;
  }
  return false;
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  widow2_bundle *bundle = ctx;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  entity->max_health = 2800 + 1000 * game->options.skill
      + (game->options.mode == QA_Q2_MODE_COOP ? 500 * game->options.skill : 0);
  qa_gameplay_authority_set_health(game->host->combat, entity->actor, entity->max_health);
  entity->laser_immune = true;
  c->state->ignore_shots = true;
  entity->prethink = bundle->power_think;
  qa_q2mpm_widow_slots(c);
  if (game->options.skill == 3) qa_q2mpm_monster_power_armor(c, "shield", 750);
}
static void search_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2mpm_rnd(c->game) < 0.5) q2mpm_snd4(c, "bosshovr/bhvunqv1.wav", 2, 1, 0);
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  widow2_bundle *bundle = ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  qa_q2mpm_rogue_state *own = qa_q2mpm_state_get(bundle->source, entity);
  bool blocked = own->blocked;
  own->blocked = false;
  if (!qa_q2bm_alive_enemy(c)) return;
  bool ready = q2mpm_now(game) >= state->attack_finished;
  if (bundle->services.bad_area(bundle->services.ctx, entity->actor.id)) {
    q2mpm_set_move(c, q2mpm_rnd(game) < 0.75 || !ready ? "widow2_move_attack_pre_beam" : "widow2_move_attack_disrupt");
    return;
  }
  qa_q2mpm_widow_slots(c);
  if ((state->attack_state == QA_Q2M_ATTACK_BLIND || blocked) && qa_q2mpm_widow_slots_left(c) >= 2) {
    q2mpm_set_move(c, "widow2_move_spawn");
    return;
  }
  double luck = q2mpm_rnd(game);
  bool slots = qa_q2mpm_widow_slots_left(c) >= 2;
  if (qa_q2m_target_distance(c) < 600) {
    if (slots) {
      if (luck <= 0.4) { q2mpm_set_move(c, "widow2_move_attack_pre_beam"); return; }
      q2mpm_set_move(c, luck <= 0.7 && ready ? "widow2_move_attack_disrupt" : "widow2_move_spawn");
      return;
    }
    q2mpm_set_move(c, luck <= 0.5 || !ready ? "widow2_move_attack_pre_beam" : "widow2_move_attack_disrupt");
    return;
  }
  if (slots) {
    if (luck < 0.3) { q2mpm_set_move(c, "widow2_move_attack_pre_beam"); return; }
    q2mpm_set_move(c, luck < 0.65 || !ready ? "widow2_move_spawn" : "widow2_move_attack_disrupt");
    return;
  }
  q2mpm_set_move(c, luck < 0.45 || !ready ? "widow2_move_attack_pre_beam" : "widow2_move_attack_disrupt");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2bm_damaged_skin(c);
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  double skill = game->options.skill;
  if (skill == 3 || q2mpm_now(game) < state->pain_time) return;
  state->pain_time = q2mpm_now(game) + 5;
  q2mpm_snd4(c, reaction.damage < 15 ? "widow/bw2pain1.wav" : reaction.damage < 75 ? "widow/bw2pain2.wav"
                                                                                    : "widow/bw2pain3.wav", 2, 1, 0);
  if (reaction.damage >= 15 && q2mpm_rnd(game) < (reaction.damage < 75 ? 0.6 - 0.2 * skill : 0.75 - 0.1 * skill)) {
    state->manual_steering = false;
    q2mpm_set_move(c, "widow2_move_pain");
  }
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  widow2_bundle *bundle = ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  if (qa_q2m_health(game, &entity->actor.id) <= state->gib_health) {
    double clipped = qa_js_min(reaction.pain.damage, 100);
    q2mpm_snd(c, "misc/udeath.wav", 2);
    for (int i = 0; i < 2; i++)
      (void)qa_q2mpm_widow_gib(entity, game, "models/objects/gibs/bone/tris.md2", clipped, true, NULL, false, "", false);
    for (int i = 0; i < 3; i++)
      (void)qa_q2mpm_widow_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", clipped, true, NULL, false, "", false);
    for (int i = 0; i < 3; i++) {
      (void)qa_q2mpm_widow_gib(entity, game, "models/monsters/blackwidow2/gib1/tris.md2", clipped, false, NULL, true, "", false);
      (void)qa_q2mpm_widow_gib(entity, game, "models/monsters/blackwidow2/gib2/tris.md2", clipped, false, NULL, true, "misc/fhit3.wav", false);
    }
    for (int i = 0; i < 2; i++) {
      (void)qa_q2mpm_widow_gib(entity, game, "models/monsters/blackwidow2/gib3/tris.md2", clipped, false, NULL, true, "", false);
      (void)qa_q2mpm_widow_gib(entity, game, "models/monsters/blackwidow/gib3/tris.md2", clipped, false, NULL, true, "", false);
    }
    qa_q2m_gib_options plain = {false, false, false, false};
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/chest/tris.md2", clipped, plain);
    (void)qa_q2m_throw_head(entity, game, "models/objects/gibs/head2/tris.md2", clipped);
    state->dead = true;
    state->gibbed = true;
    return;
  }
  if (state->dead) return;
  q2mpm_snd4(c, "widow/death.wav", 2, 1, 0);
  state->dead = true;
  state->can_take_damage = false;
  q2mpm_set_damageable(game, entity, false);
  entity->count = 0;
  qa_body_state enemy;
  qa_vec3 point = qa_q2m_enemy_body(c, &enemy) ? enemy.origin : q2mpm_body(game, entity).origin;
  for (size_t i = 0; i < qa_q2_game_entity_count(game); i++) {
    qa_q2_entity *child = qa_q2_game_entity_at(game, i);
    if (strcmp(child->classname, "monster_stalker") == 0 && qa_q2m_health(game, &child->actor.id) > 0)
      (void)q2mpm_damage(game, child->actor.id, entity, &entity->actor.id,
                         qa_q2m_health(game, &child->actor.id) + 1, 0, q2mpm_zero(), point, q2mpm_zero(), 0, false, 8);
  }
  qa_q2mpm_widow_clear_powerups(c, bundle->source);
  q2mpm_set_move(c, "widow2_move_death");
}
static void beam_target_remove_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->pos1 = q2mpm_zero();
  c->entity->pos2 = q2mpm_zero();
}
static void step_fn(void *ctx, qa_q2m_context *c) { (void)ctx; q2mpm_snd(c, "widow/bwstep1.wav", 4); }
static void reattack_beam_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->manual_steering = false;
  if (c->entity->has_enemy && qa_q2m_in_front(c, c->entity->enemy) && q2mpm_rnd(c->game) <= 0.5) {
    q2mpm_set_move(c, q2mpm_rnd(c->game) < 0.7 || qa_q2mpm_widow_slots_left(c) < 2 ? "widow2_move_attack_beam"
                                                                                    : "widow2_move_spawn");
    return;
  }
  q2mpm_set_move(c, "widow2_move_attack_post_beam");
}
static void save_loc_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 eye;
  c->entity->pos1 = qa_q2m_enemy_eye(c, &eye) ? eye : q2mpm_zero();
}
static void disrupt_fn(void *ctx, qa_q2m_context *c) {
  widow2_bundle *bundle = ctx;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(c->game->options.edition, 148));
  bool locked = qa_hypot3(qa_sub3_double(c->entity->pos1, enemy.origin)) < 30;
  qa_vec3 direction;
  if (locked) {
    direction = qa_normalize3_double_hypot(qa_sub3_double(c->entity->pos1, start));
  } else if (!qa_q2rm_predicted_direction(c, start, 1200, true, 0, &direction)) {
    return;
  }
  (void)bundle->weapons.fire_tracker(bundle->weapons.ctx, c->entity, c->game, start, direction, 20,
                                     locked ? 500 : 1200, locked && c->entity->has_enemy ? &c->entity->enemy : NULL);
  qa_q2rm_monster_flash(c, 148, start, direction);
}
static void disrupt_reattack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2mpm_rnd(c->game) < 0.25 + 0.15 * c->game->options.skill)
    c->state->next_frame = qa_q2mpm_rogue_widow2_frames_value.firea01;
}
static void start_spawn_fn(void *ctx, qa_q2m_context *c) { (void)ctx; c->state->manual_steering = true; }
static void ready_spawn_fn(void *ctx, qa_q2m_context *c) {
  widow2_bundle *bundle = ctx;
  beam_fire(bundle, c);
  qa_q2mpm_widow_summon(c, bundle->monsters, true, true);
}
static void spawn_check_fn(void *ctx, qa_q2m_context *c) {
  widow2_bundle *bundle = ctx;
  beam_fire(bundle, c);
  qa_q2mpm_widow_summon(c, bundle->monsters, true, false);
}
static void tongue_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy) || !c->entity->has_enemy) return;
  const qa_q2mpm_rogue_widow2_frames *f = &qa_q2mpm_rogue_widow2_frames_value;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_vec3 start = qa_q2mpm_widow_project(entity, game, q2mpm_at_v(tongue_offsets, 8, entity->frame - f->tongs01));
  qa_vec3 top = enemy.origin; top.z += enemy.bounds.max.z - 8;
  qa_vec3 bottom = enemy.origin; bottom.z += enemy.bounds.min.z + 8;
  if (!tongue_okay(start, enemy.origin) && !tongue_okay(start, top) && !tongue_okay(start, bottom)) return;
  qa_trace_result trace = q2mpm_trace(game, start, enemy.origin, NULL, &entity->actor.id, 0x6000003);
  if (trace.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(trace.hit.actor, entity->enemy)) return;
  q2mpm_snd(c, "brain/brnatck3.wav", 1);
  qa_q2_presentation_event beam;
  memset(&beam, 0, sizeof beam);
  beam.kind = "monster-beam"; beam.effect = "parasite"; beam.has_actor = true; beam.actor = entity->actor.id;
  beam.start = start; beam.end = enemy.origin;
  game->host->emit(game->host->ctx, beam);
  (void)q2mpm_damage(game, entity->enemy, entity, &entity->actor.id, 2, 0, qa_sub3_double(start, enemy.origin),
                     enemy.origin, q2mpm_zero(), 0, false, 8);
}
static void crunch_fn(void *ctx, qa_q2m_context *c) {
  widow2_bundle *bundle = ctx;
  qa_body_state existing;
  if (!qa_q2m_enemy_body(c, &existing)) { run_fn(NULL, c); return; }
  tongue_pull(bundle, c);
  qa_body_state enemy;
  bool has = qa_q2m_enemy_body(c, &enemy);
  double kick = c->entity->frame != qa_q2mpm_rogue_widow2_frames_value.tongs07 ? 0
      : (!has || enemy.ground == NULL) ? 250 : 500;
  (void)c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, q2mpm_v3(150, 0, 4),
                            20 + floor(q2mpm_rnd(c->game) * 6), kick);
}
static void toss_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->timestamp = q2mpm_now(c->game) + 3;
}
#define EXPL_FN(name, x, y, z) \
  static void name(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2mpm_widow_explosion(c, q2mpm_v3(x, y, z)); }
EXPL_FN(explosion1_fn, 23.74, -37.67, 76.96)
EXPL_FN(explosion2_fn, -20.49, 36.92, 73.52)
EXPL_FN(explosion3_fn, 2.11, 0.05, 92.20)
EXPL_FN(explosion4_fn, -28.04, -35.57, -77.56)
EXPL_FN(explosion5_fn, -20.11, -1.11, 40.76)
static void explode_cb_fn(void *ctx, qa_q2m_context *c) {
  widow2_bundle *bundle = ctx;
  bundle->explode.fn(bundle->explode.ctx, c->entity, c->game);
}
static void start_searching_fn(void *ctx, qa_q2m_context *c) { (void)ctx; c->entity->count = 0; }
static void keep_searching_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->entity->count <= 2) {
    q2mpm_set_move(c, "widow2_move_dead");
    c->entity->frame = qa_q2mpm_rogue_widow2_frames_value.dthsrh01;
    c->entity->count++;
  } else {
    q2mpm_set_move(c, "widow2_move_really_dead");
  }
}
static void finaldeath_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->corpse = true;
  c->state->can_take_damage = true;
  q2mpm_set_damageable(c->game, c->entity, true);
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2mpm_v3(-70, -70, 0);
  change.bounds.max = q2mpm_v3(70, 70, 80);
  q2mpm_move_body(c->game, c->entity, change, false, false);
  c->game->motion(c->game->ctx, c->entity, "toss");
  c->game->cancel(c->game->ctx, c->entity);
}
qa_q2m_definition qa_q2mpm_create_widow2_definition(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters, qa_q2mpm_weapons weapons,
                                                    qa_q2mpm_services services, qa_q2mpm_monster_state *source) {
  widow2_bundle *bundle = qa_q2mpm_arena_alloc(arena, sizeof *bundle);
  if (!bundle) qa_raise("Error", 0, 0, "Out of memory creating widow2 definition");
  bundle->monsters = monsters;
  bundle->weapons = weapons;
  bundle->services = services;
  bundle->source = source;
  bundle->power_think = qa_q2mpm_widow_power_think(arena, monsters, source);
  bundle->explode = qa_q2mpm_create_widow_explode(arena, monsters);
  qa_q2m_named_handler *callbacks = qa_q2mpm_arena_alloc(arena, 30 * sizeof *callbacks);
  if (!callbacks) qa_raise("Error", 0, 0, "Out of memory creating widow2 definition");
  size_t n = 0;
  callbacks[n++] = (qa_q2m_named_handler){"widow2_run", {NULL, run_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"Widow2Beam", {bundle, beam_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"Widow2SaveBeamTarget", {NULL, save_beam_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"Widow2StartSweep", {NULL, save_beam_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"Widow2BeamTargetRemove", {NULL, beam_target_remove_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow2_attack_beam", {(void *)"widow2_move_attack_beam", qa_q2bm_move_handler_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow2_step", {NULL, step_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow2_reattack_beam", {NULL, reattack_beam_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"Widow2SaveDisruptLoc", {NULL, save_loc_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"WidowDisrupt", {bundle, disrupt_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow2_disrupt_reattack", {NULL, disrupt_reattack_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow_start_spawn", {NULL, start_spawn_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow2_ready_spawn", {bundle, ready_spawn_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow2_spawn_check", {bundle, spawn_check_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"Widow2Tongue", {NULL, tongue_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"Widow2TonguePull", {bundle, tongue_pull_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"Widow2Crunch", {bundle, crunch_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"Widow2Toss", {NULL, toss_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"WidowExplosion1", {NULL, explosion1_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"WidowExplosion2", {NULL, explosion2_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"WidowExplosion3", {NULL, explosion3_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"WidowExplosion4", {NULL, explosion4_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"WidowExplosion5", {NULL, explosion5_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"WidowExplosion6", {NULL, explosion5_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"WidowExplosion7", {NULL, explosion5_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"WidowExplosionLeg", {NULL, qa_q2mpm_widow_explosion_leg_handler}};
  callbacks[n++] = (qa_q2m_named_handler){"WidowExplode", {bundle, explode_cb_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow2_start_searching", {NULL, start_searching_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow2_keep_searching", {NULL, keep_searching_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"widow2_finaldeath", {NULL, finaldeath_fn}};
  qa_q2_callback_definitions debris = qa_q2mpm_widow_debris_callbacks();
  qa_q2_callback_definitions spawn = qa_q2mpm_rogue_spawn_callbacks();
  size_t think_count = debris.think_count + spawn.think_count + 2;
  qa_q2_named_think *thinks = qa_q2mpm_arena_alloc(arena, think_count * sizeof *thinks);
  if (!thinks) qa_raise("Error", 0, 0, "Out of memory creating widow2 definition");
  size_t at = 0;
  for (size_t i = 0; i < debris.think_count; i++) thinks[at++] = debris.think[i];
  for (size_t i = 0; i < spawn.think_count; i++) thinks[at++] = spawn.think[i];
  thinks[at++] = (qa_q2_named_think){"q2:rogue/widow2_powerups", bundle->power_think};
  thinks[at++] = (qa_q2_named_think){"q2:rogue/WidowExplode", bundle->explode};
  qa_q2m_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.classname = "monster_widow2";
  definition.kind = "widow2";
  definition.model = "models/monsters/blackwidow2/tris.md2";
  definition.health = 2800;
  definition.gib_health = -900;
  definition.mass = 2500;
  definition.bounds.min = q2mpm_v3(-70, -70, 0);
  definition.bounds.max = q2mpm_v3(70, 70, 144);
  definition.scale = 1;
  definition.has_yaw_speed = true;
  definition.yaw_speed = 30;
  definition.initial_move = "widow2_move_stand";
  definition.moves = qa_q2mpm_rogue_widow2_moves;
  definition.move_count = qa_q2mpm_rogue_widow2_move_count;
  definition.callbacks = callbacks;
  definition.callback_count = n;
  definition.has_source_callbacks = true;
  definition.source_callbacks = debris;
  definition.source_callbacks.think = thinks;
  definition.source_callbacks.think_count = at;
  definition.stand = qa_q2bm_move_handler("widow2_move_stand");
  definition.walk = qa_q2bm_move_handler("widow2_move_walk");
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.melee = qa_q2bm_move_handler("widow2_move_tongs");
  definition.search = (qa_q2m_handler){NULL, search_fn};
  definition.attack = (qa_q2m_handler){bundle, attack_fn};
  definition.initialize = (qa_q2m_handler){bundle, init_fn};
  definition.restore = (qa_q2m_handler){NULL, qa_q2mpm_widow_restore_armor_handler};
  definition.reactions.ctx = bundle;
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.check_attack_ctx = bundle;
  definition.check_attack = check_attack_fn;
  return definition;
}
