/* Port of src/content/q2/rerelease/monsters/base-variants/floater.ts.
 * Rerelease m_float.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/base_variants/floater.h"

#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/floater.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/float.h"
#include "../rrm_internal.h"

static void stand_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_set_move(c, strcmp(c->state->move->name, "floater_move_disguise") == 0 ? "floater_move_disguise"
      : q2rm_rnd(c->game) <= 0.5 ? "floater_move_stand1" : "floater_move_stand2");
}
static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_set_move(c, strcmp(c->state->move->name, "floater_move_disguise") == 0 ? "floater_move_pop"
      : c->state->stand_ground ? "floater_move_stand1" : "floater_move_run");
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_alternate_fly_state *alt = &c->state->alt;
  alt->alternate_fly = true;
  alt->fly_thrusters = false;
  alt->fly_acceleration = 10;
  alt->fly_speed = 100;
  alt->fly_min_distance = 20;
  alt->fly_max_distance = 200;
  qa_q2bm_loop_sound(c, "floater/fltsrch1.wav");
  if (q2rm_has(c->entity->spawnflags, 8)) { q2rm_set_move(c, "floater_move_disguise"); return; }
  stand_fn(NULL, c);
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2rm_rnd(c->game) > 0.5) {
    c->state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
    q2rm_set_move(c, "floater_move_attack1");
    return;
  }
  if (q2rm_rnd(c->game) <= 0.5) c->state->lefty = !c->state->lefty;
  c->state->attack_state = QA_Q2M_ATTACK_SLIDING;
  q2rm_set_move(c, "floater_move_attack1a");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2 ? 1 : 0;
  if (q2rm_now(game) < state->pain_time || strcmp(state->move->name, "floater_move_disguise") == 0
      || strcmp(state->move->name, "floater_move_pop") == 0) return;
  bool first = qa_q2_rerelease_random_integer_max(qa_q2rm_rerelease_random(c), 3) == 0;
  q2rm_snd(c, first ? "floater/fltpain1.wav" : "floater/fltpain2.wav", 2);
  state->pain_time = q2rm_now(game) + 3;
  if (!qa_q2rm_reacts_to_pain(c)) return;
  q2rm_set_move(c, first ? "floater_move_pain1" : "floater_move_pain2");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  q2rm_snd(c, "floater/fltdeth1.wav", 2);
  q2rm_emit_effect(game, "q2:explosion1", q2rm_body(game, entity).origin, q2rm_zero(), 1, 0);
  entity->skin = trunc(entity->skin / 2);
  qa_q2m_gib_options plain = {false, false, false, false};
  qa_q2m_gib_options skinned = {false, false, true, false};
  qa_q2m_gib_options head = {true, false, true, false};
  for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_metal/tris.md2", 55, plain);
  for (int i = 0; i < 3; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", 55, plain);
  static const char *const models[3] = {
    "models/monsters/float/gibs/piece.md2", "models/monsters/float/gibs/gun.md2",
    "models/monsters/float/gibs/base.md2"};
  for (int i = 0; i < 3; i++) (void)qa_q2m_throw_gib(entity, game, models[i], 55, skinned);
  (void)qa_q2m_throw_gib(entity, game, "models/monsters/float/gibs/jar.md2", 55, head);
  state->dead = true; state->gibbed = true;
}
static void fire_blaster_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, 82, &aim)) return;
  double effects = fmod(c->entity->frame, 4) == 0 ? 64 : 0;
  (void)c->weapons.fire_blaster(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 1, 1000, effects, false, false, 0);
  qa_q2rm_monster_flash(c, 82, aim.start, aim.direction);
}
static void wham_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_snd(c, "floater/fltatck3.wav", 1);
  double damage = qa_q2_rerelease_random_integer_range(qa_q2rm_rerelease_random(c), 5, 11);
  if (!c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, q2rm_v3(80, 0, 0), damage, -50))
    c->state->melee_time = q2rm_now(c->game) + 3;
}
static void zap_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy) || !entity->has_enemy) return;
  qa_vec3 direction = qa_sub3_double(enemy.origin, q2rm_body(game, entity).origin);
  qa_vec3 origin = qa_q2m_project_flash_default(c, q2rm_v3(18.5, -0.9, 10));
  q2rm_snd(c, "floater/fltatck2.wav", 1);
  q2rm_emit_effect(game, "q2:splash", origin, direction, 32, 1);
  double damage = qa_q2_rerelease_random_integer_range(qa_q2rm_rerelease_random(c), 5, 11);
  (void)q2rm_damage(game, entity->enemy, entity, &entity->actor.id, damage, -10, direction,
                    enemy.origin, q2rm_zero(), 0, true, 4);
}
static const qa_q2m_named_handler extra_callbacks[] = {
  {"floater_run", {NULL, run_fn}},
  {"floater_fire_blaster", {NULL, fire_blaster_fn}},
  {"floater_wham", {NULL, wham_fn}},
  {"floater_zap", {NULL, zap_fn}},
};
static qa_q2m_definition floater_definition;
static bool floater_ready;
const qa_q2m_definition *qa_q2rm_get_rerelease_floater_definition(void) {
  if (floater_ready) return &floater_definition;
  floater_definition = *qa_q2bm_get_floater_definition();
  floater_definition.moves = qa_q2rm_float_moves;
  floater_definition.move_count = qa_q2rm_float_move_count;
  floater_definition.bounds.min = q2rm_v3(-24, -24, -24);
  floater_definition.bounds.max = q2rm_v3(24, 24, 48);
  floater_definition.stand = (qa_q2m_handler){NULL, stand_fn};
  floater_definition.run = (qa_q2m_handler){NULL, run_fn};
  floater_definition.initialize = (qa_q2m_handler){NULL, init_fn};
  floater_definition.attack = (qa_q2m_handler){NULL, attack_fn};
  floater_definition.reactions.pain = pain_fn;
  floater_definition.reactions.die = die_fn;
  floater_definition.callbacks = qa_q2rm_merge_callbacks(NULL, 
      floater_definition.callbacks, floater_definition.callback_count,
      extra_callbacks, sizeof extra_callbacks / sizeof extra_callbacks[0], &floater_definition.callback_count);
  floater_ready = true;
  return &floater_definition;
}
