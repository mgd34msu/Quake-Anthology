/* Port of src/content/q2/rerelease/monsters/base-variants/makron.ts.
 * Rerelease m_boss32.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/base_variants/makron.h"

#include <stdlib.h>

#include "qa/content/q2/base/monsters/boss_common.h"
#include "qa/content/q2/base/monsters/makron.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/foundation/monsters/perception.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/boss32.h"
#include "../rrm_internal.h"

static void torso_think_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  (void)ctx;
  if (++entity->frame >= 365) entity->frame = 346;
  qa_vec3 angles = q2rm_body(game, entity).angles;
  if (angles.x > 0) {
    angles.x = fmax(0, angles.x - 15);
    qa_q2f_body_changes change;
    memset(&change, 0, sizeof change);
    change.has_angles = true; change.angles = angles;
    q2rm_move_body(game, entity, change, false, false);
  }
  game->show(game->ctx, entity);
  qa_q2f_think self = {NULL, torso_think_fn};
  game->schedule(game->ctx, entity, 0.1, self);
}
static qa_q2f_think torso_think(void) {
  qa_q2f_think think = {NULL, torso_think_fn};
  return think;
}
static void spawn_torso(qa_q2m_context *c) {
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_gib_options plain = {false, false, false, false};
  qa_q2f_entity *torso = qa_q2m_throw_gib(entity, game, "models/monsters/boss3/rider/tris.md2", 0, plain);
  if (!torso) return;
  qa_body_state body = q2rm_body(game, entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE);
  double height = body.bounds.max.z - q2rm_body(game, torso).bounds.max.z;
  torso->frame = 346;
  torso->skin = 1;
  torso->effects = 2;
  torso->angular_velocity = q2rm_zero();
  torso->sound_text = "makron/spine.wav";
  qa_vec3 raised = body.origin;
  raised.z += height - 15;
  qa_vec3 origin = qa_add3_double(raised, qa_scale3_double(axes.forward, -10));
  qa_vec3 angles = body.angles;
  angles.x = 90;
  qa_vec3 velocity = qa_add3_double(q2rm_body(game, torso).velocity,
      qa_add3_double(qa_scale3_double(axes.up, 120), qa_scale3_double(axes.forward, -120)));
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = origin;
  change.has_angles = true; change.angles = angles;
  change.has_velocity = true; change.velocity = velocity;
  q2rm_move_body(game, torso, change, false, false);
  game->motion(game->ctx, torso, QA_Q2F_MOTION_TOSS);
  game->show(game->ctx, torso);
  q2rm_emit_loop(game, torso, torso->sound_text, 0, "start");
  game->schedule(game->ctx, torso, 0.1, torso_think());
}
qa_actor_id qa_q2rm_toss_rerelease_makron(qa_q2m_context *context, qa_q2m_monsters *monsters) {
  qa_q2f_entity *entity = context->entity;
  qa_q2f_game *game = context->game;
  qa_q2f_entity *child = game->create(game->ctx, "monster_makron", NULL);
  child->target = entity->target;
  child->enemy = entity->enemy;
  child->has_enemy = entity->has_enemy;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = q2rm_body(game, entity).origin;
  q2rm_move_body(game, child, change, true, false);
  if (!qa_q2m_monsters_spawn(monsters, child, game)) qa_q2f_error("Makron must be registered before Jorg");
  if (child->think.fn) child->think.fn(child->think.ctx, child, game);
  bool has_enemy = child->has_enemy && qa_q2m_health(game, &child->enemy) > 0;
  qa_actor_id enemy;
  bool have_enemy;
  if (has_enemy) { enemy = child->enemy; have_enemy = true; }
  else have_enemy = qa_q2m_monsters_current_sight_client(monsters, &enemy);
  qa_body_state_result target;
  memset(&target, 0, sizeof target);
  if (have_enemy) target = game->host->bodies.read(game->host->bodies.ctx, enemy);
  qa_q2m_context *resumed = qa_q2m_monsters_context(monsters, child->actor.id);
  if (!target.has_value || !resumed) return child->actor.id;
  qa_body_state body = q2rm_body(game, child);
  qa_vec3 difference = qa_sub3_double(target.value.origin, body.origin);
  qa_vec3 angles = body.angles;
  angles.y = qa_vector_to_angles_with_rule(difference, QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  qa_vec3 velocity = qa_scale3_double(qa_normalize3_double_hypot(difference), 400);
  velocity.z = 200;
  memset(&change, 0, sizeof change);
  change.has_angles = true; change.angles = angles;
  change.has_velocity = true; change.velocity = velocity;
  change.has_ground = true; change.ground = NULL;
  q2rm_move_body(game, child, change, false, false);
  child->enemy = enemy;
  child->has_enemy = true;
  qa_q2m_monsters_found_target(monsters, resumed);
  q2rm_set_move(resumed, "makron_move_sight");
  child->frame = qa_q2rm_boss32_frames_value.active01;
  resumed->state->next_frame = qa_q2rm_boss32_frames_value.active01;
  return child->actor.id;
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->ignore_shots = true;
}
static bool check_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_attack_chance_profile profile = {0.4, 0.8, 0.4, 0.2, 0, 0};
  return qa_q2m_default_check_attack(c, &profile);
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2 ? 1 : 0;
  if (strcmp(state->move->name, "makron_move_sight") == 0 || q2rm_now(game) < state->pain_time
      || (!qa_q2rm_chainfist(c) && reaction.damage <= 25 && q2rm_rnd(game) < 0.2)) return;
  state->pain_time = q2rm_now(game) + 3;
  bool heavy = false;
  if (reaction.damage <= 40) q2rm_sound(game, entity, "makron/pain3.wav", true, 2, true, 1, true, 0);
  else if (reaction.damage <= 110) q2rm_sound(game, entity, "makron/pain2.wav", true, 2, true, 1, true, 0);
  else if (q2rm_rnd(game) <= (reaction.damage <= 150 ? 0.45 : 0.35)) {
    heavy = true;
    q2rm_sound(game, entity, "makron/pain1.wav", true, 2, true, 1, true, 0);
  }
  if (!qa_q2rm_reacts_to_pain(c)) return;
  if (reaction.damage <= 40) q2rm_set_move(c, "makron_move_pain4");
  else if (reaction.damage <= 110) q2rm_set_move(c, "makron_move_pain5");
  else if (heavy) q2rm_set_move(c, "makron_move_pain6");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  double damage = reaction.pain.damage;
  qa_q2bm_stop_loop(c);
  entity->sound_text = "";
  if (qa_q2rm_check_gib(c)) {
    q2rm_snd(c, "misc/udeath.wav", 2);
    qa_q2m_gib_options plain = {false, false, false, false};
    qa_q2m_gib_options metal = {false, true, false, false};
    qa_q2m_gib_options metal_head = {true, true, false, false};
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
    for (int i = 0; i < 4; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_metal/tris.md2", damage, metal);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/gear/tris.md2", damage, metal_head);
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  q2rm_sound(game, entity, "makron/death.wav", true, 2, true, 1, true, 0);
  state->dead = true; state->can_take_damage = true;
  entity->server_flags = q2rm_or(entity->server_flags, 2);
  q2rm_set_damageable(game, entity, true);
  q2rm_set_move(c, "makron_move_death2");
  spawn_torso(c);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2rm_v3(-60, -60, 0);
  change.bounds.max = q2rm_v3(60, 60, 48);
  q2rm_move_body(game, entity, change, false, false);
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_corpse(c);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2rm_v3(-60, -60, 0);
  change.bounds.max = q2rm_v3(60, 60, 24);
  q2rm_move_body(c->game, c->entity, change, false, false);
}
static void spawn_torso_fn(void *ctx, qa_q2m_context *c) { (void)ctx; spawn_torso(c); }
static void save_loc_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 eye;
  if (qa_q2m_enemy_eye(c, &eye)) c->entity->pos1 = eye;
}
static void railgun_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, 119));
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(c->entity->pos1, start));
  c->weapons.fire_rail(c->weapons.ctx, c->entity, c->game, start, direction, 50, 100);
  qa_q2rm_monster_flash(c, 119, start, direction);
}
static void hyper_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double frame = c->entity->frame;
  const qa_q2rm_boss32_frames *f = &qa_q2rm_boss32_frames_value;
  double flash = 102 + frame - f->attak405;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, flash));
  qa_vec3 eye;
  bool have_eye = qa_q2m_enemy_eye(c, &eye);
  double yaw = q2rm_body(c->game, c->entity).angles.y;
  qa_vec3 angles = {
    have_eye ? qa_vector_to_angles_with_rule(qa_sub3_double(eye, start), QA_VECTOR_TO_ANGLES_NATIVE_AI).x : 0,
    frame <= f->attak413 ? yaw - 10 * (frame - f->attak413) : yaw + 10 * (frame - f->attak421), 0};
  qa_vec3 direction = qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_NATIVE).forward;
  (void)c->weapons.fire_blaster(c->weapons.ctx, c->entity, c->game, start, direction, 15, 1000, 8, false, false, 0);
  qa_q2rm_monster_flash(c, flash, start, direction);
}
static const qa_q2m_named_handler extra_callbacks[] = {
  {"makron_dead", {NULL, dead_fn}},
  {"makron_spawn_torso", {NULL, spawn_torso_fn}},
  {"MakronSaveloc", {NULL, save_loc_fn}},
  {"MakronRailgun", {NULL, railgun_fn}},
  {"MakronHyperblaster", {NULL, hyper_fn}},
};
static qa_q2m_definition makron_definition;
static bool makron_ready;
const qa_q2m_definition *qa_q2rm_get_rerelease_makron_definition(void) {
  if (makron_ready) return &makron_definition;
  makron_definition = qa_q2bm_makron_definition;
  makron_definition.moves = qa_q2rm_boss32_moves;
  makron_definition.move_count = qa_q2rm_boss32_move_count;
  qa_q2f_named_think *think_defs = malloc(sizeof *think_defs);
  if (!think_defs) qa_q2f_error("Out of memory creating rerelease makron definition");
  think_defs[0].name = "rerelease.makron.makron_torso_think";
  think_defs[0].cb = torso_think();
  makron_definition.has_source_callbacks = true;
  memset(&makron_definition.source_callbacks, 0, sizeof makron_definition.source_callbacks);
  makron_definition.source_callbacks.think = think_defs;
  makron_definition.source_callbacks.think_count = 1;
  makron_definition.initialize = (qa_q2m_handler){NULL, init_fn};
  makron_definition.check_attack_ctx = NULL;
  makron_definition.check_attack = check_attack_fn;
  makron_definition.reactions.pain = pain_fn;
  makron_definition.reactions.die = die_fn;
  makron_definition.callbacks = qa_q2rm_merge_callbacks(
      makron_definition.callbacks, makron_definition.callback_count,
      extra_callbacks, sizeof extra_callbacks / sizeof extra_callbacks[0], &makron_definition.callback_count);
  makron_ready = true;
  return &makron_definition;
}
