/* Port of src/content/q2/foundation/monsters/infantry.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/monsters/infantry.h"

#include <math.h>

#include "monsters_internal.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/frames.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"

static void set_move(qa_q2m_context *c, const char *name) { c->set_move(c->impl_ctx, c, name, false, false); }
#define IFIELD(c, field) (q2m_rr((c)->game) ? qa_q2m_rerelease_infantry_frames_value.field \
                                            : qa_q2m_classic_infantry_frames_value.field)
static bool can_run(qa_q2m_context *c) {
  return qa_q2m_walk_move(c, q2m_body(c->game, c->entity).angles.y, 8, false, true);
}
static bool jumping(qa_q2m_context *c) {
  return strcmp(c->state->move->name, "infantry_move_jump") == 0
      || strcmp(c->state->move->name, "infantry_move_jump2") == 0;
}
void qa_q2m_infantry_stand_handler(void *ctx, qa_q2m_context *c) { (void)ctx; set_move(c, "infantry_move_stand"); }
void qa_q2m_infantry_walk_handler(void *ctx, qa_q2m_context *c) { (void)ctx; set_move(c, "infantry_move_walk"); }
void qa_q2m_infantry_run_handler(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2m_rr(c->game)) qa_q2m_finish_dodge(c);
  set_move(c, c->state->stand_ground ? "infantry_move_stand" : "infantry_move_run");
}
void qa_q2m_infantry_sight_handler(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  bool rr = q2m_rr(c->game);
  q2m_sound(c->game, c->entity, !rr || q2m_rnd(c->game) < 0.5 ? "infantry/infsght1.wav" : "infantry/infsrch1.wav",
            true, rr ? 2 : 4, false, 0, false, 0);
}
void qa_q2m_infantry_pain_handler(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_q2f_game *game = c->game;
  bool rr = q2m_rr(game);
  if (qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2) entity->skin = 1;
  else if (rr) entity->skin = 0;
  if (rr && jumping(c)) return;
  if (rr) qa_q2m_finish_dodge(c);
  if (q2m_now(game) < state->pain_time) {
    if (rr && q2m_rnd(game) < 0.33 && reaction.attacker)
      c->dodge(c->impl_ctx, c, *reaction.attacker, game->host->frame_seconds(game->host->ctx), NULL, false, false);
    return;
  }
  state->pain_time = q2m_now(game) + 3;
  if (!rr && game->options.skill == 3) return;
  double n = floor(q2m_rnd(game) * 2);
  q2m_sound(game, entity, n == 0 ? "infantry/infpain1.wav" : "infantry/infpain2.wav", true, 2, false, 0, false, 0);
  const qa_attack_cause *cause = entity->last_attack ? &entity->last_attack->cause : NULL;
  if (rr && (state->ducked || state->combat_point
      || (game->options.skill == 3 && !(cause && cause->kind == QA_ATTACK_CAUSE_Q2 && cause->means_of_death == 41)))) {
    if (q2m_rnd(game) < 0.33 && reaction.attacker)
      c->dodge(c->impl_ctx, c, *reaction.attacker, game->host->frame_seconds(game->host->ctx), NULL, false, false);
    return;
  }
  set_move(c, n == 0 ? "infantry_move_pain1" : "infantry_move_pain2");
  if (rr) qa_q2m_set_duck(c, false);
}

static const qa_vec3 death_aim[12] = {
  {0, 5, 0}, {10, 15, 0}, {20, 25, 0}, {25, 35, 0}, {30, 40, 0}, {30, 45, 0},
  {25, 50, 0}, {20, 40, 0}, {15, 35, 0}, {40, 35, 0}, {70, 35, 0}, {90, 35, 0},
};
static void machine_gun(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  bool rr = q2m_rr(game);
  qa_body_state enemy;
  bool has_enemy = qa_q2m_enemy_body(c, &enemy);
  if (rr && !has_enemy) return;
  const qa_q2m_rerelease_infantry_frames *rf = &qa_q2m_rerelease_infantry_frames_value;
  bool running = rr && entity->frame >= rf->run201 && entity->frame <= rf->run208;
  bool normal = rr ? (entity->frame == rf->attak103 || entity->frame == rf->attak311
                      || entity->frame == rf->attak416 || running)
                   : entity->frame == qa_q2m_classic_infantry_frames_value.attak111;
  double flash = normal ? (running ? entity->frame
                          : (rr && entity->frame == rf->attak416 ? qa_q2m_muzzle_ids_value.INFANTRY_MACHINEGUN_22 : 26))
                        : 27 + entity->frame - IFIELD(c, death211);
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, flash));
  qa_vec3 forward = qa_math_angle_vectors_rule(q2m_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).forward;
  if (normal && has_enemy) {
    qa_q2f_target_obs_result observed = game->monster_target(game->ctx, &entity->enemy);
    if (!observed.has_value) return;
    double height = observed.value.view_height;
    if (!rr) {
      forward = qa_normalize3_double_hypot(qa_sub3_double(
          qa_add3_double(qa_add3_double(enemy.origin, qa_scale3_double(enemy.velocity, -0.2)), q2m_v3(0, 0, height)), start));
    } else {
      qa_vec3 eye = qa_add3_double(enemy.origin, q2m_v3(0, 0, height));
      qa_trace_result trace = q2m_trace(game, start, eye, NULL, &entity->actor.id, 0x46004003);
      bool use_eye = trace.hit.kind == QA_TRACE_HIT_ACTOR && entity->has_enemy && qa_same_actor(trace.hit.actor, entity->enemy);
      qa_vec3 target = use_eye ? eye : enemy.origin;
      qa_vec3 predicted = qa_add3_double(enemy.origin, qa_scale3_double(enemy.velocity, 0.2));
      if (qa_dot3_double(qa_normalize3_double_hypot(qa_sub3_double(target, start)),
                         qa_normalize3_double_hypot(qa_sub3_double(predicted, start))) < 0
          || q2m_trace(game, start, predicted, NULL, NULL, 3).fraction < 0.9)
        predicted = enemy.origin;
      if (use_eye) predicted.z += height;
      forward = qa_normalize3_double_hypot(qa_sub3_double(predicted, start));
    }
  } else if (!normal) {
    qa_vec3 aim_offset = death_aim[qa_q2m_record_at(12, flash - 27)];
    forward = qa_math_angle_vectors_rule(qa_sub3_double(q2m_body(game, entity).angles, aim_offset), QA_ANGLE_RULE_NATIVE).forward;
  }
  c->weapons.fire_bullet(c->weapons.ctx, entity, game, start, forward, 3, 4, 300, 500, 0);
  qa_q2f_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = QA_Q2F_EVENT_MONSTER_MUZZLEFLASH; e.actor = &entity->actor.id; e.flash = flash;
  e.origin = start; e.direction = forward;
  game->host->emit(game->host->ctx, e);
}
void qa_q2m_infantry_attack_handler(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!q2m_rr(c->game)) {
    set_move(c, qa_q2m_target_distance(c) < 80 ? "infantry_move_attack2" : "infantry_move_attack1");
    return;
  }
  qa_q2m_finish_dodge(c);
  if (qa_q2m_target_distance(c) <= 20 && c->state->melee_time <= q2m_now(c->game)) {
    set_move(c, "infantry_move_attack2");
    return;
  }
  if (qa_q2m_clear_shot(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, 26))) {
    if (c->state->cocked) set_move(c, "infantry_move_attack1");
    else {
      set_move(c, q2m_rnd(c->game) <= 0.1 ? "infantry_move_attack5" : "infantry_move_attack3");
      c->state->next_frame = qa_q2m_rerelease_infantry_frames_value.attak405;
    }
  }
}
void qa_q2m_infantry_die_handler(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  bool rr = q2m_rr(game);
  const qa_attack_cause *cause = entity->last_attack ? &entity->last_attack->cause : NULL;
  double damage = reaction.pain.damage;
  if (qa_q2m_health(game, &entity->actor.id) <= state->gib_health
      || (rr && state->dead && cause && cause->kind == QA_ATTACK_CAUSE_Q2 && cause->means_of_death == 20)) {
    q2m_sound(game, entity, "misc/udeath.wav", true, 2, false, 0, false, 0);
    qa_q2m_gib_options plain = {false, false, false, false};
    qa_q2m_gib_options skinned = {false, false, true, false};
    if (rr) {
      const char *head = strcmp(state->move->name, "infantry_move_death3") == 0
        ? "models/monsters/infantry/gibs/head.md2" : "models/objects/gibs/sm_meat/tris.md2";
      entity->skin = trunc(entity->skin / 2);
      (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", damage, plain);
      for (int i = 0; i < 3; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
      (void)qa_q2m_throw_gib(entity, game, "models/monsters/infantry/gibs/chest.md2", damage, skinned);
      qa_q2m_gib_options gun = {false, false, true, true};
      (void)qa_q2m_throw_gib(entity, game, "models/monsters/infantry/gibs/gun.md2", damage, gun);
      for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/monsters/infantry/gibs/foot.md2", damage, skinned);
      for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/monsters/infantry/gibs/arm.md2", damage, skinned);
      qa_q2m_gib_options head_opt = {true, false, true, false};
      (void)qa_q2m_throw_gib(entity, game, head, damage, head_opt);
    } else {
      for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", damage, plain);
      for (int i = 0; i < 4; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
      (void)qa_q2m_throw_head(entity, game, "models/objects/gibs/head2/tris.md2", damage);
    }
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  state->dead = true;
  qa_q2f_combat_traits traits;
  memset(&traits, 0, sizeof traits);
  traits.has_can_take_damage = true; traits.can_take_damage = true;
  game->host->combat.set_traits(game->host->combat.ctx, entity->actor, traits);
  double n = floor(q2m_rnd(game) * 3);
  static const char *const deaths[3] = {"infantry_move_death1", "infantry_move_death2", "infantry_move_death3"};
  set_move(c, deaths[qa_q2m_record_at(3, n)]);
  q2m_sound(game, entity, n == 1 ? "infantry/infdeth1.wav" : "infantry/infdeth2.wav", true, 2, false, 0, false, 0);
  if (rr && n != 2 && q2m_rnd(game) <= 0.25) {
    qa_q2m_gib_options plain = {false, false, false, false};
    qa_q2f_entity *head = qa_q2m_throw_gib(entity, game, "models/monsters/infantry/gibs/head.md2", damage, plain);
    if (head) {
      qa_body_state body = q2m_body(game, entity);
      qa_body_state_result inflictor = {false, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {{0, 0, 0}, {0, 0, 0}}, NULL}};
      if (reaction.inflictor) inflictor = game->host->bodies.read(game->host->bodies.ctx, *reaction.inflictor);
      qa_vec3 from = inflictor.has_value ? inflictor.value.origin : body.origin;
      qa_vec3 direction = qa_scale3_double(qa_normalize3_double_hypot(qa_sub3_double(body.origin, from)), 100);
      head->angular_velocity = qa_scale3_double(head->angular_velocity, 0.15);
      qa_q2f_body_changes changes; memset(&changes, 0, sizeof changes);
      changes.has_origin = true; changes.origin = body.origin; changes.origin.z = body.origin.z + 32;
      changes.has_angles = true; changes.angles = body.angles;
      changes.has_velocity = true; changes.velocity = direction; changes.velocity.z = 200;
      q2m_move(game, head, changes, false, false);
      game->motion(game->ctx, head, QA_Q2F_MOTION_TOSS);
    }
  }
}
bool qa_q2m_infantry_duck_fn(void *ctx, qa_q2m_context *c, double eta) {
  (void)ctx; (void)eta;
  if (jumping(c)) return false;
  const qa_q2m_rerelease_infantry_frames *rf = &qa_q2m_rerelease_infantry_frames_value;
  if (c->entity->frame == rf->attak103 || c->entity->frame == rf->attak315
      || strcmp(c->state->move->name, "infantry_move_attack2") == 0) {
    qa_q2m_set_duck(c, false);
    return false;
  }
  set_move(c, "infantry_move_duck");
  return true;
}
bool qa_q2m_infantry_sidestep(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (jumping(c)) return false;
  if (strcmp(c->state->move->name, "infantry_move_run") == 0) return true;
  double frame = c->entity->frame;
  const qa_q2m_rerelease_infantry_frames *rf = &qa_q2m_rerelease_infantry_frames_value;
  bool next_is_attack4 = c->state->next_move && strcmp(c->state->next_move->name, "infantry_move_attack4") == 0;
  if (strcmp(c->state->move->name, "infantry_move_attack4") != 0 && !next_is_attack4 && !c->state->cocked
      && (frame == rf->attak103 || frame == rf->attak311 || frame == rf->attak416)) {
    c->state->fire_wait += 0.3 + q2m_rnd(c->game) * 0.3;
    c->set_move(c->impl_ctx, c, "infantry_move_attack4", true, false);
  }
  return true;
}
static void fire_cb(void *ctx, qa_q2m_context *c) {
  machine_gun(ctx, c);
  qa_q2m_state *state = c->state;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  if (!q2m_rr(game)) { state->hold_frame = q2m_now(game) < state->pause_time; return; }
  state->cocked = false;
  if (strcmp(state->move->name, "infantry_move_attack4") == 0) {
    if (q2m_now(game) >= state->fire_wait) {
      qa_q2m_finish_dodge(c);
      c->set_move(c->impl_ctx, c, "infantry_move_attack1", true, false);
      state->next_frame = qa_q2m_rerelease_infantry_frames_value.attak114;
    } else if (!can_run(c)) {
      c->set_move(c->impl_ctx, c, "infantry_move_attack1", true, false);
      state->next_frame = qa_q2m_rerelease_infantry_frames_value.attak103;
      qa_q2m_finish_dodge(c);
      state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
    }
  } else {
    const qa_q2m_rerelease_infantry_frames *f = &qa_q2m_rerelease_infantry_frames_value;
    if ((entity->frame >= f->attak101 && entity->frame <= f->attak115)
        || (entity->frame >= f->attak301 && entity->frame <= f->attak315)
        || (entity->frame >= f->attak401 && entity->frame <= f->attak424)) {
      state->hold_frame = q2m_now(game) < state->fire_wait;
      if (!state->hold_frame && entity->frame == f->attak416) state->next_frame = f->attak420;
    }
  }
}
static void jump_now(qa_q2m_context *c, bool high) {
  qa_body_state body = q2m_body(c->game, c->entity);
  qa_angle_vectors basis = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  qa_q2f_body_changes changes; memset(&changes, 0, sizeof changes);
  changes.has_velocity = true;
  changes.velocity = qa_add3_double(body.velocity, qa_add3_double(qa_scale3_double(basis.forward, high ? 150 : 100),
                                                                   qa_scale3_double(basis.up, high ? 400 : 300)));
  changes.has_ground = true; changes.ground = NULL;
  q2m_move(c->game, c->entity, changes, false, false);
  c->game->motion(c->game->ctx, c->entity, QA_Q2F_MOTION_STEP);
}
static void dead_cb(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_corpse(c); }
static void cock_gun(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2m_sound(c->game, c->entity, "infantry/infatck3.wav", true, 1, false, 0, false, 0);
  if (!q2m_rr(c->game)) c->state->pause_time = q2m_now(c->game) + (floor(q2m_rnd(c->game) * 16) + 10) * 0.1;
  else c->state->cocked = true;
}
static void set_firetime(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->fire_wait = q2m_now(c->game) + 0.7 + q2m_rnd(c->game) * 1.3;
  if (!c->state->stand_ground && c->entity->has_enemy && qa_q2m_target_distance(c) >= 330 && can_run(c))
    c->set_move(c->impl_ctx, c, "infantry_move_attack4", true, false);
}
static void swing(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2m_sound(c->game, c->entity, "infantry/infatck2.wav", true, 1, false, 0, false, 0);
}
static void smack(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, q2m_v3(80, 0, 0),
                          5 + floor(q2m_rnd(c->game) * 5), 50))
    q2m_sound(c->game, c->entity, "infantry/melee2.wav", true, 1, false, 0, false, 0);
  else if (q2m_rr(c->game)) c->state->melee_time = q2m_now(c->game) + 1.5;
}
static void duck_down(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->ducked) return;
  qa_q2m_set_duck(c, true);
  c->state->pause_time = q2m_now(c->game) + 1;
}
static void duck_hold(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->hold_frame = q2m_now(c->game) < c->state->pause_time;
}
static void duck_up(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_set_duck(c, false); }
static void shrink(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state body = q2m_body(c->game, c->entity);
  c->entity->server_flags = (double)((uint32_t)c->entity->server_flags | 2u);
  qa_q2f_body_changes changes; memset(&changes, 0, sizeof changes);
  changes.has_bounds = true; changes.bounds = body.bounds; changes.bounds.max.z = 0;
  q2m_move(c->game, c->entity, changes, false, false);
}
static void attack4_refire(void *ctx, qa_q2m_context *c) {
  if (q2m_now(c->game) >= c->state->fire_wait) {
    qa_q2m_finish_dodge(c);
    c->set_move(c->impl_ctx, c, "infantry_move_attack1", true, false);
    c->state->next_frame = qa_q2m_rerelease_infantry_frames_value.attak114;
  } else if (c->state->stand_ground
             || (c->entity->has_enemy && (qa_q2m_target_distance(c) < 330 || !can_run(c)))) {
    c->set_move(c->impl_ctx, c, "infantry_move_attack1", true, false);
    c->state->next_frame = qa_q2m_rerelease_infantry_frames_value.attak103;
    qa_q2m_finish_dodge(c);
    c->state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
  } else c->state->next_frame = qa_q2m_rerelease_infantry_frames_value.run201;
  fire_cb(ctx, c);
}
static void jump1(void *ctx, qa_q2m_context *c) { (void)ctx; jump_now(c, false); }
static void jump2(void *ctx, qa_q2m_context *c) { (void)ctx; jump_now(c, true); }
static void jump_wait_land(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_body_state body = q2m_body(game, entity);
  if (body.ground != NULL) { state->next_frame = entity->frame + 1; return; }
  state->next_frame = entity->frame;
  qa_vec3 forward = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward;
  double hx = body.velocity.x * forward.x, hy = body.velocity.y * forward.y, hz = body.velocity.z * forward.z;
  if (sqrt(hx * hx + hy * hy + hz * hz) < 150) {
    qa_vec3 velocity = qa_scale3_double(forward, 150);
    velocity.z = body.velocity.z;
    qa_q2f_body_changes changes; memset(&changes, 0, sizeof changes);
    changes.has_velocity = true; changes.velocity = velocity;
    q2m_move(game, entity, changes, false, false);
    game->motion(game->ctx, entity, QA_Q2F_MOTION_STEP);
  }
  if (state->jump_time < q2m_now(game)) state->next_frame = entity->frame + 1;
}
#define CB(name, fn) {name, {NULL, fn}}
const qa_q2m_named_handler qa_q2m_infantry_callbacks[] = {
  CB("infantry_stand", qa_q2m_infantry_stand_handler),
  CB("infantry_run", qa_q2m_infantry_run_handler),
  CB("InfantryMachineGun", machine_gun),
  CB("infantry_fire", fire_cb),
  CB("infantry_dead", dead_cb),
  CB("infantry_cock_gun", cock_gun),
  CB("infantry_set_firetime", set_firetime),
  CB("infantry_swing", swing),
  CB("infantry_smack", smack),
  CB("infantry_duck_down", duck_down),
  CB("infantry_duck_hold", duck_hold),
  CB("infantry_duck_up", duck_up),
  CB("infantry_shrink", shrink),
  CB("infantry_attack4_refire", attack4_refire),
  CB("infantry_jump_now", jump1),
  CB("infantry_jump2_now", jump2),
  CB("infantry_jump_wait_land", jump_wait_land),
};
const size_t qa_q2m_infantry_callback_count = sizeof qa_q2m_infantry_callbacks / sizeof qa_q2m_infantry_callbacks[0];
const qa_q2m_handler *qa_q2m_infantry_callback(const char *name) {
  for (size_t i = 0; i < qa_q2m_infantry_callback_count; i++)
    if (strcmp(name, qa_q2m_infantry_callbacks[i].name) == 0) return &qa_q2m_infantry_callbacks[i].handler;
  return NULL;
}
