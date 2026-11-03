/* Port of src/content/q2/rerelease/monsters/base-variants/flyer.ts.
 * Rerelease m_flyer.cpp normal Flyer. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/base_variants/flyer.h"

#include <stdio.h>
#include <stdlib.h>

#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/flyer.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/flyer.h"
#include "../rrm_internal.h"

static void flight(qa_q2m_context *c, bool melee) {
  qa_q2m_alternate_fly_state *alt = &c->state->alt;
  if (melee) { alt->fly_pinned = false; alt->fly_position_time = 0; }
  alt->fly_thrusters = melee;
  alt->fly_acceleration = melee ? 20 : 15;
  alt->fly_speed = melee ? 210 : 165;
  alt->fly_min_distance = melee ? 0 : 45;
  alt->fly_max_distance = melee ? 10 : 200;
}
static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_set_move(c, c->state->stand_ground ? "flyer_move_stand" : "flyer_move_run");
}
static void fire(qa_q2m_context *c, double flash) {
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, flash, &aim)) return;
  double effects = fmod(c->entity->frame, 4) == 0 ? 64 : 0;
  (void)c->weapons.fire_blaster(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 1, 1000, effects, false, false, 0);
  qa_q2rm_monster_flash(c, flash, aim.start, aim.direction);
}
static void slash(qa_q2m_context *c, bool right) {
  qa_bounds bounds = q2rm_body(c->game, c->entity).bounds;
  qa_vec3 aim = {80, right ? bounds.max.x : bounds.min.x, 0};
  if (!c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, aim, 5, 0))
    c->state->melee_time = q2rm_now(c->game) + 1.5;
  q2rm_snd(c, "flyer/flyatck2.wav", 1);
}
static void touch_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_touch_contact contact) {
  qa_q2m_monsters *monsters = ctx;
  qa_q2m_context *context = qa_q2m_monsters_context(monsters, entity->actor.id);
  qa_q2m_context *other = qa_q2m_monsters_context(monsters, contact.other);
  if (!context || !other || !other->state->alt.alternate_fly || other->state->locomotion != QA_Q2M_LOCO_FLY
      || context->state->duck_wait >= q2rm_now(game)) return;
  context->state->duck_wait = q2rm_now(game) + 1;
  context->state->alt.fly_thrusters = false;
  qa_vec3 origin = q2rm_body(game, entity).origin;
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(origin, q2rm_body(game, other->entity).origin));
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_velocity = true; change.velocity = qa_scale3_double(direction, 500);
  q2rm_move_body(game, entity, change, false, false);
  q2rm_emit_effect(game, "q2:splash", origin, direction, 32, 1);
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  c->state->alt.alternate_fly = true;
  c->state->alt.fly_buzzard = true;
  c->entity->touch.ctx = ctx;
  c->entity->touch.fn = touch_fn;
  flight(c, false);
  qa_q2bm_loop_sound(c, "flyer/flyidle1.wav");
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_state *state = c->state;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_body_state enemy;
  bool have_enemy = qa_q2m_enemy_body(c, &enemy);
  double range = qa_q2m_target_distance(c);
  state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
  if (have_enemy && qa_q2m_visible_enemy(c) && range <= 225 && q2rm_rnd(game) > range / 225 * 0.35) {
    q2rm_set_move(c, "flyer_move_start_melee");
    flight(c, true);
  } else q2rm_set_move(c, "flyer_move_attack2");
  qa_q2m_alternate_fly_state *alt = &state->alt;
  if (!alt->fly_pinned && qa_q2_rerelease_random_integer_max(qa_q2rm_rerelease_random(c), 2) != 0
      && have_enemy && qa_q2m_visible_enemy(c)) {
    alt->fly_pinned = true;
    alt->fly_position_time += 1.7;
    qa_body_state body = q2rm_body(game, entity);
    alt->fly_ideal_position = qa_q2_rerelease_random_integer_max(qa_q2rm_rerelease_random(c), 2) != 0
        ? qa_add3_double(body.origin, qa_scale3_double(body.velocity, q2rm_rnd(game)))
        : qa_add3_double(alt->fly_ideal_position, enemy.origin);
  }
}
static void melee_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_set_move(c, "flyer_move_start_melee");
  flight(c, true);
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2 ? 1 : 0;
  if (q2rm_now(game) < state->pain_time) return;
  state->pain_time = q2rm_now(game) + 3;
  int32_t choice = qa_q2_rerelease_random_integer_max(qa_q2rm_rerelease_random(c), 3);
  q2rm_snd(c, choice == 1 ? "flyer/flypain2.wav" : "flyer/flypain1.wav", 2);
  if (!qa_q2rm_reacts_to_pain(c)) return;
  flight(c, false);
  char buffer[32];
  (void)snprintf(buffer, sizeof buffer, "flyer_move_pain%d", (int)choice + 1);
  q2rm_set_move(c, buffer);
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  q2rm_snd(c, "flyer/flydeth1.wav", 2);
  q2rm_emit_effect(game, "q2:explosion1", q2rm_body(game, entity).origin, q2rm_zero(), 1, 0);
  entity->skin = trunc(entity->skin / 2);
  qa_q2m_gib_options plain = {false, false, false, false};
  qa_q2m_gib_options skinned = {false, false, true, false};
  qa_q2m_gib_options head = {true, false, true, false};
  for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_metal/tris.md2", 55, plain);
  for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", 55, plain);
  (void)qa_q2m_throw_gib(entity, game, "models/monsters/flyer/gibs/base.md2", 55, skinned);
  static const char *const parts[2] = {"models/monsters/flyer/gibs/gun.md2", "models/monsters/flyer/gibs/wing.md2"};
  for (int p = 0; p < 2; p++)
    for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, parts[p], 55, skinned);
  (void)qa_q2m_throw_gib(entity, game, "models/monsters/flyer/gibs/head.md2", 55, head);
  entity->touch.fn = NULL; entity->touch.ctx = NULL;
  state->dead = true; state->gibbed = true;
}
static void fire_left_fn(void *ctx, qa_q2m_context *c) { (void)ctx; fire(c, 58); }
static void fire_right_fn(void *ctx, qa_q2m_context *c) { (void)ctx; fire(c, 59); }
static void slash_left_fn(void *ctx, qa_q2m_context *c) { (void)ctx; slash(c, false); }
static void slash_right_fn(void *ctx, qa_q2m_context *c) { (void)ctx; slash(c, true); }
static void check_melee_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2m_target_distance(c) <= 80 && c->state->melee_time <= q2rm_now(c->game)) {
    q2rm_set_move(c, "flyer_move_loop_melee");
    return;
  }
  q2rm_set_move(c, "flyer_move_end_melee");
  flight(c, false);
}
static const qa_q2bm_sound_spec pop_spec = {"flyer/flyatck1.wav", 2, 1};

qa_q2m_definition qa_q2rm_create_rerelease_flyer_definition(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters) {
  /* TS filters out flyer_move_kamikaze from the table. */
  qa_q2m_move *moves = qa_q2mpm_arena_alloc(arena, qa_q2rm_flyer_move_count * sizeof *moves);
  qa_q2_named_touch *touch_defs = qa_q2mpm_arena_alloc(arena, sizeof *touch_defs);
  if (!moves || !touch_defs) qa_raise("Error", 0, 0, "Out of memory creating rerelease flyer definition");
  size_t move_count = 0;
  for (size_t i = 0; i < qa_q2rm_flyer_move_count; i++)
    if (strcmp(qa_q2rm_flyer_moves[i].name, "flyer_move_kamikaze") != 0) moves[move_count++] = qa_q2rm_flyer_moves[i];
  touch_defs[0].name = "flyer_touch";
  touch_defs[0].cb.ctx = monsters;
  touch_defs[0].cb.fn = touch_fn;
  qa_q2m_named_handler *callbacks = qa_q2mpm_arena_alloc(arena, 8 * sizeof *callbacks);
  if (!callbacks) qa_raise("Error", 0, 0, "Out of memory creating rerelease flyer definition");
  callbacks[0] = (qa_q2m_named_handler){"flyer_run", {NULL, run_fn}};
  callbacks[1] = (qa_q2m_named_handler){"flyer_pop_blades", {(void *)&pop_spec, qa_q2bm_sound_handler_fn}};
  callbacks[2] = (qa_q2m_named_handler){"flyer_loop_melee", {(void *)"flyer_move_loop_melee", qa_q2bm_move_handler_fn}};
  callbacks[3] = (qa_q2m_named_handler){"flyer_fireleft", {NULL, fire_left_fn}};
  callbacks[4] = (qa_q2m_named_handler){"flyer_fireright", {NULL, fire_right_fn}};
  callbacks[5] = (qa_q2m_named_handler){"flyer_slash_left", {NULL, slash_left_fn}};
  callbacks[6] = (qa_q2m_named_handler){"flyer_slash_right", {NULL, slash_right_fn}};
  callbacks[7] = (qa_q2m_named_handler){"flyer_check_melee", {NULL, check_melee_fn}};
  qa_q2m_definition definition = qa_q2bm_flyer_definition;
  definition.moves = moves;
  definition.move_count = move_count;
  definition.bounds.min = q2rm_v3(-16, -16, -24);
  definition.bounds.max = q2rm_v3(16, 16, 16);
  definition.has_view_height = true;
  definition.view_height = 12;
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.has_source_callbacks = true;
  memset(&definition.source_callbacks, 0, sizeof definition.source_callbacks);
  definition.source_callbacks.touch = touch_defs;
  definition.source_callbacks.touch_count = 1;
  definition.initialize = (qa_q2m_handler){monsters, init_fn};
  definition.attack = (qa_q2m_handler){NULL, attack_fn};
  definition.melee = (qa_q2m_handler){NULL, melee_fn};
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.callbacks = callbacks;
  definition.callback_count = 8;
  return definition;
}
