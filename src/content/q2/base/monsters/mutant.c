/* Port of src/content/q2/base/monsters/mutant.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/mutant.h"

#include <math.h>
#include <stdio.h>

#include "base_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/mutant.h"
#include "qa/content/q2/foundation/monsters/ai.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->state->stand_ground ? "mutant_move_stand" : "mutant_move_run");
}
static void hit(qa_q2m_context *c, bool right) {
  qa_bounds bounds = q2bm_body(c->game, c->entity).bounds;
  bool connected = c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game,
                                       q2bm_v3(80, right ? bounds.max.x : bounds.min.x, 8),
                                       10 + floor(q2bm_rnd(c->game) * 5), 100);
  q2bm_sound(c->game, c->entity, connected ? (right ? "mutant/mutatck3.wav" : "mutant/mutatck2.wav")
                                            : "mutant/mutatck1.wav",
             true, 1, false, 0, false, 0);
}
static void jump_touch_fn(void *ctx, qa_q2f_entity *self, qa_q2f_game *services, qa_touch_contact contact) {
  qa_q2m_context *context = qa_q2m_monsters_context(ctx, self->actor.id);
  if (context == NULL) qa_q2f_error("Mutant jump has no source continuation");
  if (qa_q2m_health(services, &self->actor.id) <= 0) {
    self->touch = (qa_q2f_touch){NULL, NULL};
    return;
  }
  qa_body_state current = services->body(services->ctx, self->actor);
  qa_combat_state_result other = services->host->combat.read(services->host->combat.ctx, contact.other);
  bool can_take_damage = other.has_value && other.value.can_take_damage;
  if (can_take_damage && qa_hypot3(current.velocity) > 400) {
    qa_vec3 normal = qa_normalize3_double_hypot(current.velocity);
    qa_vec3 point = qa_add3_double(current.origin, qa_scale3_double(normal, current.bounds.max.x));
    double damage = trunc(40 + 10 * q2bm_rnd(services));
    qa_q2f_damage_args args;
    memset(&args, 0, sizeof args);
    args.target = contact.other; args.inflictor_entity = self; args.inflictor_actor = self->actor.id;
    args.attacker = &self->actor.id;
    args.amount = damage; args.knockback = damage;
    args.direction = current.velocity; args.point = point; args.normal = normal;
    args.means_of_death = 0; args.has_flags = false; args.weapon = NULL;
    (void)services->damage(services->ctx, args);
  }
  if (!qa_q2m_check_bottom(context, services->body(services->ctx, self->actor).origin)) {
    if (services->body(services->ctx, self->actor).ground != NULL) {
      context->state->next_frame = qa_q2bm_mutant_frames_value.attack02;
      self->touch = (qa_q2f_touch){NULL, NULL};
    }
    return;
  }
  self->touch = (qa_q2f_touch){NULL, NULL};
}
static const qa_q2bm_sound_spec sight_spec = {"mutant/mutsght1.wav", 2, 1};
static const qa_q2bm_sound_spec search_spec = {"mutant/mutsrch1.wav", 2, 1};
static void idle_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, "mutant_move_idle");
  q2bm_sound(c->game, c->entity, "mutant/mutidle1.wav", true, 2, true, 1, true, 2);
}
static bool check_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!qa_q2bm_alive_enemy(c)) return false;
  if (qa_q2m_target_distance(c) < 80) { c->state->attack_state = QA_Q2M_ATTACK_MELEE; return true; }
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return false;
  qa_body_state body = q2bm_body(c->game, c->entity);
  double enemy_minimum = enemy.origin.z + enemy.bounds.min.z;
  double enemy_size = enemy.bounds.max.z - enemy.bounds.min.z;
  if (body.origin.z + body.bounds.min.z > enemy_minimum + 0.75 * enemy_size
      || body.origin.z + body.bounds.max.z < enemy_minimum + 0.25 * enemy_size)
    return false;
  qa_vec3 delta = qa_sub3_double(body.origin, enemy.origin);
  double distance = hypot(delta.x, delta.y);
  if (distance < 100 || (distance > 100 && q2bm_rnd(c->game) < 0.9)) return false;
  c->state->attack_state = QA_Q2M_ATTACK_MISSILE;
  return true;
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2bm_damaged_skin(c);
  if (q2bm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  if (c->game->options.skill == 3) return;
  double r = q2bm_rnd(c->game);
  q2bm_sound(c->game, c->entity, r < 0.33 || r >= 0.66 ? "mutant/mutpain1.wav" : "mutant/mutpain2.wav",
             true, 2, false, 0, false, 0);
  q2bm_set_move(c, r < 0.33 ? "mutant_move_pain1" : r < 0.66 ? "mutant_move_pain2" : "mutant_move_pain3");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  if (qa_q2bm_standard_gib_default(c, reaction) || c->state->dead) return;
  c->entity->skin = 1;
  qa_q2bm_begin_death_default(c, reaction, "mutant/mutdeth1.wav",
                              q2bm_rnd(c->game) < 0.5 ? "mutant_move_death1" : "mutant_move_death2");
}
static void idle_loop_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2bm_rnd(c->game) < 0.75) c->state->next_frame = qa_q2bm_mutant_frames_value.stand155;
}
static void step_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double n = fmod(floor(q2bm_rnd(c->game) * 3) + 1, 3);
  char path[64];
  snprintf(path, sizeof path, "mutant/step%g.wav", n + 1);
  q2bm_sound(c->game, c->entity, path, true, 2, false, 0, false, 0);
}
static void hit_left_fn(void *ctx, qa_q2m_context *c) { (void)ctx; hit(c, false); }
static void hit_right_fn(void *ctx, qa_q2m_context *c) { (void)ctx; hit(c, true); }
static void check_refire_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2bm_alive_enemy(c)
      && ((c->game->options.skill == 3 && q2bm_rnd(c->game) < 0.5) || qa_q2m_target_distance(c) < 80))
    c->state->next_frame = qa_q2bm_mutant_frames_value.attack09;
}
static void jump_takeoff_fn(void *ctx, qa_q2m_context *c) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_body_state body = q2bm_body(game, entity);
  qa_vec3 forward = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward;
  q2bm_sound(game, entity, "mutant/mutsght1.wav", true, 2, false, 0, false, 0);
  qa_vec3 velocity = qa_scale3_double(forward, 600);
  velocity.z = 250;
  qa_vec3 origin = body.origin; origin.z = body.origin.z + 1;
  qa_q2f_body_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_origin = true; changes.origin = origin;
  changes.has_velocity = true; changes.velocity = velocity;
  changes.has_ground = true; changes.ground = NULL;
  q2bm_move_body(game, entity, changes, false, false);
  c->state->ducked = true;
  c->state->attack_finished = q2bm_now(game) + 3;
  entity->touch = (qa_q2f_touch){ctx, jump_touch_fn};
}
static void check_landing_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2bm_body(c->game, c->entity).ground != NULL) {
    q2bm_sound(c->game, c->entity, "mutant/thud1.wav", true, 1, false, 0, false, 0);
    c->state->attack_finished = 0;
    c->state->ducked = false;
    return;
  }
  c->state->next_frame = q2bm_now(c->game) > c->state->attack_finished
      ? qa_q2bm_mutant_frames_value.attack02 : qa_q2bm_mutant_frames_value.attack05;
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2bm_finish_corpse(c);
  qa_q2m_fly_check(c);
}
void qa_q2bm_create_mutant_definition_source(qa_q2bm_mutant_definition *out, qa_q2m_monsters *monsters,
                                             const char *source) {
  const char *touch_name = source && strcmp(source, "rogue") == 0
      ? "rogue_mutant_jump_touch" : "classic_mutant_jump_touch";
  qa_q2m_named_handler callbacks[12] = {
    {"mutant_stand", {(void *)"mutant_move_stand", qa_q2bm_move_handler_fn}},
    {"mutant_walk", {(void *)"mutant_move_start_walk", qa_q2bm_move_handler_fn}},
    {"mutant_walk_loop", {(void *)"mutant_move_walk", qa_q2bm_move_handler_fn}},
    {"mutant_run", {NULL, run_fn}},
    {"mutant_idle_loop", {NULL, idle_loop_fn}},
    {"mutant_step", {NULL, step_fn}},
    {"mutant_hit_left", {NULL, hit_left_fn}},
    {"mutant_hit_right", {NULL, hit_right_fn}},
    {"mutant_check_refire", {NULL, check_refire_fn}},
    {"mutant_jump_takeoff", {monsters, jump_takeoff_fn}},
    {"mutant_check_landing", {NULL, check_landing_fn}},
    {"mutant_dead", {NULL, dead_fn}},
  };
  memcpy(out->callbacks, callbacks, sizeof callbacks);
  out->touch_defs[0].name = touch_name;
  out->touch_defs[0].cb = (qa_q2f_touch){monsters, jump_touch_fn};
  qa_q2m_definition d = {
    .classname = "monster_mutant", .kind = "mutant", .model = "models/monsters/mutant/tris.md2",
    .health = 300, .gib_health = -120, .mass = 300, .bounds = {{-32, -32, -24}, {32, 32, 48}}, .scale = 1,
    .initial_move = "mutant_move_stand",
    .moves = qa_q2bm_mutant_moves, .move_count = qa_q2bm_mutant_move_count,
    .callbacks = out->callbacks, .callback_count = 12,
    .stand = {(void *)"mutant_move_stand", qa_q2bm_move_handler_fn},
    .walk = {(void *)"mutant_move_start_walk", qa_q2bm_move_handler_fn},
    .run = {NULL, run_fn},
    .attack = {(void *)"mutant_move_jump", qa_q2bm_move_handler_fn},
    .melee = {(void *)"mutant_move_attack", qa_q2bm_move_handler_fn},
    .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
    .search = {(void *)&search_spec, qa_q2bm_sound_handler_fn},
    .idle = {NULL, idle_fn},
    .check_attack = check_attack_fn,
    .has_source_callbacks = true,
    .source_callbacks = {.touch = out->touch_defs, .touch_count = 1},
    .reactions = {NULL, pain_fn, die_fn},
  };
  out->definition = d;
}
void qa_q2bm_create_mutant_definition(qa_q2bm_mutant_definition *out, qa_q2m_monsters *monsters) {
  qa_q2bm_create_mutant_definition_source(out, monsters, "classic");
}
