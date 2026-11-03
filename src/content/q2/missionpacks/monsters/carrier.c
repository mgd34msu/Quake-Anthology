/* Port of src/content/q2/missionpacks/monsters/carrier.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/carrier.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/boss_common.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/spawn.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_carrier.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/foundation/js.h"

typedef struct carrier_bundle {
  qa_q2m_monsters *monsters;
  qa_q2mpm_services services;
} carrier_bundle;
typedef struct relation { bool front, back, below; } carrier_relation;
static const qa_bounds flyer_bounds = {{-16, -16, -24}, {16, 16, 16}};
static carrier_relation relation_of(qa_q2m_context *c, qa_actor_id actor) {
  carrier_relation result = {false, false, false};
  qa_body_state_result target = qa_shared_body_table_read(c->game->host->bodies, actor);
  if (!target.has_value) return result;
  qa_body_state body = q2mpm_body(c->game, c->entity);
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(target.value.origin, body.origin));
  double forward = qa_dot3_double(direction, qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward);
  result.front = forward > 0.3;
  result.back = forward < -0.3;
  result.below = -direction.z > 0.95;
  return result;
}
static void rocket_fire(carrier_bundle *bundle, qa_q2m_context *c) {
  (void)bundle;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy) || !c->entity->has_enemy) return;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  bool predictive = game->host->is_player(game->host->ctx, entity->enemy) && q2mpm_rnd(game) < 0.5;
  qa_vec3 right = qa_math_angle_vectors_rule(q2mpm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).right;
  static const double spreads[] = {0.4, 0.025, -0.025, -0.4};
  for (double index = 0; index < 4; index++) {
    double flash = 191 + index;
    qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, flash));
    qa_vec3 direction;
    if (predictive) {
      if (!qa_q2rm_predicted_direction(c, start, 750, false, -0.3 + index * 0.15, &direction)) continue;
    } else {
      qa_vec3 target = enemy.origin;
      target.z -= index == 0 || index == 3 ? 15 : 0;
      direction = qa_normalize3_double_hypot(qa_add3_double(
          qa_normalize3_double_hypot(qa_sub3_double(target, start)),
          qa_scale3_double(right, q2mpm_at_d(spreads, 4, index))));
    }
    (void)c->weapons.fire_rocket(c->weapons.ctx, entity, game, start, direction, 50, predictive ? 750 : 500, 70, 50);
    qa_q2rm_monster_flash(c, flash, start, direction);
  }
}
static void coop_check(carrier_bundle *bundle, qa_q2m_context *c) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  if (game->options.mode != QA_Q2_MODE_COOP || entity->wait > q2mpm_now(game)) return;
  size_t player_count = 0;
  const qa_actor_id *players = game->host->players(game->host->ctx, &player_count);
  qa_actor_id *targets = malloc((player_count ? player_count : 1) * sizeof *targets);
  if (!targets) qa_raise("Error", 0, 0, "Out of memory in carrier coop check");
  size_t count = 0;
  for (size_t i = 0; i < player_count; i++) {
    qa_actor_id player = players[i];
    if (!q2mpm_live(game, player) || !game->host->is_player(game->host->ctx, player)) continue;
    carrier_relation direction = relation_of(c, player);
    qa_body_state_result target = qa_shared_body_table_read(game->host->bodies, player);
    if (target.has_value && (direction.back || direction.below)
        && q2mpm_trace(game, q2mpm_body(game, entity).origin, target.value.origin, NULL, &entity->actor.id, 3).fraction == 1)
      targets[count++] = player;
  }
  if (count == 0) { free(targets); return; }
  double pick = qa_js_min((double)count - 1, floor(q2mpm_rnd(game) * (double)count));
  qa_actor_id chosen = q2mpm_at_a(targets, count, pick);
  free(targets);
  entity->wait = q2mpm_now(game) + 2;
  bool had_enemy = entity->has_enemy;
  qa_actor_id previous = entity->enemy;
  entity->has_enemy = true;
  entity->enemy = chosen;
  rocket_fire(bundle, c);
  entity->has_enemy = had_enemy;
  entity->enemy = previous;
}
static void machine_gun(carrier_bundle *bundle, qa_q2m_context *c) {
  coop_check(bundle, c);
  for (int gun = 0; gun < 2; gun++) {
    bool right_gun = gun == 1;
    qa_body_state enemy;
    qa_vec3 eye;
    if (!qa_q2m_enemy_body(c, &enemy) || !qa_q2m_enemy_eye(c, &eye)) continue;
    double flash = (c->state->manual_steering ? 152 : 138) + (right_gun ? 1 : 0);
    qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(c->game->options.edition, flash));
    qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(
        qa_add3_double(eye, qa_scale3_double(enemy.velocity, right_gun ? 0.2 : -0.2)), start));
    c->weapons.fire_bullet(c->weapons.ctx, c->entity, c->game, start, direction, 6, 4, 900, 500, 0);
    qa_q2rm_monster_flash(c, flash, start, direction);
  }
}
static void spawn_child(carrier_bundle *bundle, qa_q2m_context *c) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_vec3 point;
  if (!qa_q2mpm_find_rogue_spawn_point(game, qa_q2m_project_flash_default(c, q2mpm_v3(105, 0, -58)),
                                       flyer_bounds, 32, &point)) return;
  double time = trunc((q2mpm_now(game) + 0.1 - entity->timestamp) / 0.5);
  qa_q2_entity *child = qa_q2mpm_create_rogue_monster(bundle->monsters, game, point,
                                                       q2mpm_body(game, entity).angles,
                                                       time == 2 ? "monster_kamikaze" : "monster_flyer");
  q2mpm_snd4(c, "medic_commander/monsterspawn1.wav", 4, 1, 0);
  c->state->monster_slots--;
  child->has_next_think = true;
  child->next_think = q2mpm_now(game);
  if (child->think.fn) child->think.fn(child->think.ctx, child, game);
  qa_q2m_context *child_context = qa_q2m_monsters_context(bundle->monsters, child->actor.id);
  if (!child_context) qa_raise("Error", 0, 0, "Carrier child has no source controller");
  child_context->state->spawned_by = QA_Q2M_SPAWNED_CARRIER;
  child_context->state->do_not_count = true;
  child_context->state->ignore_shots = true;
  child_context->state->has_commander = true;
  child_context->state->commander = entity->actor.id;
  if (entity->has_enemy && q2mpm_live(game, entity->enemy) && qa_q2m_health(game, &entity->enemy) > 0) {
    child->has_enemy = true;
    child->enemy = entity->enemy;
    qa_q2m_monsters_found_target(bundle->monsters, child_context);
    if (time == 1 || time == 3) {
      child_context->state->lefty = time == 3;
      child_context->state->attack_state = QA_Q2M_ATTACK_SLIDING;
      q2mpm_set_move(child_context, "flyer_move_attack3");
    } else if (time == 2) {
      child_context->state->lefty = false;
      child_context->state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
      q2mpm_set_move(child_context, "flyer_move_kamikaze");
      qa_combat_trait_changes traits;
      memset(&traits, 0, sizeof traits);
      traits.has_mass = true;
      traits.mass = 100;
      qa_gameplay_authority_set_traits(game->host->combat, child->actor, traits);
      child_context->state->charging = true;
    }
  }
}
static void run_move(qa_q2m_context *c) {
  c->state->hold_frame = false;
  q2mpm_set_move(c, c->state->stand_ground ? "carrier_move_stand" : "carrier_move_run");
}
static void run_fn(void *ctx, qa_q2m_context *c) { (void)ctx; run_move(c); }
static void rail_move(qa_q2m_context *c) {
  q2mpm_snd(c, "gladiator/railgun.wav", 1);
  q2mpm_set_move(c, "carrier_move_attack_rail");
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2_game_services *game = c->game;
  double hp = qa_js_max(2000, 2000 + 1000 * (game->options.skill - 1))
      + (game->options.mode == QA_Q2_MODE_COOP ? 500 * game->options.skill : 0);
  c->entity->max_health = hp;
  qa_gameplay_authority_set_health(game->host->combat, c->entity->actor, hp);
  c->entity->laser_immune = true;
  c->state->ignore_shots = true;
  c->state->monster_slots = game->options.skill == 0 ? 3 : game->options.skill == 3 ? 9 : 6;
  qa_q2bm_loop_sound(c, "bosshovr/bhvengn1.wav");
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  carrier_bundle *bundle = ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  state->hold_frame = false;
  if (!entity->has_enemy || !q2mpm_live(game, entity->enemy)) return;
  carrier_relation enemy = relation_of(c, entity->enemy);
  bool ready = q2mpm_now(game) >= state->attack_finished;
  if (bundle->services.bad_area(bundle->services.ctx, entity->actor.id)) {
    if (enemy.back || enemy.below) { q2mpm_set_move(c, "carrier_move_attack_rocket"); return; }
    if (q2mpm_rnd(game) < 0.1 || !ready) q2mpm_set_move(c, "carrier_move_attack_pre_mg");
    else rail_move(c);
    return;
  }
  if (state->attack_state == QA_Q2M_ATTACK_BLIND) { q2mpm_set_move(c, "carrier_move_spawn"); return; }
  if (!enemy.back && !enemy.front && !enemy.below) {
    if (q2mpm_rnd(game) < 0.1 || !ready) q2mpm_set_move(c, "carrier_move_attack_pre_mg");
    else rail_move(c);
    return;
  }
  if (enemy.front) {
    double distance = qa_q2m_target_distance(c);
    if (distance <= 125) {
      if (q2mpm_rnd(game) < 0.8 || !ready) q2mpm_set_move(c, "carrier_move_attack_pre_mg");
      else rail_move(c);
      return;
    }
    double luck = q2mpm_rnd(game);
    if (distance < 600) {
      if (state->monster_slots > 2) {
        if (luck <= 0.2) { q2mpm_set_move(c, "carrier_move_attack_pre_mg"); return; }
        if (luck <= 0.4) { q2mpm_set_move(c, "carrier_move_attack_pre_gren"); return; }
        if (luck <= 0.7 && ready) rail_move(c); else q2mpm_set_move(c, "carrier_move_spawn");
        return;
      }
      if (luck <= 0.3) { q2mpm_set_move(c, "carrier_move_attack_pre_mg"); return; }
      if (luck <= 0.65) { q2mpm_set_move(c, "carrier_move_attack_pre_gren"); return; }
      if (ready) rail_move(c); else q2mpm_set_move(c, "carrier_move_attack_pre_mg");
      return;
    }
    if (state->monster_slots > 2) {
      if (luck < 0.3) { q2mpm_set_move(c, "carrier_move_attack_pre_mg"); return; }
      if (luck < 0.65 && ready) {
        qa_vec3 eye;
        if (qa_q2m_enemy_eye(c, &eye)) entity->pos1 = eye;
        rail_move(c);
        return;
      }
      q2mpm_set_move(c, "carrier_move_spawn");
      return;
    }
    if (luck < 0.45 || !ready) q2mpm_set_move(c, "carrier_move_attack_pre_mg");
    else rail_move(c);
    return;
  }
  if (enemy.below || enemy.back) q2mpm_set_move(c, "carrier_move_attack_rocket");
}
static bool check_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  qa_body_state enemy;
  qa_vec3 eye;
  if (!entity->has_enemy || !qa_q2m_enemy_body(c, &enemy) || !qa_q2m_enemy_eye(c, &eye)) return false;
  if (qa_q2m_health(game, &entity->enemy) > 0) {
    qa_vec3 origin = q2mpm_body(game, entity).origin;
    qa_vec3 start = origin;
    start.z += entity->view_height;
    qa_trace_result trace = q2mpm_trace(game, start, eye, NULL, &entity->actor.id, (double)(1u | 0x2000000u | 8u | 16u));
    if (trace.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(trace.hit.actor, entity->enemy)) {
      if (game->host->is_player(game->host->ctx, entity->enemy) && state->monster_slots > 2) {
        state->attack_state = QA_Q2M_ATTACK_BLIND;
        return true;
      }
      qa_q2_entity *enemy_entity = game->entity(game->ctx, &entity->enemy);
      if (!enemy_entity || strcmp(enemy_entity->solid, "none") != 0 || trace.fraction < 1) return false;
    }
  }
  carrier_relation direction = relation_of(c, entity->enemy);
  double distance = qa_q2m_target_distance(c);
  state->ideal_yaw = qa_vector_to_angles_with_rule(qa_sub3_double(enemy.origin, q2mpm_body(game, entity).origin),
                                                   QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  if ((direction.back || (!direction.front && direction.below)) && q2mpm_now(game) >= entity->wait) {
    entity->wait = q2mpm_now(game) + 2;
    c->attack(c->impl_ctx, c);
    state->attack_state = q2mpm_rnd(game) < 0.6 ? QA_Q2M_ATTACK_SLIDING : QA_Q2M_ATTACK_STRAIGHT;
    return true;
  }
  if (distance < 80) { state->attack_state = QA_Q2M_ATTACK_MISSILE; return true; }
  qa_q2_entity *enemy_entity = game->entity(game->ctx, &entity->enemy);
  if (q2mpm_rnd(game) < (state->stand_ground ? 0.4 : distance < 1000 ? 0.8 : 0.5)
      || (enemy_entity && strcmp(enemy_entity->solid, "none") == 0)) {
    state->attack_state = QA_Q2M_ATTACK_MISSILE;
    return true;
  }
  if ((qa_js_to_uint32(entity->flags) & 1u) != 0)
    state->attack_state = q2mpm_rnd(game) < 0.6 ? QA_Q2M_ATTACK_SLIDING : QA_Q2M_ATTACK_STRAIGHT;
  return false;
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2bm_damaged_skin(c);
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  if (game->options.skill == 3 || q2mpm_now(game) < state->pain_time) return;
  state->pain_time = q2mpm_now(game) + 5;
  bool changed = false;
  if (reaction.damage < 10) {
    q2mpm_snd4(c, "carrier/pain_sm.wav", 2, 1, 0);
  } else if (reaction.damage < 30) {
    q2mpm_snd4(c, "carrier/pain_md.wav", 2, 1, 0);
    if (q2mpm_rnd(game) < 0.5) { changed = true; q2mpm_set_move(c, "carrier_move_pain_light"); }
  } else {
    q2mpm_snd4(c, "carrier/pain_lg.wav", 2, 1, 0);
    q2mpm_set_move(c, "carrier_move_pain_heavy");
    changed = true;
  }
  (void)entity;
  if (changed) {
    state->hold_frame = false;
    state->manual_steering = false;
    state->yaw_speed = 15;
  }
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  q2mpm_snd4(c, "carrier/death.wav", 2, 1, 0);
  c->state->dead = true;
  c->state->can_take_damage = false;
  q2mpm_set_damageable(c->game, c->entity, false);
  c->entity->count = 0;
  q2mpm_set_move(c, "carrier_move_death");
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = {{-56, -56, 0}, {56, 56, 80}};
  qa_q2bm_finish_corpse_bounds(c, bounds);
}
#define COOP_FN(name, call) static void name(void *ctx, qa_q2m_context *c) { coop_check(ctx, c); call; }
static void coop_only_fn(void *ctx, qa_q2m_context *c) { coop_check(ctx, c); }
static void mg_fn(void *ctx, qa_q2m_context *c) { machine_gun(ctx, c); }
static void rocket_cb_fn(void *ctx, qa_q2m_context *c) { rocket_fire(ctx, c); }
static void grenade_fn(void *ctx, qa_q2m_context *c) {
  carrier_bundle *bundle = ctx;
  coop_check(bundle, c);
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  double direction = q2mpm_rnd(game) < 0.5 ? -1 : 1;
  double time = trunc((q2mpm_now(game) - entity->timestamp) / 0.4);
  double right_spread = time == 0 ? 0.15 * direction : time == 2 ? -0.15 * direction : 0;
  double up_spread = time == 0 ? 0.1 - 0.1 * direction : time == 2 ? 0.1 + 0.1 * direction
                     : time == 1 || time == 3 ? 0.1 : 0;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, 140));
  qa_angle_vectors axes = qa_math_angle_vectors_rule(q2mpm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE);
  qa_vec3 aim = qa_add3_double(qa_add3_double(qa_normalize3_double_hypot(qa_sub3_double(enemy.origin, start)),
                                              qa_scale3_double(axes.right, right_spread)),
                               qa_scale3_double(axes.up, up_spread));
  aim.z = qa_js_max(-0.5, qa_js_min(0.15, aim.z));
  (void)c->weapons.fire_grenade(c->weapons.ctx, entity, game, start, aim, 50, 600, 2.5, 90, false, false, true, NULL);
  qa_q2rm_monster_flash(c, 53, start, aim);
}
static void save_loc_fn(void *ctx, qa_q2m_context *c) {
  coop_check(ctx, c);
  qa_vec3 eye;
  if (qa_q2m_enemy_eye(c, &eye)) c->entity->pos1 = eye;
}
static void rail_fn(void *ctx, qa_q2m_context *c) {
  coop_check(ctx, c);
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(c->game->options.edition, 147));
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(c->entity->pos1, start));
  c->weapons.fire_rail(c->weapons.ctx, c->entity, c->game, start, direction, 50, 100);
  qa_q2rm_monster_flash(c, 147, start, direction);
  c->state->attack_finished = q2mpm_now(c->game) + 3;
}
static void attack_mg_fn(void *ctx, qa_q2m_context *c) { coop_check(ctx, c); q2mpm_set_move(c, "carrier_move_attack_mg"); }
static void reattack_mg_fn(void *ctx, qa_q2m_context *c) {
  coop_check(ctx, c);
  bool again = c->entity->has_enemy && qa_q2m_in_front(c, c->entity->enemy) && q2mpm_rnd(c->game) <= 0.5;
  q2mpm_set_move(c, again ? (q2mpm_rnd(c->game) < 0.7 || c->state->monster_slots <= 2 ? "carrier_move_attack_mg"
                                                                                       : "carrier_move_spawn")
                          : "carrier_move_attack_post_mg");
}
static void attack_gren_fn(void *ctx, qa_q2m_context *c) {
  coop_check(ctx, c);
  c->entity->timestamp = q2mpm_now(c->game);
  q2mpm_set_move(c, "carrier_move_attack_gren");
}
static void reattack_gren_fn(void *ctx, qa_q2m_context *c) {
  coop_check(ctx, c);
  q2mpm_set_move(c, c->entity->has_enemy && qa_q2m_in_front(c, c->entity->enemy)
                    && c->entity->timestamp + 1.3 > q2mpm_now(c->game) ? "carrier_move_attack_gren"
                                                                        : "carrier_move_attack_post_gren");
}
static void prep_spawn_fn(void *ctx, qa_q2m_context *c) {
  carrier_bundle *bundle = ctx;
  coop_check(bundle, c);
  c->state->manual_steering = true;
  c->entity->timestamp = q2mpm_now(c->game);
  c->state->yaw_speed = 10;
  machine_gun(bundle, c);
}
static void start_spawn_fn(void *ctx, qa_q2m_context *c) {
  carrier_bundle *bundle = ctx;
  coop_check(bundle, c);
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  double time = trunc((q2mpm_now(c->game) - c->entity->timestamp) / 0.5);
  double yaw = qa_vector_to_angles_with_rule(qa_sub3_double(enemy.origin, q2mpm_body(c->game, c->entity).origin),
                                             QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  if (time >= 0 && time <= 2) c->state->ideal_yaw = qa_angle_mod_scaled(yaw + (time - 1) * 30);
  machine_gun(bundle, c);
}
static void ready_spawn_fn(void *ctx, qa_q2m_context *c) {
  carrier_bundle *bundle = ctx;
  coop_check(bundle, c);
  machine_gun(bundle, c);
  if (fabs(qa_angle_mod_scaled(q2mpm_body(c->game, c->entity).angles.y) - c->state->ideal_yaw) > 0.1) {
    c->state->hold_frame = true;
    c->entity->timestamp += 0.1;
    return;
  }
  c->state->hold_frame = false;
  qa_vec3 point;
  if (qa_q2mpm_find_rogue_spawn_point(c->game, qa_q2m_project_flash_default(c, q2mpm_v3(105, 0, -58)),
                                      flyer_bounds, 32, &point))
    (void)qa_q2mpm_rogue_spawn_grow(c->game, point, 0);
}
static void spawn_check_fn(void *ctx, qa_q2m_context *c) {
  carrier_bundle *bundle = ctx;
  coop_check(bundle, c);
  machine_gun(bundle, c);
  spawn_child(bundle, c);
  if (q2mpm_now(c->game) > c->entity->timestamp + 1.1) {
    c->state->manual_steering = false;
    c->state->yaw_speed = 15;
  } else {
    c->state->next_frame = qa_q2mpm_rogue_carrier_frames_value.spawn08;
  }
}
static const qa_q2bm_sound_spec sight_spec = {"carrier/sight.wav", 2, 1};
qa_q2m_definition qa_q2mpm_create_carrier_definition(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters, qa_q2mpm_services services) {
  carrier_bundle *bundle = qa_q2mpm_arena_alloc(arena, sizeof *bundle);
  if (!bundle) qa_raise("Error", 0, 0, "Out of memory creating carrier definition");
  bundle->monsters = monsters;
  bundle->services = services;
  qa_q2m_named_handler *callbacks = qa_q2mpm_arena_alloc(arena, 18 * sizeof *callbacks);
  if (!callbacks) qa_raise("Error", 0, 0, "Out of memory creating carrier definition");
  size_t n = 0;
  callbacks[n++] = (qa_q2m_named_handler){"carrier_run", {NULL, run_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"carrier_dead", {NULL, dead_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"BossExplode", {NULL, qa_q2bm_boss_explode_handler}};
  callbacks[n++] = (qa_q2m_named_handler){"CarrierCoopCheck", {bundle, coop_only_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"CarrierMachineGun", {bundle, mg_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"CarrierMachineGunHold", {bundle, mg_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"CarrierRocket", {bundle, rocket_cb_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"CarrierGrenade", {bundle, grenade_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"CarrierSaveLoc", {bundle, save_loc_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"CarrierRail", {bundle, rail_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"carrier_attack_mg", {bundle, attack_mg_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"carrier_reattack_mg", {bundle, reattack_mg_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"carrier_attack_gren", {bundle, attack_gren_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"carrier_reattack_gren", {bundle, reattack_gren_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"carrier_prep_spawn", {bundle, prep_spawn_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"carrier_start_spawn", {bundle, start_spawn_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"carrier_ready_spawn", {bundle, ready_spawn_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"carrier_spawn_check", {bundle, spawn_check_fn}};
  qa_q2m_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.classname = "monster_carrier";
  definition.kind = "carrier";
  definition.model = "models/monsters/carrier/tris.md2";
  definition.health = 2000;
  definition.gib_health = -200;
  definition.mass = 1000;
  definition.bounds.min = q2mpm_v3(-56, -56, -44);
  definition.bounds.max = q2mpm_v3(56, 56, 44);
  definition.scale = 1;
  definition.has_yaw_speed = true;
  definition.yaw_speed = 15;
  definition.has_locomotion = true;
  definition.locomotion = QA_Q2M_LOCO_FLY;
  definition.initial_move = "carrier_move_stand";
  definition.moves = qa_q2mpm_rogue_carrier_moves;
  definition.move_count = qa_q2mpm_rogue_carrier_move_count;
  definition.callbacks = callbacks;
  definition.callback_count = n;
  definition.has_source_callbacks = true;
  definition.source_callbacks = qa_q2mpm_rogue_spawn_callbacks();
  definition.stand = qa_q2bm_move_handler("carrier_move_stand");
  definition.walk = qa_q2bm_move_handler("carrier_move_walk");
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.attack = (qa_q2m_handler){bundle, attack_fn};
  definition.sight = qa_q2bm_sound_handler(&sight_spec);
  definition.initialize = (qa_q2m_handler){NULL, init_fn};
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.check_attack = check_attack_fn;
  qa_q2m_definition wrapped;
  qa_q2_named_think *storage = qa_q2mpm_arena_alloc(arena, (definition.source_callbacks.think_count + 1) * sizeof *storage);
  if (!storage) qa_raise("Error", 0, 0, "Out of memory creating carrier definition");
  qa_q2bm_with_boss_explosion_callbacks(&wrapped, &definition, monsters, storage);
  return wrapped;
}
