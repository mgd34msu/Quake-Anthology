/* Port of src/content/q2/base/monsters/makron.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/makron.h"

#include "base_internal.h"
#include "qa/content/q2/base/monsters/boss_common.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/boss32.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->state->stand_ground ? "makron_move_stand" : "makron_move_run");
}
/* TS WeakMap-cached spawn think per monsters: ctx is the monsters pointer. */
static void makron_spawn_fn(void *ctx, qa_q2f_entity *self, qa_q2f_game *services) {
  qa_q2m_monsters *monsters = ctx;
  if (!qa_q2m_monsters_spawn(monsters, self, services))
    qa_q2f_error("Makron definition must be registered before Jorg");
  qa_actor_id player;
  if (!qa_q2m_monsters_current_sight_client(monsters, &player)) return;
  qa_body_state_result target = services->host->bodies.read(services->host->bodies.ctx, player);
  if (!target.has_value) return;
  qa_body_state body = services->body(services->ctx, self->actor);
  qa_vec3 direction = qa_sub3_double(target.value.origin, body.origin);
  qa_vec3 velocity = qa_scale3_double(qa_normalize3_double_hypot(direction), 400);
  velocity.z = 200;
  qa_vec3 angles = body.angles;
  angles.y = qa_vector_to_angles_with_rule(direction, QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  qa_q2f_body_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_angles = true; changes.angles = angles;
  changes.has_velocity = true; changes.velocity = velocity;
  changes.has_ground = true; changes.ground = NULL;
  services->move(services->ctx, self, changes, false, false);
}
static qa_q2f_think makron_spawn_callback(qa_q2m_monsters *monsters) {
  qa_q2f_think think = {monsters, makron_spawn_fn};
  return think;
}
static void makron_torso_think_fn(void *ctx, qa_q2f_entity *self, qa_q2f_game *services) {
  (void)ctx;
  self->frame++;
  if (self->frame >= 365) self->frame = 346;
  services->show(services->ctx, self);
  qa_q2f_think think = {NULL, makron_torso_think_fn};
  services->schedule(services->ctx, self, 0.1, think);
}
void qa_q2bm_with_makron_spawn_callbacks(qa_q2m_definition *out, const qa_q2m_definition *definition,
                                         qa_q2m_monsters *monsters, qa_q2f_named_think *think_storage) {
  *out = *definition;
  size_t count = definition->has_source_callbacks ? definition->source_callbacks.think_count : 0;
  for (size_t i = 0; i < count; i++) think_storage[i] = definition->source_callbacks.think[i];
  think_storage[count].name = "q2:base/MakronSpawn";
  think_storage[count].cb = makron_spawn_callback(monsters);
  think_storage[count + 1].name = "q2:base/makron_torso_think";
  think_storage[count + 1].cb = (qa_q2f_think){NULL, makron_torso_think_fn};
  out->has_source_callbacks = true;
  out->source_callbacks.think = think_storage;
  out->source_callbacks.think_count = count + 2;
}
void qa_q2bm_makron_toss(qa_q2m_context *context, qa_q2m_monsters *monsters) {
  qa_q2f_entity *entity = context->entity;
  qa_q2f_game *game = context->game;
  qa_q2f_entity *child = game->create(game->ctx, "monster_makron", NULL);
  child->target = entity->target;
  qa_q2f_body_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_origin = true; changes.origin = q2bm_body(game, entity).origin;
  game->move(game->ctx, child, changes, true, false);
  game->schedule(game->ctx, child, 0.8, makron_spawn_callback(monsters));
}
static bool check_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  return qa_q2bm_boss_check_attack_default(c);
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double r = q2bm_rnd(c->game);
  q2bm_set_move(c, r <= 0.3 ? "makron_move_attack3" : r <= 0.6 ? "makron_move_attack4" : "makron_move_attack5");
}
static const qa_q2bm_sound_spec step_left_spec = {"makron/step1.wav", 4, 1};
static const qa_q2bm_sound_spec step_right_spec = {"makron/step2.wav", 4, 1};
static const qa_q2bm_sound_spec popup_spec = {"makron/popup.wav", 4, 0};
static const qa_q2bm_sound_spec hit_spec = {"makron/bhit.wav", 0, 0};
static const qa_q2bm_sound_spec brain_spec = {"makron/brain1.wav", 2, 1};
static const qa_q2bm_sound_spec prerail_spec = {"makron/rail_up.wav", 1, 1};

static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2bm_damaged_skin(c);
  if (q2bm_now(c->game) < c->state->pain_time
      || (reaction.damage <= 25 && q2bm_rnd(c->game) < 0.2))
    return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  if (c->game->options.skill == 3) return;
  const char *animation, *path;
  if (reaction.damage <= 40) { animation = "makron_move_pain4"; path = "makron/pain3.wav"; }
  else if (reaction.damage <= 110) { animation = "makron_move_pain5"; path = "makron/pain2.wav"; }
  else {
    if (reaction.damage > 150) return;
    if (q2bm_rnd(c->game) > 0.45 && q2bm_rnd(c->game) > 0.35) return;
    animation = "makron_move_pain6"; path = "makron/pain1.wav";
  }
  q2bm_sound(c->game, c->entity, path, true, 2, true, 1, true, 0);
  q2bm_set_move(c, animation);
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_q2m_state *state = c->state;
  double damage = reaction.pain.damage;
  qa_q2bm_stop_loop(c);
  if (qa_q2m_health(game, &entity->actor.id) <= state->gib_health) {
    q2bm_sound(game, entity, "misc/udeath.wav", true, 2, false, 0, false, 0);
    qa_q2m_gib_options plain = {.head = false, .metallic = false, .skinned = false, .upright = false};
    qa_q2m_gib_options metallic = {.head = false, .metallic = true, .skinned = false, .upright = false};
    qa_q2m_gib_options gear = {.head = true, .metallic = true, .skinned = false, .upright = false};
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
    for (int i = 0; i < 4; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_metal/tris.md2", damage, metallic);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/gear/tris.md2", damage, gear);
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  q2bm_sound(game, entity, "makron/death.wav", true, 2, true, 1, true, 0);
  state->dead = true; state->can_take_damage = true;
  q2bm_set_traits_damage(game, entity, true);
  qa_q2f_entity *torso = game->create(game->ctx, "makron_torso", NULL);
  qa_body_state body = q2bm_body(game, entity);
  torso->model = "models/monsters/boss3/rider/tris.md2";
  torso->frame = 346;
  qa_vec3 origin = body.origin; origin.y = body.origin.y - 84;
  qa_q2f_body_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_origin = true; changes.origin = origin;
  changes.has_angles = true; changes.angles = body.angles;
  changes.has_bounds = true; changes.bounds = (qa_bounds){{-8, -8, 0}, {8, 8, 8}};
  changes.has_velocity = true; changes.velocity = q2bm_zero();
  game->move(game->ctx, torso, changes, false, false);
  game->motion(game->ctx, torso, QA_Q2F_MOTION_STATIONARY);
  game->solid(game->ctx, torso, QA_Q2F_SOLID_NONE);
  game->show(game->ctx, torso);
  q2bm_emit_loop(game, torso, "makron/spine.wav", "start", 1);
  qa_q2f_think think = {NULL, makron_torso_think_fn};
  game->schedule(game->ctx, torso, 0.2, think);
  q2bm_set_move(c, "makron_move_death2");
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = {{-60, -60, 0}, {60, 60, 72}};
  qa_q2bm_finish_corpse_bounds(c, bounds);
}
static void taunt_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double r = q2bm_rnd(c->game);
  q2bm_sound(c->game, c->entity, r <= 0.3 ? "makron/voice4.wav" : r <= 0.6 ? "makron/voice3.wav" : "makron/voice.wav",
             true, 0, true, 1, true, 0);
}
static void bfg_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, 101, &aim)) return;
  q2bm_sound(c->game, c->entity, "makron/bfg_fire.wav", true, 2, false, 0, false, 0);
  (void)c->weapons.fire_bfg(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 50, 300, 300);
  qa_q2bm_muzzle(c, 101, aim.direction, aim.start);
}
static void saveloc_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 eye;
  if (qa_q2m_enemy_eye(c, &eye)) c->state->blind_fire_target = eye;
}
static void railgun_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(c->game->options.edition, 119));
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(c->state->blind_fire_target, start));
  c->weapons.fire_rail(c->weapons.ctx, c->entity, c->game, start, direction, 50, 100);
  qa_q2bm_muzzle(c, 119, direction, start);
}
static void hyperblaster_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double frame = c->entity->frame;
  double flash = 102 + frame - qa_q2bm_boss32_frames_value.attak405;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(c->game->options.edition, flash));
  qa_vec3 eye;
  bool has_eye = qa_q2m_enemy_eye(c, &eye);
  double yaw = q2bm_body(c->game, c->entity).angles.y;
  qa_vec3 aim_angles = {
    has_eye ? qa_vector_to_angles_with_rule(qa_sub3_double(eye, start), QA_VECTOR_TO_ANGLES_NATIVE_AI).x : 0,
    frame <= qa_q2bm_boss32_frames_value.attak413
        ? yaw - 10 * (frame - qa_q2bm_boss32_frames_value.attak413)
        : yaw + 10 * (frame - qa_q2bm_boss32_frames_value.attak421),
    0};
  qa_vec3 direction = qa_math_angle_vectors_rule(aim_angles, QA_ANGLE_RULE_NATIVE).forward;
  (void)c->weapons.fire_blaster(c->weapons.ctx, c->entity, c->game, start, direction, 15, 1000, 8,
                                false, false, 0);
  qa_q2bm_muzzle(c, 102, direction, start);
}
#define CB(name, fn) {name, {NULL, fn}}
static const qa_q2m_named_handler makron_callbacks[] = {
  CB("makron_run", run_fn), CB("makron_dead", dead_fn),
  {"makron_step_left", {(void *)&step_left_spec, qa_q2bm_sound_handler_fn}},
  {"makron_step_right", {(void *)&step_right_spec, qa_q2bm_sound_handler_fn}},
  {"makron_popup", {(void *)&popup_spec, qa_q2bm_sound_handler_fn}},
  {"makron_hit", {(void *)&hit_spec, qa_q2bm_sound_handler_fn}},
  {"makron_brainsplorch", {(void *)&brain_spec, qa_q2bm_sound_handler_fn}},
  {"makron_prerailgun", {(void *)&prerail_spec, qa_q2bm_sound_handler_fn}},
  CB("makron_taunt", taunt_fn), CB("makronBFG", bfg_fn),
  CB("MakronSaveloc", saveloc_fn), CB("MakronRailgun", railgun_fn), CB("MakronHyperblaster", hyperblaster_fn),
};
const qa_q2m_definition qa_q2bm_makron_definition = {
  .classname = "monster_makron", .kind = "makron", .model = "models/monsters/boss3/rider/tris.md2",
  .health = 3000, .gib_health = -2000, .mass = 500, .bounds = {{-30, -30, 0}, {30, 30, 90}}, .scale = 1,
  .initial_move = "makron_move_sight",
  .moves = qa_q2bm_boss32_moves, .move_count = 12,
  .callbacks = makron_callbacks, .callback_count = sizeof makron_callbacks / sizeof makron_callbacks[0],
  .stand = {(void *)"makron_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"makron_move_walk", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn}, .attack = {NULL, attack_fn},
  .sight = {(void *)"makron_move_sight", qa_q2bm_move_handler_fn},
  .check_attack = check_attack_fn,
  .reactions = {NULL, pain_fn, die_fn},
};
