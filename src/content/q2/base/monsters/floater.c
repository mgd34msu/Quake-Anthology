/* Port of src/content/q2/base/monsters/floater.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/floater.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "base_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/float.h"
#include "qa/content/q2/foundation/monsters/ai.h"

static void stand_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, q2bm_rnd(c->game) <= 0.5 ? "floater_move_stand1" : "floater_move_stand2");
}
static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->state->stand_ground ? "floater_move_stand1" : "floater_move_run");
}
static void melee_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, q2bm_rnd(c->game) < 0.5 ? "floater_move_attack3" : "floater_move_attack2");
}
static const qa_q2bm_sound_spec sight_spec = {"floater/fltsght1.wav", 2, 1};
static const qa_q2bm_sound_spec idle_spec = {"floater/fltidle1.wav", 2, 2};

/* TS: floatMoves.filter(name !== "floater_move_activate"), built once. */
static qa_q2m_move *active_moves;
static size_t active_move_count;
static void build_active_moves(void) {
  if (active_moves) return;
  active_moves = malloc(qa_q2bm_float_move_count * sizeof *active_moves);
  if (!active_moves) qa_raise("Error", 0, 0, "Floater move table allocation failed");
  for (size_t i = 0; i < qa_q2bm_float_move_count; i++)
    if (strcmp(qa_q2bm_float_moves[i].name, "floater_move_activate") != 0)
      active_moves[active_move_count++] = qa_q2bm_float_moves[i];
}
static void initialize_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2bm_loop_sound(c, "floater/fltsrch1.wav");
  stand_fn(NULL, c);
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2bm_damaged_skin(c);
  if (q2bm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  if (c->game->options.skill == 3) return;
  bool first = ((int)(floor(q2bm_rnd(c->game) * 3) + 1) % 3) == 0;
  q2bm_sound(c->game, c->entity, first ? "floater/fltpain1.wav" : "floater/fltpain2.wav",
             true, 2, false, 0, false, 0);
  q2bm_set_move(c, first ? "floater_move_pain1" : "floater_move_pain2");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2bm_explode(c, "floater/fltdeth1.wav");
}
static void fire_blaster_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, 82, &aim)) return;
  double effects = c->entity->frame == qa_q2bm_float_frames_value.attak104
                || c->entity->frame == qa_q2bm_float_frames_value.attak107 ? 64 : 0;
  (void)c->weapons.fire_blaster(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 1, 1000, effects,
                                false, false, 0);
  qa_q2bm_muzzle(c, 82, aim.direction, aim.start);
}
static void wham_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_sound(c->game, c->entity, "floater/fltatck3.wav", true, 1, false, 0, false, 0);
  (void)c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, q2bm_v3(80, 0, 0),
                            5 + floor(q2bm_rnd(c->game) * 6), -50);
}
static void zap_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy) || !entity->has_enemy) return;
  qa_vec3 direction = qa_sub3_double(enemy.origin, q2bm_body(game, entity).origin);
  qa_vec3 origin = qa_q2m_project_flash_default(c, q2bm_v3(18.5, -0.9, 10));
  q2bm_sound(game, entity, "floater/fltatck2.wav", true, 1, false, 0, false, 0);
  q2bm_emit_effect(game, "q2:splash", origin, direction, 32, 1);
  (void)game->damage(game->ctx, entity->enemy, entity, entity->actor.id, &entity->actor.id, 5 + floor(q2bm_rnd(game) * 6), -10, direction, enemy.origin, q2bm_zero(), 0, 4, NULL);
}
#define CB(name, fn) {name, {NULL, fn}}
static const qa_q2m_named_handler floater_callbacks[] = {
  CB("floater_run", run_fn), CB("floater_dead", qa_q2bm_finish_corpse_handler),
  CB("floater_fire_blaster", fire_blaster_fn), CB("floater_wham", wham_fn), CB("floater_zap", zap_fn),
};
/* The filtered move table is built by the definition accessor below;
 * the exported definition is filled on first use through its moves. */
static qa_q2m_definition floater_definition_storage;
static bool floater_built;
const qa_q2m_definition *qa_q2bm_get_floater_definition(void) {
  if (!floater_built) {
    build_active_moves();
    qa_q2m_definition d = {
      .classname = "monster_floater", .kind = "floater", .model = "models/monsters/float/tris.md2",
      .health = 200, .gib_health = -80, .mass = 300, .bounds = {{-24, -24, -24}, {24, 24, 32}}, .scale = 1,
      .has_locomotion = true, .locomotion = QA_Q2M_LOCO_FLY,
      .initial_move = "floater_move_stand1",
      .moves = active_moves, .move_count = active_move_count,
      .callbacks = floater_callbacks, .callback_count = sizeof floater_callbacks / sizeof floater_callbacks[0],
      .stand = {NULL, stand_fn},
      .walk = {(void *)"floater_move_walk", qa_q2bm_move_handler_fn},
      .run = {NULL, run_fn},
      .attack = {(void *)"floater_move_attack1", qa_q2bm_move_handler_fn},
      .melee = {NULL, melee_fn},
      .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
      .idle = {(void *)&idle_spec, qa_q2bm_sound_handler_fn},
      .initialize = {NULL, initialize_fn},
      .reactions = {NULL, pain_fn, die_fn},
    };
    floater_definition_storage = d;
    floater_built = true;
  }
  return &floater_definition_storage;
}
const qa_q2m_move *qa_q2bm_floater_active_moves(size_t *out_count) {
  build_active_moves();
  *out_count = active_move_count;
  return active_moves;
}
