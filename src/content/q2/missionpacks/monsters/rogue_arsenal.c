/* Port of src/content/q2/missionpacks/monsters/rogue-arsenal.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/rogue_arsenal.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/chick.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/tank.h"
#include "qa/content/q2/base/monsters/tank.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_chick.h"
#include "qa/content/q2/rerelease/monsters/common.h"

typedef struct arsenal_bundle {
  qa_q2m_monsters *monsters;
  qa_q2mpm_monster_state *source;
  qa_q2m_handler machine_gun;
} arsenal_bundle;
static void rocket(qa_q2m_context *c, double flash, double head_chance, double blind_offset) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy) || !entity->has_enemy) return;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, flash));
  qa_vec3 right = qa_math_angle_vectors_rule(q2mpm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).right;
  double speed = 500 + 100 * game->options.skill;
  bool blind = state->manual_steering;
  qa_vec3 target = blind ? state->blind_fire_target : enemy.origin;
  qa_vec3 point = target;
  if (!blind) {
    qa_q2_entity *enemy_entity = game->entity(game->ctx, &entity->enemy);
    double view_height = enemy_entity ? enemy_entity->view_height : 22;
    point.z = q2mpm_rnd(game) < head_chance || start.z < enemy.origin.z + enemy.bounds.min.z
        ? target.z + view_height : enemy.origin.z + enemy.bounds.min.z;
  }
  if (!blind && q2mpm_rnd(game) < 0.2 + (3 - game->options.skill) * 0.15)
    point = qa_add3_double(point, qa_scale3_double(enemy.velocity, qa_hypot3(qa_sub3_double(point, start)) / speed));
  qa_trace_result trace = q2mpm_trace(game, start, point, NULL, &entity->actor.id, 0x6000003);
  if (blind) {
    if (trace.start_solid || trace.all_solid || trace.fraction < 0.5) {
      point = qa_add3_double(target, qa_scale3_double(right, -blind_offset));
      trace = q2mpm_trace(game, start, point, NULL, &entity->actor.id, 0x6000003);
      if (trace.start_solid || trace.all_solid || trace.fraction < 0.5) {
        point = qa_add3_double(target, qa_scale3_double(right, blind_offset));
        trace = q2mpm_trace(game, start, point, NULL, &entity->actor.id, 0x6000003);
        if (trace.start_solid || trace.all_solid || trace.fraction < 0.5) return;
      }
    }
  } else {
    trace = q2mpm_trace(game, start, point, NULL, &entity->actor.id, 0x6000003);
    bool hit_enemy = trace.hit.kind == QA_TRACE_HIT_ACTOR && qa_same_actor(trace.hit.actor, entity->enemy);
    bool ok_first = hit_enemy || qa_q2mpm_source_trace_world(game, trace);
    bool ok_second = trace.fraction > 0.5
        || (trace.hit.kind == QA_TRACE_HIT_ACTOR && game->host->is_player(game->host->ctx, trace.hit.actor));
    if (!ok_first || !ok_second) return;
  }
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(point, start));
  (void)c->weapons.fire_rocket(c->weapons.ctx, entity, game, start, direction, 50, speed, 70, 50);
  qa_q2rm_monster_flash(c, blind ? flash : 57, start, direction);
}
static double blind_chance(qa_q2m_context *c) {
  return c->state->blind_fire_delay < 1 ? 1 : c->state->blind_fire_delay < 7.5 ? 0.4 : 0.1;
}
static bool zero_target(qa_q2m_context *c) {
  qa_vec3 point = c->state->blind_fire_target;
  return point.x == 0 && point.y == 0 && point.z == 0;
}
static bool blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  arsenal_bundle *bundle = ctx;
  return qa_q2mpm_rogue_blocked_check_shot(c, 0.25 + 0.05 * c->game->options.skill, bundle->source)
      || qa_q2rm_blocked_check_platform(c, distance);
}
static void ignore_shots_fn(void *ctx, qa_q2m_context *c) { (void)ctx; c->state->ignore_shots = true; }
static void tank_pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  const qa_q2bm_tank_frames *f = &qa_q2bm_tank_frames_value;
  if (qa_q2m_health(c->game, &c->entity->actor.id) < c->entity->max_health / 2)
    c->entity->skin = q2mpm_or(c->entity->skin, 1);
  if (reaction.damage <= 10 || q2mpm_now(c->game) < c->state->pain_time
      || (reaction.damage <= 30 && q2mpm_rnd(c->game) > 0.2)) return;
  double frame = c->entity->frame;
  if (c->game->options.skill >= 2 && ((frame >= f->attak301 && frame <= f->attak330)
                                      || (frame >= f->attak101 && frame <= f->attak116))) return;
  c->state->pain_time = q2mpm_now(c->game) + 3;
  q2mpm_snd(c, "tank/tnkpain2.wav", 2);
  if (c->game->options.skill == 3) return;
  c->state->manual_steering = false;
  q2mpm_set_move(c, reaction.damage <= 30 ? "tank_move_pain1" : reaction.damage <= 60 ? "tank_move_pain2"
                                                                                       : "tank_move_pain3");
}
static void tank_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  if (!entity->has_enemy || !q2mpm_live(game, entity->enemy)) return;
  if (qa_q2m_health(game, &entity->enemy) < 0) {
    state->brutal = false;
    q2mpm_set_move(c, "tank_move_attack_strike");
    return;
  }
  if (state->attack_state != QA_Q2M_ATTACK_BLIND) {
    if (qa_q2bm_tank_definition.attack.fn) qa_q2bm_tank_definition.attack.fn(qa_q2bm_tank_definition.attack.ctx, c);
    return;
  }
  double chance = blind_chance(c);
  double random = q2mpm_rnd(game);
  state->blind_fire_delay += 5.2 + q2mpm_rnd(game) * 3;
  if (zero_target(c) || random > chance) return;
  state->manual_steering = true;
  q2mpm_set_move(c, "tank_move_attack_fire_rocket");
  double left = q2mpm_now(game) + 3;
  double right = 2 * q2mpm_rnd(game);
  state->attack_finished = left + right;
  state->pain_time = q2mpm_now(game) + 5;
}
static void tank_rocket_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  const qa_q2bm_tank_frames *f = &qa_q2bm_tank_frames_value;
  rocket(c, c->entity->frame == f->attak324 ? 23 : c->entity->frame == f->attak327 ? 24 : 25, 0.66, 20);
}
static void tank_mg_fn(void *ctx, qa_q2m_context *c) {
  arsenal_bundle *bundle = ctx;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  bundle->machine_gun.fn(bundle->machine_gun.ctx, c);
}
static void tank_refire_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->manual_steering) {
    c->state->manual_steering = false;
    q2mpm_set_move(c, "tank_move_attack_post_rocket");
    return;
  }
  q2mpm_set_move(c, c->game->options.skill >= 2 && qa_q2bm_alive_enemy(c) && qa_q2m_visible_enemy(c)
                    && q2mpm_rnd(c->game) <= 0.4 ? "tank_move_attack_fire_rocket" : "tank_move_attack_post_rocket");
}
static void chick_run_move(qa_q2m_context *c) {
  qa_q2m_finish_dodge(c);
  if (qa_q2bm_chick_definition.run.fn) qa_q2bm_chick_definition.run.fn(qa_q2bm_chick_definition.run.ctx, c);
}
static void chick_run_fn(void *ctx, qa_q2m_context *c) { (void)ctx; chick_run_move(c); }
static bool chick_shooting(qa_q2m_context *c) {
  return strcmp(c->state->move->name, "chick_move_start_attack1") == 0
      || strcmp(c->state->move->name, "chick_move_attack1") == 0;
}
static void chick_duck_move(qa_q2m_context *c, double eta) {
  if (chick_shooting(c) && c->game->options.skill != 0) { c->state->ducked = false; return; }
  c->state->duck_wait = q2mpm_now(c->game) + eta + (c->game->options.skill == 0 ? 1 : 0.1 * (3 - c->game->options.skill));
  qa_q2mpm_rogue_duck_down(c);
  c->state->next_frame = qa_q2mpm_rogue_chick_frames_value.duck01;
  q2mpm_set_move(c, "chick_move_duck");
}
static void chick_duck_cb(void *ctx, qa_q2m_context *c, double eta) { (void)ctx; chick_duck_move(c, eta); }
static void chick_sidestep_move(qa_q2m_context *c) {
  if (chick_shooting(c) && c->game->options.skill != 0) { c->state->dodging = false; return; }
  if (strcmp(c->state->move->name, "chick_move_run") != 0) q2mpm_set_move(c, "chick_move_run");
}
static void chick_sidestep_fn(void *ctx, qa_q2m_context *c) { (void)ctx; chick_sidestep_move(c); }
static void chick_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_finish_dodge(c);
  if (c->state->attack_state == QA_Q2M_ATTACK_BLIND) {
    double chance = blind_chance(c);
    double random = q2mpm_rnd(c->game);
    c->state->blind_fire_delay += 5.5 + q2mpm_rnd(c->game);
    if (zero_target(c) || random > chance) return;
    c->state->manual_steering = true;
    q2mpm_set_move(c, "chick_move_start_attack1");
    double left = q2mpm_now(c->game);
    double right = 2 * q2mpm_rnd(c->game);
    c->state->attack_finished = left + right;
    return;
  }
  q2mpm_set_move(c, "chick_move_start_attack1");
}
static void chick_pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2m_finish_dodge(c);
  qa_q2bm_damaged_skin(c);
  if (q2mpm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2mpm_now(c->game) + 3;
  double random = q2mpm_rnd(c->game);
  q2mpm_snd(c, random < 0.33 ? "chick/chkpain1.wav" : random < 0.66 ? "chick/chkpain2.wav" : "chick/chkpain3.wav", 2);
  if (c->game->options.skill == 3) return;
  c->state->manual_steering = false;
  q2mpm_set_move(c, reaction.damage <= 10 ? "chick_move_pain1" : reaction.damage <= 25 ? "chick_move_pain2"
                                                                                        : "chick_move_pain3");
  if (c->state->ducked) qa_q2mpm_rogue_duck_up(c);
}
static void chick_dodge_fn(void *ctx, qa_q2m_context *c, qa_actor_id attacker, double eta,
                           const qa_trace_result *trace, bool gravity) {
  arsenal_bundle *bundle = ctx;
  (void)gravity;
  qa_q2mpm_duck_fn duck = {NULL, chick_duck_cb};
  qa_q2m_handler sidestep = {NULL, chick_sidestep_fn};
  qa_q2mpm_rogue_monster_dodge(c, bundle->monsters, attacker, eta, trace, duck, sidestep);
}
static bool chick_duck_field(void *ctx, qa_q2m_context *c, double eta) { (void)ctx; chick_duck_move(c, eta); return true; }
static bool chick_sidestep_field(void *ctx, qa_q2m_context *c) { (void)ctx; chick_sidestep_move(c); return true; }
static void chick_rocket_fn(void *ctx, qa_q2m_context *c) { (void)ctx; rocket(c, 57, 0.33, 10); }
static void chick_rerocket_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->manual_steering) {
    c->state->manual_steering = false;
    q2mpm_set_move(c, "chick_move_end_attack1");
    return;
  }
  q2mpm_set_move(c, qa_q2bm_alive_enemy(c) && qa_q2m_target_distance(c) >= 80 && qa_q2m_visible_enemy(c)
                    && q2mpm_rnd(c->game) <= 0.6 + 0.05 * c->game->options.skill ? "chick_move_attack1"
                                                                                    : "chick_move_end_attack1");
}
static void commander_init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->ignore_shots = true;
  c->entity->skin = 2;
}
size_t qa_q2mpm_create_rogue_arsenal_monsters(qa_q2mpm_arena *arena, qa_q2m_definition *out, qa_q2m_monsters *monsters,
                                              qa_q2mpm_monster_state *source) {
  const qa_q2m_handler *machine_gun = qa_q2mpm_find_callback(qa_q2bm_tank_definition.callbacks,
                                                             qa_q2bm_tank_definition.callback_count, "TankMachineGun");
  if (!machine_gun) qa_raise("Error", 0, 0, "Base Tank is missing its source machine gun callback");
  arsenal_bundle *bundle = qa_q2mpm_arena_alloc(arena, sizeof *bundle);
  if (!bundle) qa_raise("Error", 0, 0, "Out of memory creating rogue arsenal monsters");
  bundle->monsters = monsters;
  bundle->source = source;
  bundle->machine_gun = *machine_gun;
  qa_q2m_definition tank = qa_q2bm_tank_definition;
  tank.has_blind_fire_flag = true;
  tank.blind_fire = true;
  tank.blocked_ctx = bundle;
  tank.blocked = blocked_fn;
  tank.initialize = (qa_q2m_handler){NULL, ignore_shots_fn};
  tank.reactions.ctx = NULL;
  tank.reactions.pain = tank_pain_fn;
  tank.attack = (qa_q2m_handler){NULL, tank_attack_fn};
  {
    const qa_q2m_named_handler extra[] = {
      {"TankRocket", {NULL, tank_rocket_fn}},
      {"TankMachineGun", {bundle, tank_mg_fn}},
      {"tank_refire_rocket", {NULL, tank_refire_fn}},
    };
    tank.callbacks = qa_q2mpm_merge_callbacks(arena, tank.callbacks, tank.callback_count, extra, 3, &tank.callback_count);
  }
  out[0] = tank;
  out[1] = tank;
  out[1].classname = "monster_tank_commander";
  out[1].health = 1000;
  out[1].gib_health = -225;
  out[1].initialize = (qa_q2m_handler){NULL, commander_init_fn};
  qa_q2m_definition chick = qa_q2bm_chick_definition;
  chick.model = "models/monsters/bitch2/tris.md2";
  chick.moves = qa_q2mpm_rogue_chick_moves;
  chick.move_count = qa_q2mpm_rogue_chick_move_count;
  chick.run = (qa_q2m_handler){NULL, chick_run_fn};
  chick.blocked_ctx = bundle;
  chick.blocked = blocked_fn;
  chick.has_blind_fire_flag = true;
  chick.blind_fire = true;
  chick.attack = (qa_q2m_handler){NULL, chick_attack_fn};
  chick.reactions.ctx = NULL;
  chick.reactions.pain = chick_pain_fn;
  chick.dodge_ctx = bundle;
  chick.dodge = chick_dodge_fn;
  chick.duck = chick_duck_field;
  chick.sidestep = chick_sidestep_field;
  {
    const qa_q2m_named_handler extra[] = {
      {"chick_run", {NULL, chick_run_fn}},
      {"monster_duck_down", {NULL, qa_q2mpm_rogue_duck_down_handler}},
      {"monster_duck_hold", {NULL, qa_q2mpm_rogue_duck_hold_handler}},
      {"monster_duck_up", {NULL, qa_q2mpm_rogue_duck_up_handler}},
      {"ChickRocket", {NULL, chick_rocket_fn}},
      {"chick_rerocket", {NULL, chick_rerocket_fn}},
    };
    chick.callbacks = qa_q2mpm_merge_callbacks(arena, chick.callbacks, chick.callback_count, extra, 6, &chick.callback_count);
  }
  out[2] = chick;
  return 3;
}
