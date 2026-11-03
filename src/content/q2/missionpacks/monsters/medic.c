/* Port of src/content/q2/missionpacks/monsters/medic.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/medic.h"

#include <stdio.h>
#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/perception.h"
#include "qa/content/q2/missionpacks/monsters/combat.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/spawn.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_medic.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/foundation/js.h"

static const qa_vec3 cable_offsets[] = {
  {45, -9.2, 15.5}, {48.4, -9.7, 15.2}, {47.8, -9.8, 15.8}, {47.3, -9.3, 14.3}, {45.4, -10.1, 13.1},
  {41.9, -12.7, 12}, {37.8, -15.8, 11.2}, {34.3, -18.4, 10.7}, {32.7, -19.7, 10.4}, {32.7, -19.7, 10.4},
};
static const qa_vec3 reinforcement_positions[] = {
  {80, 0, 0}, {40, 60, 0}, {40, -60, 0}, {0, 80, 0}, {0, -80, 0},
};
static const char *const reinforcements[] = {"monster_soldier_light", "monster_soldier", "monster_soldier_ss",
                                             "monster_infantry", "monster_gunner", "monster_medic", "monster_gladiator"};
static qa_bounds reinforcement_bounds(double index) {
  qa_bounds bounds = {{-32, -32, -24}, {32, 32, 64}};
  return index == 6 ? bounds : qa_q2bm_humanoid_bounds;
}
typedef struct medic_bundle {
  qa_q2m_monsters *monsters;
  qa_q2mpm_weapons weapons;
  qa_q2mpm_monster_state *source;
} medic_bundle;

bool qa_q2mpm_pick_rogue_coop_target(qa_q2m_context *context, qa_actor_id *out) {
  qa_q2_game_services *game = context->game;
  if (game->options.mode != QA_Q2_MODE_COOP) return false;
  size_t player_count = 0;
  const qa_actor_id *all = game->host->players(game->host->ctx, &player_count);
  qa_actor_id *players = malloc((player_count ? player_count : 1) * sizeof *players);
  if (!players) qa_raise("Error", 0, 0, "Out of memory picking coop target");
  size_t count = 0;
  for (size_t i = 0; i < player_count; i++) {
    qa_actor_id actor = all[i];
    if (q2mpm_live(game, actor) && game->host->is_player(game->host->ctx, actor) && qa_q2m_visible(context, &actor))
      players[count++] = actor;
  }
  if (count == 0) { free(players); return false; }
  double pick = qa_js_min((double)count - 1, floor(q2mpm_rnd(game) * (double)count));
  *out = q2mpm_at_a(players, count, pick);
  free(players);
  return true;
}
static void medic_sound(qa_q2m_context *c, const char *normal, const char *commander, double channel,
                        double attenuation) {
  char buffer[64];
  if (qa_q2mpm_monster_mass(c) == 400) snprintf(buffer, sizeof buffer, "medic/%s.wav", normal);
  else snprintf(buffer, sizeof buffer, "medic_commander/%s.wav", commander);
  q2mpm_snd4(c, buffer, channel, 1, attenuation);
}
static void heal_cleanup(medic_bundle *bundle, qa_q2m_context *c, bool change_frame) {
  qa_q2_entity *target = c->entity->has_enemy ? c->game->entity(c->game->ctx, &c->entity->enemy) : NULL;
  if (target) qa_q2mpm_cleanup_rogue_heal_target(bundle->monsters, bundle->source, target, c->game);
  if (change_frame) c->state->next_frame = qa_q2mpm_rogue_medic_frames_value.attack52;
}
static void heal_abort(medic_bundle *bundle, qa_q2m_context *c, bool change_frame, bool gib, bool mark) {
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  heal_cleanup(bundle, c, change_frame);
  qa_q2_entity *target = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
  if (target && mark) {
    qa_q2mpm_rogue_state *patient = qa_q2mpm_state_get(bundle->source, target);
    qa_q2_entity *previous = patient->has_bad_medic1 ? game->entity(game->ctx, &patient->bad_medic1) : NULL;
    if (previous && strncmp(previous->classname, "monster_medic", 13) == 0) {
      patient->has_bad_medic2 = true;
      patient->bad_medic2 = entity->actor.id;
    } else {
      patient->has_bad_medic1 = true;
      patient->bad_medic1 = entity->actor.id;
    }
  }
  if (target && gib) {
    qa_q2m_context *target_context = qa_q2m_monsters_context(bundle->monsters, target->actor.id);
    double threshold = target_context ? target_context->state->gib_health : 0;
    (void)q2mpm_damage(game, target->actor.id, entity, &entity->actor.id, threshold == 0 ? 500 : -threshold, 0,
                       q2mpm_zero(), q2mpm_body(game, target).origin, q2mpm_v3(0, 0, 1), 0, false, 0);
  }
  state->medic = false;
  if (state->has_old_enemy && q2mpm_live(game, state->old_enemy)) {
    entity->has_enemy = true;
    entity->enemy = state->old_enemy;
  } else {
    entity->has_enemy = false;
  }
  qa_q2mpm_state_get(bundle->source, entity)->medic_tries = 0;
}
static qa_q2_entity *find_dead(medic_bundle *bundle, qa_q2m_context *c) {
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2_entity *best = NULL;
  size_t count = 0;
  const qa_actor_id *nearby = game->host->nearby(game->host->ctx, q2mpm_body(game, entity).origin,
                                                 c->state->stand_ground ? 400 : 1024, &count);
  bool has_flies_on = false; qa_q2_think flies_on = qa_q2_think_resolve(game->source_callbacks, "M_FliesOn", &has_flies_on);
  bool has_flies_off = false; qa_q2_think flies_off = qa_q2_think_resolve(game->source_callbacks, "M_FliesOff", &has_flies_off);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = nearby[i];
    qa_q2_entity *target = game->entity(game->ctx, &actor);
    qa_q2m_context *candidate = qa_q2m_monsters_context(bundle->monsters, actor);
    if (!target || target == entity || (qa_js_to_uint32(target->server_flags) & 4u) == 0
        || (candidate && candidate->state->good_guy) || strncmp(target->classname, "player", 6) == 0) continue;
    qa_q2mpm_rogue_state *patient = qa_q2mpm_state_get(bundle->source, target);
    qa_q2_entity *healer = patient->has_healer ? game->entity(game->ctx, &patient->healer) : NULL;
    bool bad_medic = (patient->has_bad_medic1 && qa_same_actor(patient->bad_medic1, entity->actor.id))
        || (patient->has_bad_medic2 && qa_same_actor(patient->bad_medic2, entity->actor.id));
    bool busy_healer = healer && qa_q2m_health(game, &healer->actor.id) > 0
        && (qa_js_to_uint32(healer->server_flags) & 4u) != 0
        && (qa_q2m_monsters_context(bundle->monsters, healer->actor.id) != NULL
            && qa_q2m_monsters_context(bundle->monsters, healer->actor.id)->state->medic);
    if (bad_medic || busy_healer) continue;
    bool flies = (has_flies_on && qa_q2_think_eq(target->think, flies_on))
        || (has_flies_off && qa_q2_think_eq(target->think, flies_off));
    if (qa_q2m_health(game, &actor) > 0 || (target->has_next_think && !flies) || !qa_q2m_visible(c, &actor)) continue;
    qa_vec3 a = q2mpm_body(game, entity).origin;
    qa_vec3 b = q2mpm_body(game, target).origin;
    if (qa_hypot3(qa_sub3_double(a, b)) <= 32) continue;
    if (!best || target->max_health > best->max_health) best = target;
  }
  if (best) entity->timestamp = q2mpm_now(game) + 10;
  return best;
}
static bool acquire_dead(medic_bundle *bundle, qa_q2m_context *c) {
  qa_q2_entity *target = find_dead(bundle, c);
  if (!target) return false;
  c->state->has_old_enemy = c->entity->has_enemy;
  c->state->old_enemy = c->entity->enemy;
  c->entity->has_enemy = true;
  c->entity->enemy = target->actor.id;
  qa_q2mpm_rogue_state *patient = qa_q2mpm_state_get(bundle->source, target);
  patient->has_healer = true;
  patient->healer = c->entity->actor.id;
  c->state->medic = true;
  qa_q2m_monsters_found_target(bundle->monsters, c);
  return true;
}
static void run_move(medic_bundle *bundle, qa_q2m_context *c) {
  qa_q2m_finish_dodge(c);
  if (!c->state->medic && acquire_dead(bundle, c)) return;
  q2mpm_set_move(c, c->state->stand_ground ? "medic_move_stand" : "medic_move_run");
}
static void run_fn(void *ctx, qa_q2m_context *c) { run_move(ctx, c); }
static void idle_move(medic_bundle *bundle, qa_q2m_context *c) {
  medic_sound(c, "idle", "medidle", 2, 2);
  if (!c->state->has_old_enemy) (void)acquire_dead(bundle, c);
}
static void idle_fn(void *ctx, qa_q2m_context *c) { idle_move(ctx, c); }
static void attack_move(medic_bundle *bundle, qa_q2m_context *c) {
  qa_q2m_finish_dodge(c);
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  bool melee_range = qa_q2m_target_distance(c) < 80;
  qa_q2mpm_rogue_state *own = qa_q2mpm_state_get(bundle->source, entity);
  if (own->blocked) {
    q2mpm_set_move(c, "medic_move_callReinforcements");
    own->blocked = false;
  }
  double random = q2mpm_rnd(game);
  bool commander = qa_q2mpm_monster_mass(c) > 400;
  if (state->medic) {
    q2mpm_set_move(c, commander && random > 0.8 && state->monster_slots > 2 ? "medic_move_callReinforcements"
                                                                             : "medic_move_attackCable");
    return;
  }
  q2mpm_set_move(c, state->attack_state == QA_Q2M_ATTACK_BLIND
                    || (commander && random > 0.2 && !melee_range && state->monster_slots > 2)
                        ? "medic_move_callReinforcements" : "medic_move_attackBlaster");
}
static void attack_fn(void *ctx, qa_q2m_context *c) { attack_move(ctx, c); }
static bool attacking(qa_q2m_context *c) {
  const char *name = c->state->move->name;
  return strcmp(name, "medic_move_attackHyperBlaster") == 0 || strcmp(name, "medic_move_attackCable") == 0
      || strcmp(name, "medic_move_attackBlaster") == 0 || strcmp(name, "medic_move_callReinforcements") == 0;
}
static void duck_move(qa_q2m_context *c, double eta) {
  if (c->state->medic) return;
  if (attacking(c)) { c->state->ducked = false; return; }
  c->state->duck_wait = q2mpm_now(c->game) + eta + (c->game->options.skill == 0 ? 1 : 0.1 * (3 - c->game->options.skill));
  qa_q2mpm_rogue_duck_down(c);
  c->state->next_frame = qa_q2mpm_rogue_medic_frames_value.duck1;
  q2mpm_set_move(c, "medic_move_duck");
}
static void duck_cb_fn(void *ctx, qa_q2m_context *c, double eta) { (void)ctx; duck_move(c, eta); }
static void sidestep_move(qa_q2m_context *c) {
  if (attacking(c) && c->game->options.skill != 0) { c->state->dodging = false; return; }
  if (strcmp(c->state->move->name, "medic_move_run") != 0) q2mpm_set_move(c, "medic_move_run");
}
static void sidestep_fn(void *ctx, qa_q2m_context *c) { (void)ctx; sidestep_move(c); }
static void cable_attack(medic_bundle *bundle, qa_q2m_context *c) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  const qa_q2mpm_rogue_medic_frames *f = &qa_q2mpm_rogue_medic_frames_value;
  qa_q2_entity *target = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
  if (!target || (qa_js_to_uint32(target->effects) & 2u) != 0 || game->host->is_player(game->host->ctx, target->actor.id)
      || qa_q2m_health(game, &target->actor.id) > 0) {
    heal_abort(bundle, c, true, false, false);
    return;
  }
  qa_vec3 start = qa_q2m_project_flash_default(c, q2mpm_at_v(cable_offsets, 10, entity->frame - f->attack42));
  qa_body_state target_body = q2mpm_body(game, target);
  if (qa_hypot3(qa_sub3_double(start, target_body.origin)) < 32) {
    heal_abort(bundle, c, true, true, false);
    return;
  }
  qa_trace_result trace = q2mpm_trace(game, start, target_body.origin, NULL, &entity->actor.id, 3);
  if (trace.fraction != 1 && (trace.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(trace.hit.actor, target->actor.id))) {
    if (qa_q2mpm_source_trace_world(game, trace)) {
      if (qa_q2mpm_state_get(bundle->source, entity)->medic_tries > 1) {
        heal_abort(bundle, c, true, false, true);
        return;
      }
      qa_q2mpm_state_get(bundle->source, entity)->medic_tries++;
      heal_cleanup(bundle, c, true);
      return;
    }
    heal_abort(bundle, c, true, false, false);
    return;
  }
  if (entity->frame == f->attack43) {
    game->sound(game->ctx, target->actor, qa_q2mpm_monster_mass(c) == 400 ? "medic/medatck3.wav" : "medic_commander/medatck3a.wav", 0, 1, 1);
    qa_q2m_context *patient = qa_q2m_monsters_context(bundle->monsters, target->actor.id);
    if (patient) {
      patient->state->resurrecting = true;
      patient->state->can_take_damage = false;
      qa_q2mpm_rogue_heal_effects(patient);
    }
    q2mpm_set_damageable(game, target, false);
  } else if (entity->frame == f->attack50) {
    target->spawnflags = 0;
    target->target = "";
    target->targetname = "";
    target->combat_target = "";
    target->death_target = "";
    qa_q2m_context *previous = qa_q2m_monsters_context(bundle->monsters, target->actor.id);
    if (previous) {
      qa_q2m_state *flags = previous->state;
      flags->ignore_shots = false;
      flags->do_not_count = false;
      flags->spawned_by = QA_Q2M_SPAWNED_NONE;
      flags->good_guy = false;
      flags->target_anger = false;
      flags->brutal = false;
      flags->medic = false;
      flags->resurrecting = false;
      flags->stand_ground = false;
      flags->temporary_stand_ground = false;
      flags->hold_frame = false;
      flags->ducked = false;
      flags->dodging = false;
      flags->charging = false;
      flags->manual_steering = false;
      flags->combat_point = false;
      flags->lost_sight = false;
      flags->pursue_next = false;
      flags->pursue_temporary = false;
      flags->pursuit_last_seen = false;
      flags->has_sound_target = false;
    }
    qa_q2mpm_rogue_state *patient = qa_q2mpm_state_get(bundle->source, target);
    patient->has_healer = true;
    patient->healer = entity->actor.id;
    qa_bounds clear_bounds = target_body.bounds;
    clear_bounds.max.z += 48;
    qa_trace_result clear = q2mpm_trace(game, target_body.origin, target_body.origin, &clear_bounds,
                                        &target->actor.id, qa_q2m_monster_solid_mask(game));
    if (clear.start_solid || clear.all_solid || !qa_q2mpm_source_trace_world(game, clear)) {
      heal_abort(bundle, c, true, true, false);
      return;
    }
    if (previous) previous->state->do_not_count = true;
    qa_q2m_context *revived = qa_q2m_monsters_respawn(bundle->monsters, target, game);
    if (target->think.fn) {
      target->has_next_think = true;
      target->next_think = q2mpm_now(game);
      target->think.fn(target->think.ctx, target, game);
    }
    revived->state->resurrecting = false;
    revived->state->ignore_shots = true;
    revived->state->do_not_count = true;
    target->effects = q2mpm_and(target->effects, ~0x4000);
    qa_q2mpm_state_get(bundle->source, target)->has_healer = false;
    if (state->has_old_enemy && q2mpm_live(game, state->old_enemy)
        && qa_q2m_health(game, &state->old_enemy) > 0) {
      target->has_enemy = true;
      target->enemy = state->old_enemy;
      qa_q2m_monsters_found_target(bundle->monsters, revived);
    } else {
      target->has_enemy = false;
      if (!revived->find_target(revived->impl_ctx, revived)) {
        revived->state->pause_time = q2mpm_now(game) + 100000000;
        revived->stand(revived->impl_ctx, revived);
      }
      entity->has_enemy = false;
      state->has_old_enemy = false;
      if (!c->find_target(c->impl_ctx, c)) {
        state->pause_time = q2mpm_now(game) + 100000000;
        c->stand(c->impl_ctx, c);
        return;
      }
    }
  } else if (entity->frame == f->attack44) {
    medic_sound(c, "medatck4", "medatck4a", 1, 1);
  }
  qa_q2_entity *current = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
  if (!current) return;
  qa_body_state body = q2mpm_body(game, current);
  qa_q2_presentation_event beam;
  memset(&beam, 0, sizeof beam);
  beam.kind = "monster-beam";
  beam.effect = "medic";
  beam.has_actor = true; beam.actor = entity->actor.id;
  beam.start = qa_add3_double(start, qa_scale3_double(
      qa_math_angle_vectors_rule(q2mpm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).forward, 8));
  beam.end = body.origin;
  beam.end.z += (body.bounds.min.z + body.bounds.max.z) / 2;
  game->host->emit(game->host->ctx, beam);
}
static void cable_fn(void *ctx, qa_q2m_context *c) { cable_attack(ctx, c); }
/* TS eachSpawn iteration step: returns spawn count for i, or -1 when
 * no spawn point exists (visit not called). */
static double spawn_count_for(medic_bundle *bundle, qa_q2m_context *c) {
  double strength = qa_q2mpm_state_get(bundle->source, c->entity)->summon_strength;
  return strength == 0 ? 1 : strength - 1 + fmod(strength, 2);
}
static bool spawn_step(medic_bundle *bundle, qa_q2m_context *c, bool behind, double i, qa_vec3 *out_point,
                       qa_bounds *out_bounds, double *out_index) {
  double strength = qa_q2mpm_state_get(bundle->source, c->entity)->summon_strength;
  double index = strength - i - fmod(i, 2);
  qa_vec3 position = q2mpm_at_v(reinforcement_positions, 5, i);
  qa_bounds bounds = reinforcement_bounds(index);
  qa_vec3 offset = behind ? q2mpm_v3(-position.x, -position.y, position.z) : position;
  qa_vec3 point = qa_q2m_project_flash_default(c, offset);
  point.z += 10;
  qa_vec3 spawn;
  if (!qa_q2mpm_find_rogue_spawn_point(c->game, point, bounds, 32, &spawn)) return false;
  *out_point = spawn;
  *out_bounds = bounds;
  *out_index = index;
  return true;
}
static void hook_launch_fn(void *ctx, qa_q2m_context *c) { (void)ctx; medic_sound(c, "medatck2", "medatck2c", 1, 1); }
static void hook_retract_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  medic_sound(c, "medatck5", "medatck5a", 1, 1);
  c->state->medic = false;
  if (c->state->has_old_enemy && q2mpm_live(c->game, c->state->old_enemy)) {
    c->entity->has_enemy = true;
    c->entity->enemy = c->state->old_enemy;
  } else {
    c->entity->has_enemy = false;
    c->state->has_old_enemy = false;
    if (!c->find_target(c->impl_ctx, c)) {
      c->state->pause_time = q2mpm_now(c->game) + 100000000;
      c->stand(c->impl_ctx, c);
    }
  }
}
static void continue_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2m_visible_enemy(c) && q2mpm_rnd(c->game) <= 0.95) q2mpm_set_move(c, "medic_move_attackHyperBlaster");
}
static void fire_blaster_fn(void *ctx, qa_q2m_context *c) {
  medic_bundle *bundle = ctx;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, 60, &aim)) return;
  const qa_q2mpm_rogue_medic_frames *f = &qa_q2mpm_rogue_medic_frames_value;
  double frame = c->entity->frame;
  double effects = frame == f->attack9 || frame == f->attack12 ? 8
      : frame == f->attack19 || frame == f->attack22 || frame == f->attack25 || frame == f->attack28 ? 64 : 0;
  qa_q2_entity *enemy = c->entity->has_enemy ? c->game->entity(c->game->ctx, &c->entity->enemy) : NULL;
  const char *tesla = q2mpm_rr(c->game) ? "tesla_mine" : "tesla";
  double damage = enemy && strcmp(enemy->classname, tesla) == 0 ? 3 : 2;
  bool commander = qa_q2mpm_monster_mass(c) > 400;
  if (commander)
    (void)bundle->weapons.fire_blaster2(bundle->weapons.ctx, c->entity, c->game, aim.start, aim.direction, damage, 1000, effects);
  else
    (void)c->weapons.fire_blaster(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, damage, 1000, effects,
                                  false, false, 0);
  qa_q2rm_monster_flash(c, commander ? 146 : 60, aim.start, aim.direction);
}
static void start_spawn_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_snd(c, "medic_commander/monsterspawn1.wav", 1);
  c->state->next_frame = qa_q2mpm_rogue_medic_frames_value.attack48;
}
static void determine_spawn_fn(void *ctx, qa_q2m_context *c) {
  medic_bundle *bundle = ctx;
  double lucky = q2mpm_rnd(c->game);
  double strength = c->game->options.skill + (lucky < 0.05 ? -3 : lucky < 0.15 ? -2 : lucky < 0.3 ? -1
                                               : lucky > 0.95 ? 3 : lucky > 0.85 ? 2 : lucky > 0.7 ? 1 : 0);
  qa_q2mpm_state_get(bundle->source, c->entity)->summon_strength = qa_js_max(0, strength);
  bool success = false;
  double count = spawn_count_for(bundle, c);
  for (double i = 0; i < count; i++) {
    qa_vec3 point; qa_bounds bounds; double index;
    if (!spawn_step(bundle, c, false, i, &point, &bounds, &index)) continue;
    success = qa_q2mpm_check_rogue_ground_spawn_point(c->game, point, bounds, 256, -1);
    if (success) break;
  }
  if (!success) {
    for (double i = 0; i < count; i++) {
      qa_vec3 point; qa_bounds bounds; double index;
      if (!spawn_step(bundle, c, true, i, &point, &bounds, &index)) continue;
      success = qa_q2mpm_check_rogue_ground_spawn_point(c->game, point, bounds, 256, -1);
      if (success) break;
    }
    if (success) {
      c->state->manual_steering = true;
      c->state->ideal_yaw = qa_angle_mod_scaled(q2mpm_body(c->game, c->entity).angles.y) + 180;
      if (c->state->ideal_yaw > 360) c->state->ideal_yaw -= 360;
    }
  }
  if (!success) c->state->next_frame = qa_q2mpm_rogue_medic_frames_value.attack53;
}
static void spawngrows_fn(void *ctx, qa_q2m_context *c) {
  medic_bundle *bundle = ctx;
  if (c->state->manual_steering) {
    if (fabs(qa_angle_mod_scaled(q2mpm_body(c->game, c->entity).angles.y) - c->state->ideal_yaw) > 0.1) {
      c->state->hold_frame = true;
      return;
    }
    c->state->hold_frame = false;
    c->state->manual_steering = false;
  }
  bool success = false;
  double count = spawn_count_for(bundle, c);
  for (double i = 0; i < count; i++) {
    qa_vec3 point; qa_bounds bounds; double index;
    if (!spawn_step(bundle, c, false, i, &point, &bounds, &index)) continue;
    if (qa_q2mpm_check_rogue_ground_spawn_point(c->game, point, bounds, 256, -1)) {
      success = true;
      (void)qa_q2mpm_rogue_spawn_grow(c->game, point, index > 3 ? 1 : 0);
    }
  }
  if (!success) c->state->next_frame = qa_q2mpm_rogue_medic_frames_value.attack53;
}
static void finish_spawn_fn(void *ctx, qa_q2m_context *c) {
  medic_bundle *bundle = ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  qa_q2mpm_rogue_state *own = qa_q2mpm_state_get(bundle->source, entity);
  own->summon_strength = fabs(own->summon_strength);
  double count = spawn_count_for(bundle, c);
  for (double i = 0; i < count; i++) {
    qa_vec3 point; qa_bounds bounds; double index;
    if (!spawn_step(bundle, c, false, i, &point, &bounds, &index)) continue;
    if (!qa_q2mpm_check_rogue_spawn_point(game, point, bounds)) continue;
    qa_q2m_monsters * monsters2 = bundle->monsters;
    qa_vec3 origin = point;
    qa_vec3 angles2 = q2mpm_body(game, entity).angles;
    qa_bounds bounds2 = bounds;
    const char * classname = q2mpm_at_s(reinforcements, 7, index);
    qa_q2_entity *child = qa_q2mpm_create_rogue_ground_monster(monsters2, game, origin,
                                                                 angles2, bounds2,
                                                                 classname, 256);
    if (!child) continue;
    if (child->think.fn) {
      child->has_next_think = true;
      child->next_think = q2mpm_now(game);
      child->think.fn(child->think.ctx, child, game);
    }
    qa_q2m_context *child_context = qa_q2m_monsters_context(bundle->monsters, child->actor.id);
    if (!child_context) qa_raise("Error", 0, 0, "Medic reinforcement has no shared controller");
    child_context->state->ignore_shots = true;
    child_context->state->do_not_count = true;
    child_context->state->spawned_by = QA_Q2M_SPAWNED_MEDIC;
    child_context->state->has_commander = true;
    child_context->state->commander = entity->actor.id;
    state->monster_slots--;
    bool has_enemy = false;
    qa_actor_id enemy = entity->enemy;
    if (state->medic) { has_enemy = state->has_old_enemy; enemy = state->old_enemy; }
    else has_enemy = entity->has_enemy;
    if (game->options.mode == QA_Q2_MODE_COOP) {
      qa_actor_id picked;
      bool got = qa_q2mpm_pick_rogue_coop_target(child_context, &picked);
      if (got) { has_enemy = true; enemy = picked; } else has_enemy = false;
      if (has_enemy && entity->has_enemy && qa_same_actor(enemy, entity->enemy)) {
        got = qa_q2mpm_pick_rogue_coop_target(child_context, &picked);
        if (got) { has_enemy = true; enemy = picked; } else has_enemy = false;
      }
      if (!has_enemy) { has_enemy = entity->has_enemy; enemy = entity->enemy; }
    }
    if (has_enemy && q2mpm_live(game, enemy) && qa_q2m_health(game, &enemy) > 0) {
      child->has_enemy = true;
      child->enemy = enemy;
      qa_q2m_monsters_found_target(bundle->monsters, child_context);
    } else {
      child->has_enemy = false;
      child_context->stand(child_context->impl_ctx, child_context);
    }
  }
}
static void done_dodge_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_finish_dodge(c); }
static void sight_fn(void *ctx, qa_q2m_context *c) { (void)ctx; medic_sound(c, "medsght1", "medsght", 2, 1); }
static void search_fn(void *ctx, qa_q2m_context *c) {
  medic_bundle *bundle = ctx;
  medic_sound(c, "medsrch1", "medsrch", 2, 2);
  if (!c->state->has_old_enemy) (void)acquire_dead(bundle, c);
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->ignore_shots = true;
  if (qa_q2mpm_monster_mass(c) > 400) {
    c->entity->skin = 2;
    double skill = c->game->options.skill;
    c->state->monster_slots = skill == 0 ? 3 : skill == 1 ? 4 : 6;
  }
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  bool commander = qa_q2mpm_monster_mass(c) > 400;
  qa_q2m_finish_dodge(c);
  if (qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2) entity->skin = commander ? 3 : 1;
  if (q2mpm_now(game) < state->pain_time) return;
  state->pain_time = q2mpm_now(game) + 3;
  if (game->options.skill == 3 || state->medic) return;
  if (commander) {
    if (reaction.damage < 35) { medic_sound(c, "medpain1", "medpain1", 2, 1); return; }
    state->manual_steering = false;
    state->hold_frame = false;
    medic_sound(c, "medpain2", "medpain2", 2, 1);
    q2mpm_set_move(c, q2mpm_rnd(game) < qa_js_min(reaction.damage * 0.005, 0.5) ? "medic_move_pain2" : "medic_move_pain1");
  } else {
    bool first = q2mpm_rnd(game) < 0.5;
    q2mpm_set_move(c, first ? "medic_move_pain1" : "medic_move_pain2");
    medic_sound(c, first ? "medpain1" : "medpain2", "medpain2", 2, 1);
  }
  if (state->ducked) qa_q2mpm_rogue_duck_up(c);
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2bm_begin_death_default(c, reaction,
      qa_q2mpm_monster_mass(c) == 400 ? "medic/meddeth1.wav" : "medic_commander/meddeth.wav", "medic_move_death");
}
static void dodge_fn(void *ctx, qa_q2m_context *c, qa_actor_id attacker, double eta, const qa_trace_result *trace,
                     bool gravity) {
  medic_bundle *bundle = ctx;
  (void)gravity;
  qa_q2mpm_duck_fn duck = {NULL, duck_cb_fn};
  qa_q2m_handler sidestep = {NULL, sidestep_fn};
  qa_q2mpm_rogue_monster_dodge(c, bundle->monsters, attacker, eta, trace, duck, sidestep);
}
static bool duck_field_fn(void *ctx, qa_q2m_context *c, double eta) { (void)ctx; duck_move(c, eta); return true; }
static bool sidestep_field_fn(void *ctx, qa_q2m_context *c) { (void)ctx; sidestep_move(c); return true; }
static bool blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  medic_bundle *bundle = ctx;
  return qa_q2mpm_rogue_blocked_check_shot(c, 0.25 + 0.05 * c->game->options.skill, bundle->source)
      || qa_q2rm_blocked_check_platform(c, distance);
}
static bool check_attack_fn(void *ctx, qa_q2m_context *c) {
  medic_bundle *bundle = ctx;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  if (state->medic) {
    if (!entity->has_enemy || !q2mpm_live(game, entity->enemy)) {
      heal_abort(bundle, c, true, false, false);
      return false;
    }
    if (entity->timestamp < q2mpm_now(game)) {
      heal_abort(bundle, c, true, false, true);
      entity->timestamp = 0;
      return false;
    }
    if (qa_q2m_target_distance(c) < 410) {
      attack_move(bundle, c);
      return true;
    }
    state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
    return false;
  }
  if (entity->has_enemy && game->host->is_player(game->host->ctx, entity->enemy) && !qa_q2m_visible_enemy(c)
      && state->monster_slots > 2) {
    state->attack_state = QA_Q2M_ATTACK_BLIND;
    return true;
  }
  if (q2mpm_rnd(game) < 0.8 && state->monster_slots > 5 && qa_q2m_target_distance(c) > 150) {
    qa_q2mpm_state_get(bundle->source, entity)->blocked = true;
    state->attack_state = QA_Q2M_ATTACK_MISSILE;
    return true;
  }
  if (game->options.skill > 0 && state->stand_ground) {
    state->attack_state = QA_Q2M_ATTACK_MISSILE;
    return true;
  }
  return qa_q2m_default_check_attack(c, NULL);
}
size_t qa_q2mpm_create_rogue_medic_definitions(qa_q2mpm_arena *arena, qa_q2m_definition *out, qa_q2m_monsters *monsters,
                                               qa_q2mpm_weapons weapons, qa_q2mpm_monster_state *source) {
  medic_bundle *bundle = qa_q2mpm_arena_alloc(arena, sizeof *bundle);
  if (!bundle) qa_raise("Error", 0, 0, "Out of memory creating rogue medic definitions");
  bundle->monsters = monsters;
  bundle->weapons = weapons;
  bundle->source = source;
  qa_q2m_named_handler *callbacks = qa_q2mpm_arena_alloc(arena, 16 * sizeof *callbacks);
  if (!callbacks) qa_raise("Error", 0, 0, "Out of memory creating rogue medic definitions");
  size_t n = 0;
  callbacks[n++] = (qa_q2m_named_handler){"medic_idle", {bundle, idle_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"medic_run", {bundle, run_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"medic_dead", {NULL, qa_q2bm_finish_corpse_handler}};
  callbacks[n++] = (qa_q2m_named_handler){"monster_done_dodge", {NULL, done_dodge_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"monster_duck_down", {NULL, qa_q2mpm_rogue_duck_down_handler}};
  callbacks[n++] = (qa_q2m_named_handler){"monster_duck_hold", {NULL, qa_q2mpm_rogue_duck_hold_handler}};
  callbacks[n++] = (qa_q2m_named_handler){"monster_duck_up", {NULL, qa_q2mpm_rogue_duck_up_handler}};
  callbacks[n++] = (qa_q2m_named_handler){"medic_hook_launch", {NULL, hook_launch_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"medic_hook_retract", {NULL, hook_retract_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"medic_cable_attack", {bundle, cable_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"medic_continue", {NULL, continue_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"medic_fire_blaster", {bundle, fire_blaster_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"medic_start_spawn", {NULL, start_spawn_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"medic_determine_spawn", {bundle, determine_spawn_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"medic_spawngrows", {bundle, spawngrows_fn}};
  callbacks[n++] = (qa_q2m_named_handler){"medic_finish_spawn", {bundle, finish_spawn_fn}};
  qa_q2m_definition base;
  memset(&base, 0, sizeof base);
  base.classname = "monster_medic";
  base.kind = "medic";
  base.model = "models/monsters/medic/tris.md2";
  base.health = 300;
  base.gib_health = -130;
  base.mass = 400;
  base.bounds.min = q2mpm_v3(-24, -24, -24);
  base.bounds.max = q2mpm_v3(24, 24, 32);
  base.scale = 1;
  base.initial_move = "medic_move_stand";
  base.moves = qa_q2mpm_rogue_medic_moves;
  base.move_count = qa_q2mpm_rogue_medic_move_count;
  base.callbacks = callbacks;
  base.callback_count = n;
  base.has_source_callbacks = true;
  base.source_callbacks = qa_q2mpm_rogue_spawn_callbacks();
  base.stand = qa_q2bm_move_handler("medic_move_stand");
  base.walk = qa_q2bm_move_handler("medic_move_walk");
  base.run = (qa_q2m_handler){bundle, run_fn};
  base.attack = (qa_q2m_handler){bundle, attack_fn};
  base.idle = (qa_q2m_handler){bundle, idle_fn};
  base.sight = (qa_q2m_handler){NULL, sight_fn};
  base.search = (qa_q2m_handler){bundle, search_fn};
  base.initialize = (qa_q2m_handler){NULL, init_fn};
  base.reactions.pain = pain_fn;
  base.reactions.die = die_fn;
  base.dodge_ctx = bundle;
  base.dodge = dodge_fn;
  base.duck = duck_field_fn;
  base.sidestep = sidestep_field_fn;
  base.blocked_ctx = bundle;
  base.blocked = blocked_fn;
  base.check_attack_ctx = bundle;
  base.check_attack = check_attack_fn;
  out[0] = base;
  out[1] = base;
  out[1].classname = "monster_medic_commander";
  out[1].kind = "medic_commander";
  out[1].health = 600;
  out[1].mass = 600;
  out[1].has_yaw_speed = true;
  out[1].yaw_speed = 40;
  return 2;
}
