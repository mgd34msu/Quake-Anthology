/* Port of src/content/q2/missionpacks/monsters/rogue-infantry.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/rogue_infantry.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/infantry.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_infantry.h"

typedef struct infantry_bundle {
  qa_q2m_monsters *monsters;
  qa_q2mpm_monster_state *source;
} infantry_bundle;
static void run_move(qa_q2m_context *c) {
  qa_q2m_finish_dodge(c);
  qa_q2m_infantry_run_handler(NULL, c);
}
static void run_fn(void *ctx, qa_q2m_context *c) { (void)ctx; run_move(c); }
static bool jumping(qa_q2m_context *c) {
  return strcmp(c->state->move->name, "infantry_move_jump") == 0
      || strcmp(c->state->move->name, "infantry_move_jump2") == 0;
}
static bool shooting(qa_q2m_context *c) {
  return strcmp(c->state->move->name, "infantry_move_attack1") == 0
      || strcmp(c->state->move->name, "infantry_move_attack2") == 0;
}
static void duck_move(qa_q2m_context *c, double eta) {
  if (jumping(c)) return;
  if (shooting(c) && c->game->options.skill != 0) { c->state->ducked = false; return; }
  c->state->duck_wait = q2mpm_now(c->game) + eta + (c->game->options.skill == 0 ? 1 : 0.1 * (3 - c->game->options.skill));
  qa_q2mpm_rogue_duck_down(c);
  c->state->next_frame = qa_q2mpm_rogue_infantry_frames_value.duck01;
  q2mpm_set_move(c, "infantry_move_duck");
}
static void duck_cb_fn(void *ctx, qa_q2m_context *c, double eta) { (void)ctx; duck_move(c, eta); }
static void sidestep_move(qa_q2m_context *c) {
  if (jumping(c)) return;
  if (shooting(c) && c->game->options.skill != 0) { c->state->dodging = false; return; }
  if (strcmp(c->state->move->name, "infantry_move_run") != 0) q2mpm_set_move(c, "infantry_move_run");
}
static void sidestep_fn(void *ctx, qa_q2m_context *c) { (void)ctx; sidestep_move(c); }
static void machine_gun(qa_q2m_context *c) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  if (c->entity->frame != qa_q2mpm_rogue_infantry_frames_value.attak104) {
    const qa_q2m_handler *handler = qa_q2m_infantry_callback("InfantryMachineGun");
    if (handler) handler->fn(handler->ctx, c);
    return;
  }
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot(c, 26, -0.2, &aim)) return;
  c->weapons.fire_bullet(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 3, 4, 300, 500, 0);
  qa_q2mpm_monster_flash(c, 26, aim.start, aim.direction);
}
static void machine_gun_fn(void *ctx, qa_q2m_context *c) { (void)ctx; machine_gun(c); }
static void jump_now(qa_q2m_context *c, bool high) {
  qa_body_state body = q2mpm_body(c->game, c->entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  c->entity->timestamp = q2mpm_now(c->game);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_velocity = true;
  change.velocity = qa_add3_double(body.velocity, qa_add3_double(qa_scale3_double(axes.forward, high ? 150 : 100),
                                                                  qa_scale3_double(axes.up, high ? 400 : 300)));
  q2mpm_move_body(c->game, c->entity, change, false, false);
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_finish_dodge(c);
  qa_q2m_infantry_attack_handler(NULL, c);
}
static void idle_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_snd4(c, "infantry/infidle1.wav", 2, 1, 2);
  q2mpm_set_move(c, "infantry_move_fidget");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2bm_damaged_skin(c);
  if (q2mpm_body(c->game, c->entity).ground == NULL) return;
  qa_q2m_finish_dodge(c);
  if (q2mpm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2mpm_now(c->game) + 3;
  if (c->game->options.skill == 3) return;
  bool second = ((uint32_t)floor(q2mpm_rnd(c->game) * 0x8000) & 1u) != 0;
  q2mpm_set_move(c, second ? "infantry_move_pain2" : "infantry_move_pain1");
  q2mpm_snd(c, second ? "infantry/infpain2.wav" : "infantry/infpain1.wav", 2);
  if (c->state->ducked) qa_q2mpm_rogue_duck_up(c);
}
static void dodge_fn(void *ctx, qa_q2m_context *c, qa_actor_id attacker, double eta, const qa_trace_result *trace,
                     bool gravity) {
  infantry_bundle *bundle = ctx;
  (void)gravity;
  qa_q2mpm_duck_fn duck = {NULL, duck_cb_fn};
  qa_q2m_handler sidestep = {NULL, sidestep_fn};
  qa_q2mpm_rogue_monster_dodge(c, bundle->monsters, attacker, eta, trace, duck, sidestep);
}
static bool duck_field_fn(void *ctx, qa_q2m_context *c, double eta) { (void)ctx; duck_move(c, eta); return true; }
static bool blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  infantry_bundle *bundle = ctx;
  if (qa_q2mpm_rogue_blocked_check_shot(c, 0.25 + 0.05 * c->game->options.skill, bundle->source)) return true;
  if (qa_q2mpm_blocked_check_jump_default(c, distance, 192, 40) != QA_Q2MPM_JUMP_NONE) {
    qa_body_state enemy;
    if (qa_q2m_enemy_body(c, &enemy)) {
      qa_q2m_finish_dodge(c);
      q2mpm_set_move(c, enemy.origin.z > q2mpm_body(c->game, c->entity).origin.z ? "infantry_move_jump2"
                                                                                  : "infantry_move_jump");
    }
    return true;
  }
  return qa_q2mpm_blocked_check_platform(c, distance);
}
static void done_dodge_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_finish_dodge(c); }
static void cock_fn(void *ctx, qa_q2m_context *c) { (void)ctx; q2mpm_snd(c, "infantry/infatck3.wav", 1); }
static void fire_prep_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->pause_time = q2mpm_now(c->game) + ((double)((uint32_t)floor(q2mpm_rnd(c->game) * 0x8000) & 15u) + 4) * 0.1;
}
static void fire_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  machine_gun(c);
  c->state->hold_frame = q2mpm_now(c->game) < c->state->pause_time;
}
static void jump1_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_now(c, false); }
static void jump2_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_now(c, true); }
static void jump_wait_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->next_frame = c->entity->frame
      + (q2mpm_body(c->game, c->entity).ground != NULL || qa_q2mpm_monster_jump_finished(c) ? 1 : 0);
}
qa_q2m_definition qa_q2mpm_create_rogue_infantry_definition(qa_q2m_monsters *monsters,
                                                            qa_q2mpm_monster_state *source) {
  infantry_bundle *bundle = malloc(sizeof *bundle);
  if (!bundle) qa_q2f_error("Out of memory creating rogue infantry definition");
  bundle->monsters = monsters;
  bundle->source = source;
  const qa_q2m_named_handler extra[] = {
    {"infantry_run", {NULL, run_fn}},
    {"InfantryMachineGun", {NULL, machine_gun_fn}},
    {"monster_done_dodge", {NULL, done_dodge_fn}},
    {"monster_duck_down", {NULL, qa_q2mpm_rogue_duck_down_handler}},
    {"monster_duck_hold", {NULL, qa_q2mpm_rogue_duck_hold_handler}},
    {"monster_duck_up", {NULL, qa_q2mpm_rogue_duck_up_handler}},
    {"infantry_cock_gun", {NULL, cock_fn}},
    {"infantry_fire_prep", {NULL, fire_prep_fn}},
    {"infantry_fire", {NULL, fire_fn}},
    {"infantry_jump_now", {NULL, jump1_fn}},
    {"infantry_jump2_now", {NULL, jump2_fn}},
    {"infantry_jump_wait_land", {NULL, jump_wait_fn}},
  };
  qa_q2m_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.classname = "monster_infantry";
  definition.kind = "infantry";
  definition.model = "models/monsters/infantry/tris.md2";
  definition.health = 100;
  definition.gib_health = -40;
  definition.mass = 200;
  definition.bounds = qa_q2bm_humanoid_bounds;
  definition.scale = 1;
  definition.initial_move = "infantry_move_stand";
  definition.moves = qa_q2mpm_rogue_infantry_moves;
  definition.move_count = qa_q2mpm_rogue_infantry_move_count;
  definition.callbacks = qa_q2mpm_merge_callbacks(qa_q2m_infantry_callbacks, qa_q2m_infantry_callback_count,
                                                  extra, sizeof extra / sizeof extra[0], &definition.callback_count);
  definition.stand = (qa_q2m_handler){NULL, qa_q2m_infantry_stand_handler};
  definition.walk = (qa_q2m_handler){NULL, qa_q2m_infantry_walk_handler};
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.attack = (qa_q2m_handler){NULL, attack_fn};
  definition.sight = (qa_q2m_handler){NULL, qa_q2m_infantry_sight_handler};
  definition.idle = (qa_q2m_handler){NULL, idle_fn};
  definition.reactions.pain = pain_fn;
  definition.reactions.die = qa_q2m_infantry_die_handler;
  definition.dodge_ctx = bundle;
  definition.dodge = dodge_fn;
  definition.duck = duck_field_fn;
  definition.blocked_ctx = bundle;
  definition.blocked = blocked_fn;
  return definition;
}
