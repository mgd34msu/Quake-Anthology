/* Port of src/content/q2/missionpacks/monsters/rogue-soldier.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/rogue_soldier.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/foundation/monsters/soldier.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_soldier.h"

typedef struct soldier_bundle {
  qa_q2m_monsters *monsters;
  qa_q2mpm_monster_state *source;
} soldier_bundle;
static const double blaster_flashes[] = {39, 40, 83, 86, 89, 92, 95, 98};
static const double shotgun_flashes[] = {41, 42, 84, 87, 90, 93, 96, 99};
static const double machinegun_flashes[] = {43, 44, 85, 88, 91, 94, 97, 100};
static void run_move(qa_q2m_context *c) {
  qa_q2m_finish_dodge(c);
  qa_q2m_soldier_run_handler(NULL, c);
}
static void run_fn(void *ctx, qa_q2m_context *c) { (void)ctx; run_move(c); }
static void stand_move(qa_q2m_context *c) {
  if (((uint32_t)c->entity->spawnflags & 8u) != 0) q2mpm_set_move(c, "soldier_move_blind");
  else qa_q2m_soldier_stand_handler(NULL, c);
}
static void stand_fn(void *ctx, qa_q2m_context *c) { (void)ctx; stand_move(c); }
static void fire_at(qa_q2m_context *c, double input) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  qa_body_state enemy_body;
  if (!qa_q2m_enemy_body(c, &enemy_body)) { state->hold_frame = false; return; }
  double index = fabs(input);
  const double *flashes = entity->skin < 2 ? blaster_flashes : entity->skin < 4 ? shotgun_flashes : machinegun_flashes;
  double flash = q2mpm_at_d(flashes, 8, index);
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, flash));
  qa_vec3 aim = qa_math_angle_vectors_rule(q2mpm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).forward;
  if (index != 5 && index != 6) {
    qa_vec3 eye;
    if (!qa_q2m_enemy_eye(c, &eye)) return;
    qa_vec3 direction = qa_sub3_double(eye, start);
    if (input < 0 && qa_dot3_double(qa_normalize3_double_hypot(direction), aim) < 0.9) return;
    qa_angle_vectors axes = qa_math_angle_vectors_rule(
        qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_NATIVE_AI), QA_ANGLE_RULE_NATIVE);
    double spread = game->options.skill < 2 ? 1000 : 500;
    aim = qa_normalize3_double_hypot(qa_add3_double(qa_scale3_double(axes.forward, 8192),
        qa_add3_double(qa_scale3_double(axes.right, (q2mpm_rnd(game) * 2 - 1) * spread),
                       qa_scale3_double(axes.up, (q2mpm_rnd(game) * 2 - 1) * spread * 0.5))));
    qa_trace_result trace = q2mpm_trace(game, start, eye, NULL, &entity->actor.id, 0x6000003);
    if (trace.hit.kind == QA_TRACE_HIT_ACTOR && (!entity->has_enemy || !qa_same_actor(trace.hit.actor, entity->enemy)))
      return;
  }
  if (entity->skin <= 1) {
    (void)c->weapons.fire_blaster(c->weapons.ctx, entity, game, start, aim, 5, 600, 8, false, false, 0);
  } else if (entity->skin <= 3) {
    c->weapons.fire_shotgun(c->weapons.ctx, entity, game, start, aim, 2, 1, 1000, 500, 12, 0);
  } else {
    if (!state->hold_frame)
      entity->wait = q2mpm_now(game) + (3 + (double)((uint32_t)floor(q2mpm_rnd(game) * 0x8000) % 8u)) * 0.1;
    c->weapons.fire_bullet(c->weapons.ctx, entity, game, start, aim, 2, 4, 300, 500, 0);
    state->hold_frame = q2mpm_now(game) < entity->wait;
  }
  qa_q2mpm_monster_flash(c, flash, start, aim);
}
static void duck_move(qa_q2m_context *c, double eta) {
  qa_q2mpm_rogue_duck_down(c);
  bool simple = c->game->options.skill == 0 || q2mpm_rnd(c->game) > c->game->options.skill * 0.3;
  c->state->next_frame = simple ? qa_q2mpm_rogue_soldier_frames_value.duck01
                                : qa_q2mpm_rogue_soldier_frames_value.attak301;
  q2mpm_set_move(c, simple ? "soldier_move_duck" : "soldier_move_attack3");
  c->state->duck_wait = q2mpm_now(c->game) + eta + (c->game->options.skill == 0 || !simple ? 1 : 0.1 * (3 - c->game->options.skill));
}
static void duck_cb_fn(void *ctx, qa_q2m_context *c, double eta) { (void)ctx; duck_move(c, eta); }
static void sidestep_move(qa_q2m_context *c) {
  const char *move = c->entity->skin <= 3 ? "soldier_move_attack6" : "soldier_move_start_run";
  if (strcmp(c->state->move->name, move) != 0) q2mpm_set_move(c, move);
}
static void sidestep_fn(void *ctx, qa_q2m_context *c) { (void)ctx; sidestep_move(c); }
static void refire_move(qa_q2m_context *c, bool first, bool blaster) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  const qa_q2mpm_rogue_soldier_frames *f = &qa_q2mpm_rogue_soldier_frames_value;
  if (first && blaster && state->manual_steering) { state->manual_steering = false; return; }
  if (!entity->has_enemy || (entity->skin <= 1) != blaster || qa_q2m_health(game, &entity->enemy) <= 0) return;
  if ((game->options.skill == 3 && q2mpm_rnd(game) < 0.5) || qa_q2m_target_distance(c) < 80)
    state->next_frame = first ? f->attak102 : f->attak204;
  else if (blaster) state->next_frame = first ? f->attak110 : f->attak216;
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  soldier_bundle *bundle = ctx;
  qa_q2m_finish_dodge(c);
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  qa_q2f_entity *entity = c->entity;
  if (state->attack_state == QA_Q2M_ATTACK_BLIND) {
    double chance = state->blind_fire_delay < 1 ? 1 : state->blind_fire_delay < 7.5 ? 0.4 : 0.1;
    double random = q2mpm_rnd(game);
    state->blind_fire_delay += 4.1 + q2mpm_rnd(game) * 3;
    qa_vec3 target = state->blind_fire_target;
    if ((target.x == 0 && target.y == 0 && target.z == 0) || random > chance) return;
    state->manual_steering = true;
    q2mpm_set_move(c, "soldier_move_attack1");
    state->attack_finished = q2mpm_now(game) + 1.5 + q2mpm_rnd(game);
    return;
  }
  double random = q2mpm_rnd(game);
  if (!qa_q2mpm_state_get(bundle->source, entity)->blocked && !state->stand_ground
      && qa_q2m_target_distance(c) >= 80 && random < game->options.skill * 0.25 && entity->skin <= 3) {
    q2mpm_set_move(c, "soldier_move_attack6");
    return;
  }
  q2mpm_set_move(c, entity->skin < 4 ? (q2mpm_rnd(game) < 0.5 ? "soldier_move_attack1" : "soldier_move_attack2")
                                     : "soldier_move_attack4");
}
static void sight_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_snd(c, q2mpm_rnd(c->game) < 0.5 ? "soldier/solsght1.wav" : "soldier/solsrch1.wav", 2);
  if (c->game->options.skill > 0 && c->entity->has_enemy && qa_q2m_target_distance(c) >= 80
      && q2mpm_rnd(c->game) > 0.75 && c->entity->skin <= 3)
    q2mpm_set_move(c, "soldier_move_attack6");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  if (qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2) entity->skin = q2mpm_or(entity->skin, 1);
  qa_q2m_finish_dodge(c);
  state->charging = false;
  state->manual_steering = false;
  bool airborne = q2mpm_body(game, entity).velocity.z > 100;
  if (q2mpm_now(game) < state->pain_time) {
    const char *name = state->move->name;
    if (airborne && (strcmp(name, "soldier_move_pain1") == 0 || strcmp(name, "soldier_move_pain2") == 0
                     || strcmp(name, "soldier_move_pain3") == 0)) {
      if (state->ducked) qa_q2mpm_rogue_duck_up(c);
      q2mpm_set_move(c, "soldier_move_pain4");
    }
    return;
  }
  state->pain_time = q2mpm_now(game) + 3;
  int32_t skin = (int32_t)entity->skin | 1;
  q2mpm_snd(c, skin == 1 ? "soldier/solpain2.wav" : skin == 3 ? "soldier/solpain1.wav" : "soldier/solpain3.wav", 2);
  if (airborne) {
    if (state->ducked) qa_q2mpm_rogue_duck_up(c);
    q2mpm_set_move(c, "soldier_move_pain4");
    return;
  }
  if (game->options.skill == 3) return;
  double random = q2mpm_rnd(game);
  q2mpm_set_move(c, random < 0.33 ? "soldier_move_pain1" : random < 0.66 ? "soldier_move_pain2" : "soldier_move_pain3");
  if (state->ducked) qa_q2mpm_rogue_duck_up(c);
}
static void dodge_fn(void *ctx, qa_q2m_context *c, qa_actor_id attacker, double eta, const qa_trace_result *trace,
                     bool gravity) {
  soldier_bundle *bundle = ctx;
  (void)gravity;
  qa_q2mpm_duck_fn duck = {NULL, duck_cb_fn};
  qa_q2m_handler sidestep = {NULL, sidestep_fn};
  qa_q2mpm_rogue_monster_dodge(c, bundle->monsters, attacker, eta, trace, duck, sidestep);
}
static bool duck_field_fn(void *ctx, qa_q2m_context *c, double eta) { (void)ctx; duck_move(c, eta); return true; }
static bool blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  soldier_bundle *bundle = ctx;
  return !c->state->dodging && !c->state->ducked
      && (qa_q2mpm_rogue_blocked_check_shot(c, 0.25 + 0.05 * c->game->options.skill, bundle->source)
          || qa_q2mpm_blocked_check_platform(c, distance));
}
static void done_dodge_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_finish_dodge(c); }
static void stop_charge_fn(void *ctx, qa_q2m_context *c) { (void)ctx; c->state->charging = false; }
#define SFIRE(name, v) static void name(void *ctx, qa_q2m_context *c) { (void)ctx; fire_at(c, v); }
SFIRE(fire1_fn, 0) SFIRE(fire2_fn, 1) SFIRE(fire4_fn, 3) SFIRE(fire6_fn, 5) SFIRE(fire7_fn, 6) SFIRE(fire8_fn, -7)
static void fire3_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2mpm_rogue_duck_down(c); fire_at(c, 2); }
static void fire_run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->entity->skin <= 1 && c->entity->has_enemy && qa_q2m_visible_enemy(c)) fire_at(c, 0);
}
static void refire_tt_fn(void *ctx, qa_q2m_context *c) { (void)ctx; refire_move(c, true, true); }
static void refire_tf_fn(void *ctx, qa_q2m_context *c) { (void)ctx; refire_move(c, true, false); }
static void refire_ft_fn(void *ctx, qa_q2m_context *c) { (void)ctx; refire_move(c, false, true); }
static void refire_ff_fn(void *ctx, qa_q2m_context *c) { (void)ctx; refire_move(c, false, false); }
static void refire3_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2mpm_now(c->game) + 0.4 < c->state->duck_wait)
    c->state->next_frame = qa_q2mpm_rogue_soldier_frames_value.attak303;
}
static void refire6_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_finish_dodge(c);
  c->state->charging = false;
  if (c->entity->has_enemy && qa_q2m_health(c->game, &c->entity->enemy) > 0 && qa_q2m_target_distance(c) >= 80
      && (c->game->options.skill == 3 || q2mpm_rnd(c->game) < 0.25 * c->game->options.skill))
    c->state->next_frame = qa_q2mpm_rogue_soldier_frames_value.runs03;
}
static void dead2_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 origin = q2mpm_body(c->game, c->entity).origin;
  qa_vec3 start = origin;
  start.z += 1;
  qa_bounds bounds = {{-32, -32, -24}, {32, 32, -8}};
  qa_trace_result trace = q2mpm_trace(c->game, start, start, &bounds, &c->entity->actor.id, 3);
  if (trace.start_solid || trace.all_solid) qa_q2bm_finish_corpse(c);
  else qa_q2bm_finish_corpse_bounds(c, bounds);
}

size_t qa_q2mpm_create_rogue_soldier_definitions(qa_q2m_definition *out, qa_q2m_monsters *monsters,
                                                 qa_q2mpm_monster_state *source) {
  soldier_bundle *bundle = malloc(sizeof *bundle);
  if (!bundle) qa_q2f_error("Out of memory creating rogue soldier definitions");
  bundle->monsters = monsters;
  bundle->source = source;
  const qa_q2m_named_handler extra[] = {
    {"soldier_run", {NULL, run_fn}},
    {"monster_done_dodge", {NULL, done_dodge_fn}},
    {"monster_duck_down", {NULL, qa_q2mpm_rogue_duck_down_handler}},
    {"monster_duck_hold", {NULL, qa_q2mpm_rogue_duck_hold_handler}},
    {"monster_duck_up", {NULL, qa_q2mpm_rogue_duck_up_handler}},
    {"soldier_stop_charge", {NULL, stop_charge_fn}},
    {"soldier_fire1", {NULL, fire1_fn}},
    {"soldier_fire2", {NULL, fire2_fn}},
    {"soldier_fire3", {NULL, fire3_fn}},
    {"soldier_fire4", {NULL, fire4_fn}},
    {"soldier_fire6", {NULL, fire6_fn}},
    {"soldier_fire7", {NULL, fire7_fn}},
    {"soldier_fire8", {NULL, fire8_fn}},
    {"soldier_fire_run", {NULL, fire_run_fn}},
    {"soldier_attack1_refire1", {NULL, refire_tt_fn}},
    {"soldier_attack1_refire2", {NULL, refire_tf_fn}},
    {"soldier_attack2_refire1", {NULL, refire_ft_fn}},
    {"soldier_attack2_refire2", {NULL, refire_ff_fn}},
    {"soldier_attack3_refire", {NULL, refire3_fn}},
    {"soldier_attack6_refire", {NULL, refire6_fn}},
    {"soldier_dead2", {NULL, dead2_fn}},
  };
  size_t callback_count = 0;
  qa_q2m_named_handler *callbacks = qa_q2mpm_merge_callbacks(qa_q2m_soldier_callbacks, qa_q2m_soldier_callback_count,
                                                             extra, sizeof extra / sizeof extra[0], &callback_count);
  static const char *const classnames[3] = {"monster_soldier_light", "monster_soldier", "monster_soldier_ss"};
  static const double healths[3] = {20, 30, 40};
  for (int i = 0; i < 3; i++) {
    qa_q2m_definition *definition = &out[i];
    memset(definition, 0, sizeof *definition);
    definition->classname = classnames[i];
    definition->kind = "soldier";
    definition->model = "models/monsters/soldier/tris.md2";
    definition->health = healths[i];
    definition->gib_health = -30;
    definition->mass = 100;
    definition->bounds = qa_q2bm_humanoid_bounds;
    definition->scale = 1;
    definition->initial_move = "soldier_move_stand1";
    definition->moves = qa_q2mpm_rogue_soldier_moves;
    definition->move_count = qa_q2mpm_rogue_soldier_move_count;
    definition->callbacks = callbacks;
    definition->callback_count = callback_count;
    definition->stand = (qa_q2m_handler){NULL, stand_fn};
    definition->walk = (qa_q2m_handler){NULL, qa_q2m_soldier_walk_handler};
    definition->run = (qa_q2m_handler){NULL, run_fn};
    definition->attack = (qa_q2m_handler){bundle, attack_fn};
    definition->sight = (qa_q2m_handler){NULL, sight_fn};
    definition->initialize = (qa_q2m_handler){NULL, stand_fn};
    definition->reactions.pain = pain_fn;
    definition->reactions.die = qa_q2m_soldier_die_handler;
    definition->has_blind_fire_flag = true;
    definition->blind_fire = i == 0;
    definition->dodge_ctx = bundle;
    definition->dodge = dodge_fn;
    definition->duck = duck_field_fn;
    definition->blocked_ctx = bundle;
    definition->blocked = blocked_fn;
  }
  return 3;
}
