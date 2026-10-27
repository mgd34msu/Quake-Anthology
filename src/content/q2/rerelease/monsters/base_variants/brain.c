/* Port of src/content/q2/rerelease/monsters/base-variants/brain.ts.
 * Rerelease m_brain.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/base_variants/brain.h"

#include <stdlib.h>

#include "qa/content/q2/base/monsters/brain.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/missionpacks/monsters/power_armor.h"
#include "qa/content/q2/missionpacks/monsters/xatrix_variants.h"
#include "qa/content/q2/rerelease/monsters/beam.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/brain.h"
#include "../rrm_internal.h"

static void screen(qa_q2m_context *c, bool active) {
  qa_q2f_entity *entity = c->entity;
  double cells = c->game->host->inventory.count(c->game->host->inventory.ctx, entity->actor.id, "q2:monster-power");
  qa_q2rm_set_powered(c->game, entity->actor, active ? "screen" : "none", active ? cells : 0);
}
static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  screen(c, true);
  q2rm_set_move(c, c->state->stand_ground ? "brain_move_stand" : "brain_move_run");
}
static bool tongue_allowed(qa_vec3 start, qa_vec3 end) {
  qa_vec3 direction = qa_sub3_double(start, end);
  if (qa_hypot3(direction) > 512) return false;
  double pitch = qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_NATIVE_AI).x;
  if (pitch < -180) pitch += 360;
  return fabs(pitch) <= 30;
}
static void hit(qa_q2m_context *c, bool right) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_bounds bounds = q2rm_body(game, entity).bounds;
  qa_vec3 aim = {80, right ? bounds.max.x : bounds.min.x, 8};
  if (c->weapons.fire_hit(c->weapons.ctx, entity, game, aim, 15 + floor(q2rm_rnd(game) * 5), 40))
    q2rm_snd(c, "brain/melee3.wav", 1);
  else c->state->melee_time = q2rm_now(game) + 3;
}
typedef struct eye_ctx {
  qa_q2m_monsters *monsters;
  const qa_vec3 *positions;
  bool update;
} eye_ctx;
static void eye_fn(void *ctx, qa_q2f_entity *beam, qa_q2f_game *game) {
  eye_ctx *eye = ctx;
  qa_q2m_context *context = beam->has_owner ? qa_q2m_monsters_context(eye->monsters, beam->owner) : NULL;
  if (!context) { qa_q2rm_free_monster_beam_fn(NULL, beam, game); return; }
  double index = context->entity->frame - qa_q2rm_brain_frames_value.walk101;
  if (index < 0 || index >= 11 || index != trunc(index)) { qa_q2rm_free_monster_beam_fn(NULL, beam, game); return; }
  qa_vec3 position = eye->positions[(size_t)index];
  qa_body_state body = q2rm_body(game, context->entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  qa_vec3 start = qa_add3_double(qa_add3_double(qa_add3_double(body.origin, qa_scale3_double(axes.right, position.x)),
                                                qa_scale3_double(axes.forward, position.y)),
                                 qa_scale3_double(axes.up, position.z));
  qa_vec3 direction;
  if (!qa_q2rm_predicted_direction(context, start, 0, false, 0.1 + q2rm_rnd(game) * 0.1, &direction)) return;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = start;
  q2rm_move_body(game, beam, change, false, false);
  beam->movedir = direction;
  if (eye->update) qa_q2rm_update_monster_beam(beam, game, false);
}
static void sight_fn(void *ctx, qa_q2m_context *c) { (void)ctx; q2rm_snd(c, "brain/brnsght1.wav", 2); }
static void search_fn(void *ctx, qa_q2m_context *c) { (void)ctx; q2rm_snd(c, "brain/brnsrch1.wav", 2); }
static void idle_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_snd4(c, "brain/brnlens1.wav", 0, 1, 2);
  q2rm_set_move(c, "brain_move_idle");
}
static void melee_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_set_move(c, q2rm_rnd(c->game) <= 0.5 ? "brain_move_attack1" : "brain_move_attack2");
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double type = qa_q2f_number_field(&c->entity->spawn, "power_armor_type", 1);
  double cells = qa_q2f_number_field(&c->entity->spawn, "power_armor_power", 100);
  qa_q2mpm_monster_power_armor(c, type == 2 ? "shield" : "screen", cells);
  if (type == 0) screen(c, false);
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2m_target_distance(c) <= 440) {
    if (q2rm_rnd(c->game) < 0.5) { q2rm_set_move(c, "brain_move_attack3"); return; }
    if (!q2rm_has(c->entity->spawnflags, 8)) { q2rm_set_move(c, "brain_move_attack4"); return; }
  } else if (!q2rm_has(c->entity->spawnflags, 8)) { q2rm_set_move(c, "brain_move_attack4"); return; }
}
static bool duck_fn(void *ctx, qa_q2m_context *c, double eta) {
  (void)ctx; (void)eta;
  q2rm_set_move(c, "brain_move_duck");
  return true;
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2 ? 1 : 0;
  if (q2rm_now(game) < state->pain_time) return;
  state->pain_time = q2rm_now(game) + 3;
  double choice = q2rm_rnd(game);
  q2rm_snd(c, choice < 0.33 || choice >= 0.66 ? "brain/brnpain1.wav" : "brain/brnpain2.wav", 2);
  if (!qa_q2rm_reacts_to_pain(c)) return;
  q2rm_set_move(c, choice < 0.33 ? "brain_move_pain1" : choice < 0.66 ? "brain_move_pain2" : "brain_move_pain3");
  if (state->ducked) qa_q2m_set_duck(c, false);
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  double damage = reaction.pain.damage;
  entity->effects = 0;
  screen(c, false);
  if (qa_q2rm_check_gib(c)) {
    q2rm_snd(c, "misc/udeath.wav", 2);
    entity->skin = trunc(entity->skin / 2);
    qa_q2f_entity *first = game->entity(game->ctx, entity->has_beam ? &entity->beam : NULL);
    qa_q2f_entity *second = game->entity(game->ctx, entity->has_beam2 ? &entity->beam2 : NULL);
    if (first) qa_q2rm_free_monster_beam_fn(NULL, first, game);
    if (second) qa_q2rm_free_monster_beam_fn(NULL, second, game);
    qa_q2m_gib_options plain = {false, false, false, false};
    qa_q2m_gib_options skin_up = {false, false, true, true};
    qa_q2m_gib_options skinned = {false, false, true, false};
    qa_q2m_gib_options head = {true, false, true, false};
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", damage, plain);
    for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
    static const char *const ups[5] = {
      "models/monsters/brain/gibs/arm.md2", "models/monsters/brain/gibs/arm.md2",
      "models/monsters/brain/gibs/boot.md2", "models/monsters/brain/gibs/door.md2",
      "models/monsters/brain/gibs/door.md2"};
    for (int i = 0; i < 5; i++) (void)qa_q2m_throw_gib(entity, game, ups[i], damage, skin_up);
    static const char *const skins[2] = {"models/monsters/brain/gibs/pelvis.md2", "models/monsters/brain/gibs/chest.md2"};
    for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, skins[i], damage, skinned);
    (void)qa_q2m_throw_gib(entity, game, "models/monsters/brain/gibs/head.md2", damage, head);
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  q2rm_snd(c, "brain/brndeth1.wav", 2);
  state->dead = true; state->can_take_damage = true;
  q2rm_set_damageable(game, entity, true);
  q2rm_set_move(c, q2rm_rnd(game) <= 0.5 ? "brain_move_death1" : "brain_move_death2");
}
static void corpse_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_corpse(c); }
static void hit_right_fn(void *ctx, qa_q2m_context *c) { (void)ctx; hit(c, true); }
static void hit_left_fn(void *ctx, qa_q2m_context *c) { (void)ctx; hit(c, false); }
static void chest_open_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->count = 0;
  screen(c, false);
  q2rm_snd(c, "brain/brnatck1.wav", 4);
}
static void tentacle_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, q2rm_v3(80, 0, 8),
                          10 + floor(q2rm_rnd(c->game) * 5), -600))
    c->entity->count = 1;
  else c->state->melee_time = q2rm_now(c->game) + 3;
  q2rm_snd(c, "brain/brnatck3.wav", 1);
}
static void chest_closed_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  screen(c, true);
  if (c->entity->count != 0) {
    c->entity->count = 0;
    q2rm_set_move(c, "brain_move_attack1");
  }
}
static void tongue_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy) || !entity->has_enemy) return;
  qa_vec3 start = qa_q2m_project_flash_default(c, q2rm_v3(24, 0, 16));
  qa_vec3 ends[3];
  ends[0] = enemy.origin;
  ends[1] = enemy.origin; ends[1].z += enemy.bounds.max.z - 8;
  ends[2] = enemy.origin; ends[2].z += enemy.bounds.min.z + 8;
  bool allowed = false;
  for (int i = 0; i < 3; i++) if (tongue_allowed(start, ends[i])) allowed = true;
  if (!allowed) return;
  qa_vec3 end = enemy.origin;
  qa_trace_result trace = q2rm_trace(game, start, end, NULL, &entity->actor.id, (double)0x46004003u);
  if (trace.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(trace.hit.actor, entity->enemy)) return;
  q2rm_snd(c, "brain/brnatck3.wav", 1);
  qa_q2bm_extended_event beam;
  memset(&beam, 0, sizeof beam);
  beam.kind = "monster-beam";
  beam.effect = "parasite";
  beam.actor = entity->actor.id;
  beam.start = start; beam.end = end;
  qa_q2bm_emit_extended(beam);
  (void)q2rm_damage(game, entity->enemy, entity, &entity->actor.id, 5, 0, qa_sub3_double(start, end), end,
                    q2rm_zero(), 36, true, 8);
  qa_body_state body = q2rm_body(game, entity);
  qa_q2f_owned_result target = game->host->actors.resolve_owned(game->host->actors.ctx, entity->enemy);
  qa_vec3 origin = body.origin;
  origin.z += 1;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = origin;
  q2rm_move_body(game, entity, change, false, false);
  if (target.has_value) {
    qa_body_state written = enemy;
    written.velocity = qa_scale3_double(qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward, -1200);
    game->host->bodies.write(game->host->bodies.ctx, target.value, written);
  }
}
typedef struct brain_ctx {
  qa_q2f_think right_eye, left_eye;
} brain_ctx;
static void laserbeam_fn(void *ctx, qa_q2m_context *c) {
  brain_ctx *brain = ctx;
  qa_q2rm_fire_monster_beam(c, 1, false, brain->right_eye);
  qa_q2rm_fire_monster_beam(c, 1, true, brain->left_eye);
}
static void laserbeam_reattack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2rm_rnd(c->game) < 0.5 && qa_q2m_visible_enemy(c)
      && qa_q2m_health(c->game, c->entity->has_enemy ? &c->entity->enemy : NULL) > 0)
    c->entity->frame = qa_q2rm_brain_frames_value.walk101;
}
static void shrink_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = q2rm_body(c->game, c->entity).bounds;
  c->entity->server_flags = q2rm_or(c->entity->server_flags, 2);
  bounds.max.z = 0;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true; change.bounds = bounds;
  q2rm_move_body(c->game, c->entity, change, false, false);
}

qa_q2m_definition qa_q2rm_create_rerelease_brain_definition(qa_q2m_monsters *monsters) {
  eye_ctx *right = malloc(sizeof *right);
  eye_ctx *left = malloc(sizeof *left);
  brain_ctx *brain = malloc(sizeof *brain);
  qa_q2f_named_think *think_defs = malloc(3 * sizeof *think_defs);
  if (!right || !left || !brain || !think_defs) qa_q2f_error("Out of memory creating rerelease brain definition");
  right->monsters = monsters; right->positions = qa_q2mpm_xatrix_brain_right_eye; right->update = false;
  left->monsters = monsters; left->positions = qa_q2mpm_xatrix_brain_left_eye; left->update = true;
  brain->right_eye = (qa_q2f_think){right, eye_fn};
  brain->left_eye = (qa_q2f_think){left, eye_fn};
  think_defs[0].name = "rerelease.brain.right_eye_update";
  think_defs[0].cb = brain->right_eye;
  think_defs[1].name = "rerelease.brain.left_eye_update";
  think_defs[1].cb = brain->left_eye;
  think_defs[2].name = "beam_think";
  think_defs[2].cb = qa_q2rm_free_monster_beam();
  const qa_q2m_named_handler extra[] = {
    {"brain_run", {NULL, run_fn}},
    {"brain_dead", {NULL, corpse_fn}},
    {"brain_hit_right", {NULL, hit_right_fn}},
    {"brain_hit_left", {NULL, hit_left_fn}},
    {"brain_chest_open", {NULL, chest_open_fn}},
    {"brain_tentacle_attack", {NULL, tentacle_fn}},
    {"brain_chest_closed", {NULL, chest_closed_fn}},
    {"brain_tounge_attack", {NULL, tongue_fn}},
    {"brain_laserbeam", {brain, laserbeam_fn}},
    {"brain_laserbeam_reattack", {NULL, laserbeam_reattack_fn}},
    {"brain_shrink", {NULL, shrink_fn}},
  };
  const qa_q2m_definition *base = &qa_q2bm_brain_definition;
  /* TS builds this definition literally from base fields. */
  qa_q2m_definition definition;
  memset(&definition, 0, sizeof definition);
  definition.classname = base->classname;
  definition.kind = base->kind;
  definition.model = base->model;
  definition.health = 300; definition.gib_health = -150; definition.mass = 400;
  definition.bounds = base->bounds;
  definition.scale = 1;
  definition.initial_move = base->initial_move;
  definition.moves = qa_q2rm_brain_moves;
  definition.move_count = qa_q2rm_brain_move_count;
  definition.stand = base->stand;
  definition.walk = base->walk;
  definition.run = (qa_q2m_handler){NULL, run_fn};
  definition.attack = (qa_q2m_handler){NULL, attack_fn};
  definition.sight = (qa_q2m_handler){NULL, sight_fn};
  definition.search = (qa_q2m_handler){NULL, search_fn};
  definition.idle = (qa_q2m_handler){NULL, idle_fn};
  definition.melee = (qa_q2m_handler){NULL, melee_fn};
  definition.has_ranged_attack_flag = true;
  definition.has_ranged_attack = true;
  definition.has_source_callbacks = true;
  definition.source_callbacks.think = think_defs;
  definition.source_callbacks.think_count = 3;
  definition.initialize = (qa_q2m_handler){NULL, init_fn};
  definition.restore = (qa_q2m_handler){NULL, qa_q2mpm_restore_monster_power_armor_handler};
  definition.duck_ctx = NULL; definition.duck = duck_fn;
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.callbacks = qa_q2rm_merge_callbacks(base->callbacks, base->callback_count,
                                                 extra, sizeof extra / sizeof extra[0], &definition.callback_count);
  return definition;
}
