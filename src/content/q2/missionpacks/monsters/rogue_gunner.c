/* Port of src/content/q2/missionpacks/monsters/rogue-gunner.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/rogue_gunner.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/gunner.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_gunner.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/foundation/js.h"

typedef struct gunner_bundle {
  qa_q2m_monsters *monsters;
  qa_q2mpm_monster_state *source;
} gunner_bundle;
static bool grenade_check(qa_q2m_context *c) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return false;
  qa_body_state body = q2mpm_body(c->game, c->entity);
  qa_q2m_state *state = c->state;
  bool below = state->manual_steering ? body.origin.z + c->entity->view_height < state->blind_fire_target.z
                                      : body.origin.z + body.bounds.max.z <= enemy.origin.z + enemy.bounds.min.z;
  if (below) return false;
  qa_vec3 target = state->manual_steering ? state->blind_fire_target : enemy.origin;
  if (qa_hypot3(qa_sub3_double(body.origin, target)) < 100) return false;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(c->game->options.edition, 53));
  qa_trace_result trace = q2mpm_trace(c->game, start, target, NULL, &c->entity->actor.id, 0x6000003);
  return trace.fraction == 1
      || (trace.hit.kind == QA_TRACE_HIT_ACTOR && c->entity->has_enemy && qa_same_actor(trace.hit.actor, c->entity->enemy));
}
static void grenade(qa_q2m_context *c) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  const qa_q2mpm_rogue_gunner_frames *f = &qa_q2mpm_rogue_gunner_frames_value;
  bool blind = state->manual_steering;
  qa_body_state body = q2mpm_body(game, entity);
  double index = entity->frame == f->attak105 ? 0 : entity->frame == f->attak108 ? 1 : entity->frame == f->attak111 ? 2 : 3;
  if (index == 3) state->manual_steering = false;
  bool blind_target = blind && !qa_q2m_visible_enemy(c);
  qa_vec3 point = blind_target ? state->blind_fire_target : body.origin;
  if (blind_target && point.x == 0 && point.y == 0 && point.z == 0) return;
  double flash = 53 + index;
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, flash));
  qa_vec3 aim = qa_sub3_double(point, body.origin);
  double distance = qa_hypot3(aim);
  if (distance > 512 && aim.z < 64 && aim.z > -64) aim.z += distance - 512;
  double pitch = qa_normalize3_double_hypot(aim).z;
  pitch = qa_js_max(-0.5, qa_js_min(0.4, pitch));
  qa_vec3 direction = qa_add3_double(qa_add3_double(axes.forward, qa_scale3_double(axes.right, 0.02 + index * 0.03)),
                                     qa_scale3_double(axes.up, pitch));
  (void)c->weapons.fire_grenade(c->weapons.ctx, entity, game, start, direction, 50, 600, 2.5, 90, false, false, true, NULL);
  qa_q2rm_monster_flash(c, flash, start, direction);
}
static void duck_down_grenade(qa_q2m_context *c) {
  c->state->ducked = true;
  if (c->game->options.skill >= 2 && q2mpm_rnd(c->game) > 0.5) grenade(c);
  qa_q2mpm_rogue_duck_down(c);
}
static bool jumping(qa_q2m_context *c) {
  return strcmp(c->state->move->name, "gunner_move_jump") == 0
      || strcmp(c->state->move->name, "gunner_move_jump2") == 0;
}
static bool shooting(qa_q2m_context *c) {
  const char *name = c->state->move->name;
  return strcmp(name, "gunner_move_attack_chain") == 0 || strcmp(name, "gunner_move_fire_chain") == 0
      || strcmp(name, "gunner_move_attack_grenade") == 0;
}
static void duck_move(qa_q2m_context *c, double eta) {
  if (jumping(c)) return;
  if (shooting(c) && c->game->options.skill != 0) { c->state->ducked = false; return; }
  c->state->duck_wait = q2mpm_now(c->game) + eta + (c->game->options.skill == 0 ? 1 : 0.1 * (3 - c->game->options.skill));
  duck_down_grenade(c);
  c->state->next_frame = qa_q2mpm_rogue_gunner_frames_value.duck01;
  q2mpm_set_move(c, "gunner_move_duck");
}
static void duck_cb_fn(void *ctx, qa_q2m_context *c, double eta) { (void)ctx; duck_move(c, eta); }
static void sidestep_move(qa_q2m_context *c) {
  if (jumping(c)) return;
  if (shooting(c) && c->game->options.skill != 0) { c->state->dodging = false; return; }
  if (strcmp(c->state->move->name, "gunner_move_run") != 0) q2mpm_set_move(c, "gunner_move_run");
}
static void sidestep_fn(void *ctx, qa_q2m_context *c) { (void)ctx; sidestep_move(c); }
static void run_move(qa_q2m_context *c) {
  qa_q2m_finish_dodge(c);
  if (qa_q2bm_gunner_definition.run.fn) qa_q2bm_gunner_definition.run.fn(qa_q2bm_gunner_definition.run.ctx, c);
}
static void run_fn(void *ctx, qa_q2m_context *c) { (void)ctx; run_move(c); }
static void jump_now(qa_q2m_context *c, bool up) {
  qa_body_state body = q2mpm_body(c->game, c->entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  c->entity->timestamp = q2mpm_now(c->game);
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_velocity = true;
  change.velocity = qa_add3_double(body.velocity, qa_add3_double(qa_scale3_double(axes.forward, up ? 150 : 100),
                                                                  qa_scale3_double(axes.up, up ? 400 : 300)));
  q2mpm_move_body(c->game, c->entity, change, false, false);
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  gunner_bundle *bundle = ctx;
  qa_q2m_finish_dodge(c);
  qa_q2m_state *state = c->state;
  qa_q2_game_services *game = c->game;
  if (state->attack_state == QA_Q2M_ATTACK_BLIND) {
    double chance = state->blind_fire_delay < 1 ? 1 : state->blind_fire_delay < 7.5 ? 0.4 : 0.1;
    double random = q2mpm_rnd(game);
    state->blind_fire_delay += 4.1 + q2mpm_rnd(game) * 3;
    qa_vec3 target = state->blind_fire_target;
    if ((target.x == 0 && target.y == 0 && target.z == 0) || random > chance) return;
    state->manual_steering = true;
    if (grenade_check(c)) {
      q2mpm_set_move(c, "gunner_move_attack_grenade");
      double left = q2mpm_now(game);
      double right = 2 * q2mpm_rnd(game);
      state->attack_finished = left + right;
    }
    state->manual_steering = false;
    return;
  }
  bool bad_area = qa_q2mpm_state_get(bundle->source, c->entity)->has_bad_area;
  q2mpm_set_move(c, qa_q2m_target_distance(c) < 80 || bad_area ? "gunner_move_attack_chain"
                    : q2mpm_rnd(game) <= 0.5 && grenade_check(c) ? "gunner_move_attack_grenade"
                                                                 : "gunner_move_attack_chain");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2bm_damaged_skin(c);
  qa_q2m_finish_dodge(c);
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  if (q2mpm_body(game, entity).ground == NULL || q2mpm_now(game) < state->pain_time) return;
  state->pain_time = q2mpm_now(game) + 3;
  q2mpm_snd(c, ((uint32_t)floor(q2mpm_rnd(game) * 0x8000) & 1u) != 0 ? "gunner/gunpain2.wav" : "gunner/gunpain1.wav", 2);
  if (game->options.skill == 3) return;
  q2mpm_set_move(c, reaction.damage <= 10 ? "gunner_move_pain3" : reaction.damage <= 25 ? "gunner_move_pain2"
                                                                                         : "gunner_move_pain1");
  state->manual_steering = false;
  if (state->ducked) qa_q2mpm_rogue_duck_up(c);
}
static void dodge_fn(void *ctx, qa_q2m_context *c, qa_actor_id attacker, double eta, const qa_trace_result *trace,
                     bool gravity) {
  gunner_bundle *bundle = ctx;
  (void)gravity;
  qa_q2mpm_duck_fn duck = {NULL, duck_cb_fn};
  qa_q2m_handler sidestep = {NULL, sidestep_fn};
  qa_q2mpm_rogue_monster_dodge(c, bundle->monsters, attacker, eta, trace, duck, sidestep);
}
static bool duck_field_fn(void *ctx, qa_q2m_context *c, double eta) { (void)ctx; duck_move(c, eta); return true; }
static bool blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  gunner_bundle *bundle = ctx;
  if (qa_q2mpm_rogue_blocked_check_shot(c, 0.25 + 0.05 * c->game->options.skill, bundle->source)
      || qa_q2rm_blocked_check_platform(c, distance)) return true;
  if (qa_q2rm_blocked_check_jump_default(c, distance, 192, 40) == QA_Q2RM_JUMP_NONE) return false;
  qa_body_state enemy;
  if (qa_q2m_enemy_body(c, &enemy)) {
    qa_q2m_finish_dodge(c);
    q2mpm_set_move(c, enemy.origin.z > q2mpm_body(c->game, c->entity).origin.z ? "gunner_move_jump2" : "gunner_move_jump");
  }
  return true;
}
static void done_dodge_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_finish_dodge(c); }
static void duck_down_fn(void *ctx, qa_q2m_context *c) { (void)ctx; duck_down_grenade(c); }
static void grenade_fn(void *ctx, qa_q2m_context *c) { (void)ctx; grenade(c); }
static void fire_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  double flash = 45 + c->entity->frame - qa_q2mpm_rogue_gunner_frames_value.attak216;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot(c, flash, -0.2, &aim)) return;
  c->weapons.fire_bullet(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 3, 4, 300, 500, 0);
  qa_q2rm_monster_flash(c, flash, aim.start, aim.direction);
}
static void blind_check_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->manual_steering)
    c->state->ideal_yaw = qa_vector_to_angles_with_rule(
        qa_sub3_double(c->state->blind_fire_target, q2mpm_body(c->game, c->entity).origin),
        QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
}
static void jump1_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_now(c, false); }
static void jump2_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_now(c, true); }
static void jump_wait_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->next_frame = c->entity->frame
      + (q2mpm_body(c->game, c->entity).ground != NULL || qa_q2rm_monster_jump_finished(c) ? 1 : 0);
}
qa_q2m_definition qa_q2mpm_create_rogue_gunner_definition(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters,
                                                          qa_q2mpm_monster_state *source) {
  gunner_bundle *bundle = qa_q2mpm_arena_alloc(arena, sizeof *bundle);
  if (!bundle) qa_raise("Error", 0, 0, "Out of memory creating rogue gunner definition");
  bundle->monsters = monsters;
  bundle->source = source;
  qa_q2m_definition definition = qa_q2bm_gunner_definition;
  definition.moves = qa_q2mpm_rogue_gunner_moves;
  definition.move_count = qa_q2mpm_rogue_gunner_move_count;
  definition.has_blind_fire_flag = true;
  definition.blind_fire = true;
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.attack = (qa_q2m_handler){bundle, attack_fn};
  definition.reactions.ctx = NULL;
  definition.reactions.pain = pain_fn;
  definition.dodge_ctx = bundle;
  definition.dodge = dodge_fn;
  definition.duck = duck_field_fn;
  definition.blocked_ctx = bundle;
  definition.blocked = blocked_fn;
  const qa_q2m_named_handler extra[] = {
    {"gunner_run", {NULL, run_fn}},
    {"monster_done_dodge", {NULL, done_dodge_fn}},
    {"gunner_duck_down", {NULL, duck_down_fn}},
    {"monster_duck_hold", {NULL, qa_q2mpm_rogue_duck_hold_handler}},
    {"monster_duck_up", {NULL, qa_q2mpm_rogue_duck_up_handler}},
    {"GunnerGrenade", {NULL, grenade_fn}},
    {"GunnerFire", {NULL, fire_fn}},
    {"gunner_blind_check", {NULL, blind_check_fn}},
    {"gunner_jump_now", {NULL, jump1_fn}},
    {"gunner_jump2_now", {NULL, jump2_fn}},
    {"gunner_jump_wait_land", {NULL, jump_wait_fn}},
  };
  definition.callbacks = qa_q2mpm_merge_callbacks(arena, definition.callbacks, definition.callback_count,
                                                  extra, sizeof extra / sizeof extra[0], &definition.callback_count);
  return definition;
}
