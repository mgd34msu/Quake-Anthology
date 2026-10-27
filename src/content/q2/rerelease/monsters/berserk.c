/* Port of src/content/q2/rerelease/monsters/berserk.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/monsters/berserk.h"

#include <stdlib.h>

#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/berserk.h"
#include "rrm_internal.h"

static bool jumping(qa_q2m_context *c) {
  const char *name = c->state->move->name;
  return strcmp(name, "berserk_move_jump") == 0 || strcmp(name, "berserk_move_jump2") == 0
      || strcmp(name, "berserk_move_attack_strike") == 0;
}
static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_finish_dodge(c);
  q2rm_set_move(c, c->state->stand_ground ? "berserk_move_stand" : "berserk_move_run1");
}
static void melee_impl(qa_q2m_context *c) {
  if (c->state->melee_time > q2rm_now(c->game)) return;
  if (strcmp(c->state->move->name, "berserk_move_run_attack1") == 0
      && c->entity->frame >= qa_q2rm_berserk_frames_value.r_att13) {
    c->state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
    c->state->attack_finished = 0;
    return;
  }
  qa_q2m_finish_dodge(c);
  q2rm_set_move(c, q2rm_rnd(c->game) < 0.5 ? "berserk_move_attack_spike" : "berserk_move_attack_club");
}
static void melee_fn(void *ctx, qa_q2m_context *c) { (void)ctx; melee_impl(c); }

void qa_q2rm_slam_radius_damage(qa_q2m_context *context, qa_vec3 origin, double damage, double kick, double radius) {
  qa_q2f_game *game = context->game;
  qa_q2f_entity *entity = context->entity;
  qa_vec3 point = origin;
  size_t count = 0;
  const qa_actor_id *actors = game->host->nearby(game->host->ctx, q2rm_body(game, entity).origin, radius * 2, &count);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor = actors[i];
    if (qa_same_actor(actor, entity->actor.id)) continue;
    qa_combat_state_result combat = game->host->combat.read(game->host->combat.ctx, actor);
    if (!combat.has_value || !combat.value.can_take_damage) continue;
    if (!game->can_damage(game->ctx, actor, entity->actor)) continue;
    qa_body_state_result target = game->host->bodies.read(game->host->bodies.ctx, actor);
    if (!target.has_value) continue;
    if (game->host->is_player(game->host->ctx, actor) && !target.value.ground) continue;
    qa_vec3 min = qa_add3_double(target.value.origin, target.value.bounds.min);
    qa_vec3 max = qa_add3_double(target.value.origin, target.value.bounds.max);
    qa_vec3 closest = {fmax(min.x, fmin(max.x, point.x)), fmax(min.y, fmin(max.y, point.y)),
                       fmax(min.z, fmin(max.z, point.z))};
    double amount = fmin(1, 1 - qa_hypot3(qa_sub3_double(closest, point)) / radius);
    if (amount <= 0) continue;
    qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(target.value.origin, point));
    point.z = min.z;
    double scaled_damage = damage * amount * amount;
    (void)q2rm_damage(game, actor, entity, &entity->actor.id, trunc(fmax(1, scaled_damage)),
                      trunc(kick * amount * amount), direction, point, direction, 0, true, 1);
    qa_q2f_owned_result owned = game->host->actors.resolve_owned(game->host->actors.ctx, actor);
    qa_body_state_result after = game->host->bodies.read(game->host->bodies.ctx, actor);
    if (game->host->is_player(game->host->ctx, actor) && owned.has_value && after.has_value) {
      qa_body_state written = after.value;
      written.velocity.z = fmax(270, after.value.velocity.z);
      game->host->bodies.write(game->host->bodies.ctx, owned.value, written);
    }
  }
}

static void slam(qa_q2m_context *c) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  q2rm_snd(c, "mutant/thud1.wav", 1);
  q2rm_sound(game, entity, "world/explod2.wav", true, 0, true, 0.75, false, 0);
  qa_vec3 start = qa_q2m_project_flash_default(c, q2rm_v3(20, -14.3, -21));
  qa_trace_result trace = q2rm_trace(game, q2rm_body(game, entity).origin, start, NULL, &entity->actor.id, 3);
  q2rm_emit_effect(game, "q2:berserk-slam", trace.end, q2rm_v3(0, 0, 1), 1, 0);
  entity->gravity = 1;
  entity->flags = q2rm_or(entity->flags, 1 << 23);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_velocity = true; change.velocity = q2rm_zero();
  q2rm_move_body(game, entity, change, false, false);
  game->motion(game->ctx, entity, entity->motion);
  qa_q2rm_slam_radius_damage(c, trace.end, 8, 300, 165);
}
static void high_gravity(qa_q2m_context *c) {
  qa_actor_id world = c->game->host->world_actor(c->game->host->ctx);
  qa_q2f_entity *world_entity = c->game->entity(c->game->ctx, &world);
  double gravity = !world_entity ? 800 : qa_q2f_number_field(&world_entity->spawn, "gravity", 800);
  c->entity->gravity = (q2rm_body(c->game, c->entity).velocity.z < 0 ? 2.25 : 5.25) * (800 / gravity);
  c->game->motion(c->game->ctx, c->entity, c->entity->motion);
}
static void high_gravity_fn(void *ctx, qa_q2m_context *c) { (void)ctx; high_gravity(c); }

static void touch_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_touch_contact contact) {
  qa_q2m_monsters *monsters = ctx;
  (void)contact;
  qa_q2m_context *context = qa_q2m_monsters_context(monsters, entity->actor.id);
  if (!context) return;
  if (qa_q2m_health(game, &entity->actor.id) <= 0) { entity->touch.fn = NULL; entity->touch.ctx = NULL; return; }
  if (q2rm_body(game, entity).ground) {
    entity->frame = qa_q2rm_berserk_frames_value.slam18;
    if (entity->touch.fn) slam(context);
    entity->touch.fn = NULL; entity->touch.ctx = NULL;
  }
}

static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  double distance = qa_q2m_target_distance(c);
  if (c->state->melee_time <= q2rm_now(game) && distance < 80) { melee_impl(c); return; }
  if (!q2rm_has(entity->spawnflags, 8) && entity->timestamp < q2rm_now(game) && q2rm_rnd(game) < 0.5 && distance > 150) {
    q2rm_set_move(c, "berserk_move_attack_strike");
    entity->timestamp = q2rm_now(game) + 5;
    q2rm_snd(c, "berserk/jump.wav", 1);
    return;
  }
  if (strcmp(c->state->move->name, "berserk_move_run1") == 0 && distance <= 500) {
    q2rm_set_move(c, "berserk_move_run_attack1");
    c->state->next_frame = qa_q2rm_berserk_frames_value.r_att1 + entity->frame - qa_q2rm_berserk_frames_value.run1 + 1;
  }
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  if (jumping(c) || q2rm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2rm_now(c->game) + 3;
  q2rm_snd(c, "berserk/berpain2.wav", 2);
  if (!qa_q2rm_reacts_to_pain(c)) return;
  qa_q2m_finish_dodge(c);
  q2rm_set_move(c, reaction.damage <= 50 || q2rm_rnd(c->game) < 0.5 ? "berserk_move_pain1" : "berserk_move_pain2");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  double damage = reaction.pain.damage;
  if (qa_q2rm_check_gib(c)) {
    q2rm_snd(c, "misc/udeath.wav", 2);
    entity->skin = 0;
    qa_q2m_gib_options plain = {false, false, false, false};
    for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", damage, plain);
    for (int i = 0; i < 3; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/gear/tris.md2", damage, plain);
    static const char *const parts[4] = {"chest", "hammer", "thigh", "head"};
    static const char *const models[4] = {
      "models/monsters/berserk/gibs/chest.md2", "models/monsters/berserk/gibs/hammer.md2",
      "models/monsters/berserk/gibs/thigh.md2", "models/monsters/berserk/gibs/head.md2"};
    for (int i = 0; i < 4; i++) {
      qa_q2m_gib_options options = {strcmp(parts[i], "head") == 0, false, true, strcmp(parts[i], "hammer") == 0};
      (void)qa_q2m_throw_gib(entity, game, models[i], damage, options);
    }
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  q2rm_snd(c, "berserk/berdeth2.wav", 2);
  state->dead = true; state->can_take_damage = true;
  q2rm_set_damageable(game, entity, true);
  q2rm_set_move(c, damage >= 50 ? "berserk_move_death1" : "berserk_move_death2");
}
static bool duck_fn(void *ctx, qa_q2m_context *c, double eta) {
  (void)ctx; (void)eta;
  const char *name = c->state->move->name;
  if (q2rm_rnd(c->game) >= 0.05 || strcmp(name, "berserk_move_jump") == 0 || strcmp(name, "berserk_move_jump2") == 0)
    return false;
  q2rm_set_move(c, "berserk_move_duck2");
  return true;
}
static bool sidestep_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (jumping(c) || strcmp(c->state->move->name, "berserk_move_pain2") == 0) return false;
  if (strcmp(c->state->move->name, "berserk_move_run1") != 0) q2rm_set_move(c, "berserk_move_run1");
  return true;
}
static bool blocked_fn(void *ctx, qa_q2m_context *c, double distance) {
  (void)ctx;
  qa_q2rm_jump_navigation navigation;
  memset(&navigation, 0, sizeof navigation);
  qa_q2rm_jump_result jump = qa_q2rm_blocked_check_jump(c, distance, 256, 40,
                                                        !q2rm_has(c->entity->spawnflags, 8), navigation);
  if (jump != QA_Q2RM_JUMP_NONE) {
    if (jump != QA_Q2RM_JUMP_TURN) q2rm_set_move(c, jump == QA_Q2RM_JUMP_UP ? "berserk_move_jump2" : "berserk_move_jump");
    return true;
  }
  return qa_q2rm_blocked_check_platform(c, distance);
}
static void fidget_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->stand_ground || c->entity->has_enemy || q2rm_rnd(c->game) > 0.15) return;
  q2rm_set_move(c, "berserk_move_stand_fidget");
  q2rm_snd4(c, "berserk/beridle1.wav", 1, 1, 2);
}
static void spike_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, q2rm_v3(80, 0, -24),
                           5 + floor(q2rm_rnd(c->game) * 6), 80))
    c->state->melee_time = q2rm_now(c->game) + 1.2;
}
static void club_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 aim = {80, q2rm_body(c->game, c->entity).bounds.min.x, -4};
  if (!c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, aim, 15 + floor(q2rm_rnd(c->game) * 6), 400))
    c->state->melee_time = q2rm_now(c->game) + 2.5;
}
static void run_attack_speed_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->entity->has_enemy && qa_q2m_target_distance(c) < 80) {
    c->state->next_frame = c->entity->frame + 6;
    qa_q2m_finish_dodge(c);
  }
}
static void run_swing_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_snd(c, "berserk/attack.wav", 1);
  c->state->melee_time = q2rm_now(c->game) + 0.6;
  if (c->state->attack_state == QA_Q2M_ATTACK_SLIDING) qa_q2m_finish_dodge(c);
}
static void takeoff_fn(void *ctx, qa_q2m_context *c) {
  qa_q2m_monsters *monsters = ctx;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_body_state body = q2rm_body(c->game, c->entity);
  double speed = qa_hypot3(qa_sub3_double(body.origin, enemy.origin)) * 1.95;
  qa_vec3 direction;
  if (!qa_q2rm_predicted_direction_default(c, body.origin, speed, false, &direction)) return;
  qa_vec3 angles = body.angles;
  angles.y = qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  qa_vec3 forward = qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_NATIVE).forward;
  qa_vec3 velocity = qa_scale3_double(forward, speed);
  velocity.z = 450;
  qa_vec3 origin = body.origin;
  origin.z += 1;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = origin;
  change.has_velocity = true; change.velocity = velocity;
  change.has_angles = true; change.angles = angles;
  change.has_ground = true; change.ground = NULL;
  q2rm_move_body(c->game, c->entity, change, false, false);
  c->state->ducked = true;
  c->state->attack_finished = q2rm_now(c->game) + 3;
  c->entity->touch.ctx = monsters;
  c->entity->touch.fn = touch_fn;
  high_gravity(c);
}
static void check_landing_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  high_gravity(c);
  if (q2rm_body(c->game, c->entity).ground) {
    c->state->attack_finished = 0;
    qa_q2m_set_duck(c, false);
    c->entity->frame = qa_q2rm_berserk_frames_value.slam18;
    if (c->entity->touch.fn) { slam(c); c->entity->touch.fn = NULL; c->entity->touch.ctx = NULL; }
    c->entity->flags = q2rm_and(c->entity->flags, ~(1 << 23));
    return;
  }
  c->state->next_frame = q2rm_now(c->game) > c->state->attack_finished
      ? qa_q2rm_berserk_frames_value.slam3 : qa_q2rm_berserk_frames_value.slam5;
}
static void shrink_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->server_flags = q2rm_or(c->entity->server_flags, 2);
  qa_bounds bounds = q2rm_body(c->game, c->entity).bounds;
  bounds.max.z = 0;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true; change.bounds = bounds;
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void jump_now_impl(qa_q2m_context *c, double forward_scale, double up_scale) {
  qa_body_state body = q2rm_body(c->game, c->entity);
  qa_angle_vectors axis = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  qa_vec3 velocity = qa_add3_double(body.velocity,
      qa_add3_double(qa_scale3_double(axis.forward, forward_scale), qa_scale3_double(axis.up, up_scale)));
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_velocity = true; change.velocity = velocity;
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void jump_now_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_now_impl(c, 100, 300); }
static void jump2_now_fn(void *ctx, qa_q2m_context *c) { (void)ctx; jump_now_impl(c, 150, 400); }
static void wait_land_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->next_frame = c->entity->frame + (q2rm_body(c->game, c->entity).ground || qa_q2rm_monster_jump_finished(c) ? 1 : 0);
}
static void corpse_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_corpse(c); }
static const qa_q2bm_sound_spec sight_spec = {"berserk/sight.wav", 2, 1};
static const qa_q2bm_sound_spec search_spec = {"berserk/bersrch1.wav", 2, 1};
static const qa_q2bm_sound_spec swing_spec = {"berserk/attack.wav", 1, 1};

qa_q2m_definition qa_q2rm_create_rerelease_berserk_definition(qa_q2m_monsters *monsters) {
  qa_q2m_named_handler *callbacks = malloc(13 * sizeof *callbacks);
  if (!callbacks) qa_q2f_error("Out of memory creating rerelease berserk definition");
  callbacks[0] = (qa_q2m_named_handler){"berserk_stand", {(void *)"berserk_move_stand", qa_q2bm_move_handler_fn}};
  callbacks[1] = (qa_q2m_named_handler){"berserk_run", {NULL, run_fn}};
  callbacks[2] = (qa_q2m_named_handler){"berserk_dead", {NULL, corpse_fn}};
  callbacks[3] = (qa_q2m_named_handler){"berserk_fidget", {NULL, fidget_fn}};
  callbacks[4] = (qa_q2m_named_handler){"berserk_swing", {(void *)&swing_spec, qa_q2bm_sound_handler_fn}};
  callbacks[5] = (qa_q2m_named_handler){"berserk_attack_spike", {NULL, spike_fn}};
  callbacks[6] = (qa_q2m_named_handler){"berserk_attack_club", {NULL, club_fn}};
  callbacks[7] = (qa_q2m_named_handler){"berserk_run_attack_speed", {NULL, run_attack_speed_fn}};
  callbacks[8] = (qa_q2m_named_handler){"berserk_run_swing", {NULL, run_swing_fn}};
  callbacks[9] = (qa_q2m_named_handler){"berserk_high_gravity", {NULL, high_gravity_fn}};
  callbacks[10] = (qa_q2m_named_handler){"berserk_jump_takeoff", {monsters, takeoff_fn}};
  callbacks[11] = (qa_q2m_named_handler){"berserk_check_landing", {NULL, check_landing_fn}};
  callbacks[12] = (qa_q2m_named_handler){"berserk_shrink", {NULL, shrink_fn}};
  /* jump_now / jump2_now / jump_wait_land appended below via realloc-free second table merge. */
  const qa_q2m_named_handler extra[3] = {
    {"berserk_jump_now", {NULL, jump_now_fn}},
    {"berserk_jump2_now", {NULL, jump2_now_fn}},
    {"berserk_jump_wait_land", {NULL, wait_land_fn}},
  };
  size_t callback_count = 0;
  qa_q2m_named_handler *merged = qa_q2rm_merge_callbacks(callbacks, 13, extra, 3, &callback_count);
  free(callbacks);
  qa_q2f_named_touch *touch_defs = malloc(sizeof *touch_defs);
  if (!touch_defs) qa_q2f_error("Out of memory creating rerelease berserk definition");
  touch_defs[0].name = "berserk_jump_touch";
  touch_defs[0].cb.ctx = monsters;
  touch_defs[0].cb.fn = touch_fn;
  qa_q2m_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.classname = "monster_berserk";
  definition.kind = "berserk";
  definition.model = "models/monsters/berserk/tris.md2";
  definition.health = 240; definition.gib_health = -60; definition.mass = 250;
  definition.bounds = qa_q2bm_humanoid_bounds;
  definition.scale = 1;
  definition.initial_move = "berserk_move_stand";
  definition.moves = qa_q2rm_berserk_moves;
  definition.move_count = qa_q2rm_berserk_move_count;
  definition.callbacks = merged;
  definition.callback_count = callback_count;
  definition.stand = qa_q2bm_move_handler("berserk_move_stand");
  definition.walk = qa_q2bm_move_handler("berserk_move_walk");
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.attack = (qa_q2m_handler){NULL, attack_fn};
  definition.melee = (qa_q2m_handler){NULL, melee_fn};
  definition.sight = qa_q2bm_sound_handler(&sight_spec);
  definition.search = qa_q2bm_sound_handler(&search_spec);
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.has_source_callbacks = true;
  definition.source_callbacks.touch = touch_defs;
  definition.source_callbacks.touch_count = 1;
  definition.duck_ctx = NULL; definition.duck = duck_fn;
  definition.sidestep_ctx = NULL; definition.sidestep = sidestep_fn;
  definition.blocked_ctx = NULL; definition.blocked = blocked_fn;
  return definition;
}
