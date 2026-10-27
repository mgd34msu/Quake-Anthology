/* Port of src/content/q2/rerelease/monsters/gunner.ts.
 * Rerelease m_gunner.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/gunner.h"

#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/gunner.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/flashes.h"
#include "qa/content/q2/rerelease/monsters/tables/gunner.h"
#include "rrm_internal.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_finish_dodge(c);
  qa_q2bm_gunner_definition.run.fn(qa_q2bm_gunner_definition.run.ctx, c);
}
static bool jumping(qa_q2m_context *c) {
  const char *name = c->state->move->name;
  return strcmp(name, "gunner_move_jump") == 0 || strcmp(name, "gunner_move_jump2") == 0;
}
static bool shooting(qa_q2m_context *c) {
  const char *name = c->state->move->name;
  return strcmp(name, "gunner_move_attack_chain") == 0 || strcmp(name, "gunner_move_fire_chain") == 0
      || strcmp(name, "gunner_move_attack_grenade") == 0 || strcmp(name, "gunner_move_attack_grenade2") == 0;
}
static bool grenade_check(qa_q2m_context *c) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy) || !qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, 53)))
    return false;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, 53));
  qa_vec3 target = c->state->manual_steering ? c->state->blind_fire_target : enemy.origin;
  qa_vec3 delta = qa_sub3_double(target, start);
  qa_vec3 out;
  return qa_hypot3(delta) >= 100
      && qa_q2rm_calculate_pitch_to_fire_default(c, target, start, qa_normalize3_double_hypot(delta), 600, 2.5, false, &out);
}
static void grenade(qa_q2m_context *c) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  bool blind = state->manual_steering;
  double current = entity->frame;
  const qa_q2rm_gunner_frames *frame = &qa_q2rm_gunner_frames_value;
  double spread, id;
  if (current == frame->attak105 || current == frame->attak309) { spread = -0.1; id = 53; }
  else if (current == frame->attak108 || current == frame->attak312) { spread = -0.05; id = 54; }
  else if (current == frame->attak111 || current == frame->attak315) { spread = 0.05; id = 55; }
  else { state->manual_steering = false; spread = 0.1; id = 56; }
  if (current >= frame->attak301 && current <= frame->attak324)
    id = qa_q2rm_rereleaseFlash_value.GUNNER_GRENADE2_1 + 56 - id;
  qa_vec3 target = blind && !qa_q2m_visible_enemy(c) ? state->blind_fire_target : enemy.origin;
  if (blind && !qa_q2m_visible_enemy(c) && qa_hypot3(target) == 0) return;
  qa_body_state body = q2rm_body(game, entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, id));
  qa_vec3 delta = qa_sub3_double(target, body.origin);
  double distance = qa_hypot3(delta);
  if (distance > 512 && delta.z < 64 && delta.z > -64) delta.z += distance - 512;
  double pitch = fmax(-0.5, fmin(0.4, qa_normalize3_double_hypot(delta).z));
  qa_vec3 aim = qa_add3_double(qa_add3_double(axes.forward, qa_scale3_double(axes.right, spread)),
                               qa_scale3_double(axes.up, pitch));
  qa_vec3 predicted;
  bool have_predicted = qa_q2rm_calculate_pitch_to_fire_default(c, target, start, aim, 600, 2.5, false, &predicted);
  double right = (q2rm_rnd(game) * 2 - 1) * 10;
  double up = !have_predicted ? 200 + (q2rm_rnd(game) * 2 - 1) * 10 : q2rm_rnd(game) * 10;
  qa_vec3 direction = have_predicted ? predicted : aim;
  qa_q2w_grenade_adjustment adjustment = {right, up, qa_q2rm_host_gravity(game)};
  (void)c->weapons.fire_grenade(c->weapons.ctx, entity, game, start, direction, 50, 600, 2.5, 90,
                                false, false, true, &adjustment);
  qa_q2rm_monster_flash(c, id, start, direction);
}
static void grenade_fn(void *ctx, qa_q2m_context *c) { (void)ctx; grenade(c); }
static void jump_impl(qa_q2m_context *c, bool up) {
  qa_body_state body = q2rm_body(c->game, c->entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  qa_vec3 velocity = qa_add3_double(body.velocity,
      qa_add3_double(qa_scale3_double(axes.forward, up ? 150 : 100), qa_scale3_double(axes.up, up ? 400 : 300)));
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_velocity = true; change.velocity = velocity;
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void sight_fn(void *ctx, qa_q2m_context *c) { (void)ctx; q2rm_snd(c, "gunner/sight1.wav", 2); }
static void search_fn(void *ctx, qa_q2m_context *c) { (void)ctx; q2rm_snd(c, "gunner/gunsrch1.wav", 2); }
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_q2m_finish_dodge(c);
  if (state->attack_state == QA_Q2M_ATTACK_BLIND) {
    if (entity->timestamp > q2rm_now(game)) return;
    double chance = state->blind_fire_delay < 1 ? 1 : state->blind_fire_delay < 7.5 ? 0.4 : 0.1;
    double choice = q2rm_rnd(game);
    state->blind_fire_delay += 4.1 + q2rm_rnd(game) * 3;
    if (qa_hypot3(state->blind_fire_target) == 0 || choice > chance) return;
    state->manual_steering = true;
    if (grenade_check(c)) {
      q2rm_set_move(c, q2rm_rnd(game) < 0.5 ? "gunner_move_attack_grenade2" : "gunner_move_attack_grenade");
      state->attack_finished = q2rm_now(game) + q2rm_rnd(game) * 2;
    } else state->manual_steering = false;
    entity->timestamp = q2rm_now(game) + 2 + q2rm_rnd(game);
    return;
  }
  if (entity->timestamp > q2rm_now(game)
      || (qa_q2m_target_distance(c) <= 175 && qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, 45)))) {
    q2rm_set_move(c, "gunner_move_attack_chain");
    return;
  }
  if (entity->timestamp <= q2rm_now(game) && q2rm_rnd(game) <= 0.5 && grenade_check(c)) {
    q2rm_set_move(c, q2rm_rnd(game) < 0.5 ? "gunner_move_attack_grenade2" : "gunner_move_attack_grenade");
    entity->timestamp = q2rm_now(game) + 2 + q2rm_rnd(game);
  } else if (qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, 45)))
    q2rm_set_move(c, "gunner_move_attack_chain");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  qa_q2m_finish_dodge(c);
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2
      ? q2rm_or(entity->skin, 1) : q2rm_and(entity->skin, ~1);
  if (jumping(c) || q2rm_now(game) < state->pain_time) return;
  state->pain_time = q2rm_now(game) + 3;
  q2rm_snd(c, q2rm_rnd(game) < 0.5 ? "gunner/gunpain2.wav" : "gunner/gunpain1.wav", 2);
  if (!qa_q2rm_reacts_to_pain(c)) return;
  q2rm_set_move(c, reaction.damage <= 10 ? "gunner_move_pain3" : reaction.damage <= 25 ? "gunner_move_pain2" : "gunner_move_pain1");
  state->manual_steering = false;
  if (state->ducked) qa_q2m_set_duck(c, false);
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  double damage = reaction.pain.damage;
  if (qa_q2rm_check_gib(c)) {
    q2rm_snd(c, "misc/udeath.wav", 2);
    entity->skin = trunc(entity->skin / 2);
    qa_q2m_gib_options plain = {false, false, false, false};
    for (int i = 0; i < 2; i++) {
      (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", damage, plain);
      (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
    }
    static const char *const parts[5] = {"chest", "garm", "gun", "foot", "head"};
    static const char *const models[5] = {
      "models/monsters/gunner/gibs/chest.md2", "models/monsters/gunner/gibs/garm.md2",
      "models/monsters/gunner/gibs/gun.md2", "models/monsters/gunner/gibs/foot.md2",
      "models/monsters/gunner/gibs/head.md2"};
    for (int i = 0; i < 5; i++) {
      qa_q2m_gib_options options = {strcmp(parts[i], "head") == 0, false, true,
                                    strcmp(parts[i], "garm") == 0 || strcmp(parts[i], "gun") == 0};
      (void)qa_q2m_throw_gib(entity, game, models[i], damage, options);
    }
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  q2rm_snd(c, "gunner/death1.wav", 2);
  state->dead = true; state->can_take_damage = true;
  q2rm_set_damageable(game, entity, true);
  q2rm_set_move(c, "gunner_move_death");
}
static bool duck_fn(void *ctx, qa_q2m_context *c, double eta) {
  (void)ctx; (void)eta;
  if (jumping(c)) return false;
  if (shooting(c)) { qa_q2m_set_duck(c, false); return false; }
  if (q2rm_rnd(c->game) > 0.5) grenade(c);
  q2rm_set_move(c, "gunner_move_duck");
  return true;
}
static bool sidestep_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (jumping(c) || shooting(c) || strcmp(c->state->move->name, "gunner_move_pain1") == 0) return false;
  if (strcmp(c->state->move->name, "gunner_move_run") != 0) q2rm_set_move(c, "gunner_move_run");
  return true;
}
static bool blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  (void)ctx;
  if (qa_q2rm_blocked_check_platform(c, distance)) return true;
  qa_q2rm_jump_navigation navigation;
  memset(&navigation, 0, sizeof navigation);
  qa_q2rm_jump_result result = qa_q2rm_blocked_check_jump(c, distance, 192, 40,
                                                          !q2rm_has(c->entity->spawnflags, 8), navigation);
  if (result == QA_Q2RM_JUMP_NONE) return false;
  qa_body_state enemy;
  if (result != QA_Q2RM_JUMP_TURN && qa_q2m_enemy_body(c, &enemy)) {
    qa_q2m_finish_dodge(c);
    q2rm_set_move(c, result == QA_Q2RM_JUMP_UP ? "gunner_move_jump2" : "gunner_move_jump");
  }
  return true;
}
static void corpse_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_corpse(c); }
static void fidget_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!c->state->stand_ground && !c->entity->has_enemy && q2rm_rnd(c->game) <= 0.05)
    q2rm_set_move(c, "gunner_move_fidget");
}
static void shrink_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->server_flags = q2rm_or(c->entity->server_flags, 2);
  qa_bounds bounds = q2rm_body(c->game, c->entity).bounds;
  bounds.max.z = -4;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true; change.bounds = bounds;
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void runandshoot_fn(void *ctx, qa_q2m_context *c) { (void)ctx; q2rm_set_move(c, "gunner_move_runandshoot"); }
static void blind_check_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->manual_steering)
    c->state->ideal_yaw = qa_vector_to_angles_with_rule(
        qa_sub3_double(c->state->blind_fire_target, q2rm_body(c->game, c->entity).origin),
        QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
}
static void fire_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  double id = 45 + c->entity->frame - qa_q2rm_gunner_frames_value.attak216;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, id));
  qa_vec3 direction;
  if (!qa_q2rm_predicted_direction(c, start, 0, true, -0.2, &direction)) return;
  c->weapons.fire_bullet(c->weapons.ctx, c->entity, c->game, start, direction, 3, 4, 300, 500, 0);
  qa_q2rm_monster_flash(c, id, start, direction);
}
static void refire_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  bool again = qa_q2m_health(c->game, c->entity->has_enemy ? &c->entity->enemy : NULL) > 0
      && qa_q2m_visible_enemy(c) && q2rm_rnd(c->game) <= 0.5;
  c->set_move(c->impl_ctx, c, again ? "gunner_move_fire_chain" : "gunner_move_endfire_chain", true, false);
}
static void jump_now_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_impl(c, false); }
static void jump2_now_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_impl(c, true); }
static void wait_land_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->next_frame = c->entity->frame
      + (q2rm_body(c->game, c->entity).ground || qa_q2rm_monster_jump_finished(c) ? 1 : 0);
}
static const qa_q2m_named_handler extra_callbacks[] = {
  {"gunner_run", {NULL, run_fn}},
  {"gunner_dead", {NULL, corpse_fn}},
  {"GunnerGrenade", {NULL, grenade_fn}},
  {"gunner_fidget", {NULL, fidget_fn}},
  {"gunner_shrink", {NULL, shrink_fn}},
  {"gunner_runandshoot", {NULL, runandshoot_fn}},
  {"gunner_blind_check", {NULL, blind_check_fn}},
  {"GunnerFire", {NULL, fire_fn}},
  {"gunner_refire_chain", {NULL, refire_fn}},
  {"gunner_jump_now", {NULL, jump_now_fn}},
  {"gunner_jump2_now", {NULL, jump2_now_fn}},
  {"gunner_jump_wait_land", {NULL, wait_land_fn}},
};
static qa_q2m_definition gunner_definition;
static bool gunner_ready;
const qa_q2m_definition *qa_q2rm_get_rerelease_gunner_definition(void) {
  if (gunner_ready) return &gunner_definition;
  /* TS builds this definition literally (only bounds/stand/walk and
   * the callback table come from the base definition). */
  memset(&gunner_definition, 0, sizeof gunner_definition);
  gunner_definition.classname = "monster_gunner";
  gunner_definition.kind = "gunner";
  gunner_definition.model = "models/monsters/gunner/tris.md2";
  gunner_definition.health = 175;
  gunner_definition.gib_health = -70;
  gunner_definition.mass = 200;
  gunner_definition.bounds = qa_q2bm_gunner_definition.bounds;
  gunner_definition.scale = (float)1.15;
  gunner_definition.initial_move = "gunner_move_stand";
  gunner_definition.stand = qa_q2bm_gunner_definition.stand;
  gunner_definition.walk = qa_q2bm_gunner_definition.walk;
  gunner_definition.moves = qa_q2rm_gunner_moves;
  gunner_definition.move_count = qa_q2rm_gunner_move_count;
  gunner_definition.run = (qa_q2m_handler){NULL, run_fn};
  gunner_definition.attack = (qa_q2m_handler){NULL, attack_fn};
  gunner_definition.sight = (qa_q2m_handler){NULL, sight_fn};
  gunner_definition.search = (qa_q2m_handler){NULL, search_fn};
  gunner_definition.has_blind_fire_flag = true;
  gunner_definition.blind_fire = true;
  gunner_definition.reactions.pain = pain_fn;
  gunner_definition.reactions.die = die_fn;
  gunner_definition.duck_ctx = NULL; gunner_definition.duck = duck_fn;
  gunner_definition.sidestep_ctx = NULL; gunner_definition.sidestep = sidestep_fn;
  gunner_definition.blocked_ctx = NULL; gunner_definition.blocked = blocked_fn;
  gunner_definition.callbacks = qa_q2rm_merge_callbacks(
      qa_q2bm_gunner_definition.callbacks, qa_q2bm_gunner_definition.callback_count,
      extra_callbacks, sizeof extra_callbacks / sizeof extra_callbacks[0], &gunner_definition.callback_count);
  gunner_ready = true;
  return &gunner_definition;
}
