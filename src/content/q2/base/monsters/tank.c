/* Port of src/content/q2/base/monsters/tank.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/tank.h"
#include "qa/foundation/js.h"

#include "base_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/tank.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->brutal = c->entity->has_enemy && c->game->host->is_player(c->game->host->ctx, c->entity->enemy);
  if (c->state->stand_ground) { q2bm_set_move(c, "tank_move_stand"); return; }
  const char *name = c->state->move->name;
  q2bm_set_move(c, strcmp(name, "tank_move_walk") == 0 || strcmp(name, "tank_move_start_run") == 0
                ? "tank_move_run" : "tank_move_start_run");
}
static const qa_q2bm_sound_spec sight_spec = {"tank/sight1.wav", 2, 1};
static const qa_q2bm_sound_spec idle_spec = {"tank/tnkidle1.wav", 2, 2};
static const qa_q2bm_sound_spec footstep_spec = {"tank/step.wav", 4, 1};
static const qa_q2bm_sound_spec thud_spec = {"tank/tnkdeth2.wav", 4, 1};
static const qa_q2bm_sound_spec windup_spec = {"tank/tnkatck4.wav", 1, 1};
static const qa_q2bm_sound_spec strike_spec = {"tank/tnkatck5.wav", 1, 1};

static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2m_health(c->game, c->entity->has_enemy ? &c->entity->enemy : NULL) < 0) {
    c->state->brutal = false;
    q2bm_set_move(c, "tank_move_attack_strike");
    return;
  }
  double range = qa_q2m_target_distance(c), r = q2bm_rnd(c->game);
  if (range <= 125) { q2bm_set_move(c, r < 0.4 ? "tank_move_attack_chain" : "tank_move_attack_blast"); return; }
  if (range <= 250) { q2bm_set_move(c, r < 0.5 ? "tank_move_attack_chain" : "tank_move_attack_blast"); return; }
  if (r < 0.33) { q2bm_set_move(c, "tank_move_attack_chain"); return; }
  if (r < 0.66) {
    c->state->pain_time = q2bm_now(c->game) + 5;
    q2bm_set_move(c, "tank_move_attack_pre_rocket");
    return;
  }
  q2bm_set_move(c, "tank_move_attack_blast");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  if (qa_q2m_health(c->game, &c->entity->actor.id) < c->entity->max_health / 2)
    c->entity->skin = (double)(qa_js_to_int32(qa_js_to_uint32(c->entity->skin) | 1u));
  if (reaction.damage <= 10 || q2bm_now(c->game) < c->state->pain_time) return;
  if (reaction.damage <= 30 && q2bm_rnd(c->game) > 0.2) return;
  double frame = c->entity->frame;
  if (c->game->options.skill >= 2
      && ((frame >= qa_q2bm_tank_frames_value.attak301 && frame <= qa_q2bm_tank_frames_value.attak330)
          || (frame >= qa_q2bm_tank_frames_value.attak101 && frame <= qa_q2bm_tank_frames_value.attak116)))
    return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  q2bm_sound(c->game, c->entity, "tank/tnkpain2.wav", true, 2, false, 0, false, 0);
  if (c->game->options.skill == 3) return;
  q2bm_set_move(c, reaction.damage <= 30 ? "tank_move_pain1"
                   : reaction.damage <= 60 ? "tank_move_pain2" : "tank_move_pain3");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  double damage = reaction.pain.damage;
  if (qa_q2m_health(game, &entity->actor.id) <= state->gib_health) {
    q2bm_sound(game, entity, "misc/udeath.wav", true, 2, false, 0, false, 0);
    qa_q2m_gib_options plain = {.head = false, .metallic = false, .skinned = false, .upright = false};
    qa_q2m_gib_options metallic = {.head = false, .metallic = true, .skinned = false, .upright = false};
    qa_q2m_gib_options gear = {.head = true, .metallic = true, .skinned = false, .upright = false};
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", damage, plain);
    for (int i = 0; i < 4; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_metal/tris.md2", damage, metallic);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/chest/tris.md2", damage, plain);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/gear/tris.md2", damage, gear);
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  q2bm_sound(game, entity, "tank/death.wav", true, 2, false, 0, false, 0);
  state->dead = true; state->can_take_damage = true;
  q2bm_set_traits_damage(game, entity, true);
  q2bm_set_move(c, "tank_move_death");
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = {{-16, -16, -16}, {16, 16, 0}};
  qa_q2bm_finish_corpse_bounds(c, bounds);
}
static void poststrike_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->has_enemy = false;
  run_fn(NULL, c);
}
static void reattack_blaster_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->game->options.skill >= 2 && qa_q2m_visible_enemy(c) && qa_q2bm_alive_enemy(c)
                       && q2bm_rnd(c->game) <= 0.6
                   ? "tank_move_reattack_blast" : "tank_move_attack_post_blast");
}
static void refire_rocket_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->game->options.skill >= 2 && qa_q2bm_alive_enemy(c) && qa_q2m_visible_enemy(c)
                       && q2bm_rnd(c->game) <= 0.4
                   ? "tank_move_attack_fire_rocket" : "tank_move_attack_post_rocket");
}
static void blaster_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double flash = c->entity->frame == qa_q2bm_tank_frames_value.attak110 ? 1
               : c->entity->frame == qa_q2bm_tank_frames_value.attak113 ? 2 : 3;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, flash, &aim)) return;
  (void)c->weapons.fire_blaster(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 30, 800, 8,
                                false, false, 0);
  qa_q2bm_muzzle(c, flash, aim.direction, aim.start);
}
static void rocket_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double flash = c->entity->frame == qa_q2bm_tank_frames_value.attak324 ? 23
               : c->entity->frame == qa_q2bm_tank_frames_value.attak327 ? 24 : 25;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, flash, &aim)) return;
  (void)c->weapons.fire_rocket(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 50, 550, 70, 50);
  qa_q2bm_muzzle(c, flash, aim.direction, aim.start);
}
static void machine_gun_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double frame = c->entity->frame;
  double flash = 4 + frame - qa_q2bm_tank_frames_value.attak406;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(c->game->options.edition, flash));
  qa_vec3 eye;
  bool has_eye = qa_q2m_enemy_eye(c, &eye);
  qa_vec3 angles = q2bm_body(c->game, c->entity).angles;
  qa_vec3 aim_angles = {
    has_eye ? qa_vector_to_angles_with_rule(qa_sub3_double(eye, start), QA_VECTOR_TO_ANGLES_NATIVE_AI).x : 0,
    frame <= qa_q2bm_tank_frames_value.attak415
        ? angles.y - 8 * (frame - qa_q2bm_tank_frames_value.attak411)
        : angles.y + 8 * (frame - qa_q2bm_tank_frames_value.attak419),
    0};
  qa_vec3 direction = qa_math_angle_vectors_rule(aim_angles, QA_ANGLE_RULE_NATIVE).forward;
  c->weapons.fire_bullet(c->weapons.ctx, c->entity, c->game, start, direction, 20, 4, 300, 500, 0);
  qa_q2bm_muzzle(c, flash, direction, start);
}
static void commander_initialize_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->entity->skin = 2;
}
#define CB(name, fn) {name, {NULL, fn}}
static const qa_q2m_named_handler tank_callbacks[] = {
  {"tank_stand", {(void *)"tank_move_stand", qa_q2bm_move_handler_fn}},
  {"tank_walk", {(void *)"tank_move_walk", qa_q2bm_move_handler_fn}},
  CB("tank_run", run_fn),
  {"tank_footstep", {(void *)&footstep_spec, qa_q2bm_sound_handler_fn}},
  {"tank_thud", {(void *)&thud_spec, qa_q2bm_sound_handler_fn}},
  {"tank_windup", {(void *)&windup_spec, qa_q2bm_sound_handler_fn}},
  {"TankStrike", {(void *)&strike_spec, qa_q2bm_sound_handler_fn}},
  CB("tank_dead", dead_fn), CB("tank_poststrike", poststrike_fn),
  {"tank_doattack_rocket", {(void *)"tank_move_attack_fire_rocket", qa_q2bm_move_handler_fn}},
  CB("tank_reattack_blaster", reattack_blaster_fn), CB("tank_refire_rocket", refire_rocket_fn),
  CB("TankBlaster", blaster_fn), CB("TankRocket", rocket_fn), CB("TankMachineGun", machine_gun_fn),
};
const qa_q2m_definition qa_q2bm_tank_definition = {
  .classname = "monster_tank", .kind = "tank", .model = "models/monsters/tank/tris.md2",
  .health = 750, .gib_health = -200, .mass = 500, .bounds = {{-32, -32, -16}, {32, 32, 72}}, .scale = 1,
  .initial_move = "tank_move_stand",
  .moves = qa_q2bm_tank_moves, .move_count = 19,
  .callbacks = tank_callbacks, .callback_count = sizeof tank_callbacks / sizeof tank_callbacks[0],
  .stand = {(void *)"tank_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"tank_move_walk", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn}, .attack = {NULL, attack_fn},
  .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
  .idle = {(void *)&idle_spec, qa_q2bm_sound_handler_fn},
  .reactions = {NULL, pain_fn, die_fn},
};
/* TS: { ...tankDefinition, classname, health, gibHealth, initialize }. */
const qa_q2m_definition qa_q2bm_tank_commander_definition = {
  .classname = "monster_tank_commander", .kind = "tank", .model = "models/monsters/tank/tris.md2",
  .health = 1000, .gib_health = -225, .mass = 500, .bounds = {{-32, -32, -16}, {32, 32, 72}}, .scale = 1,
  .initial_move = "tank_move_stand",
  .moves = qa_q2bm_tank_moves, .move_count = 19,
  .callbacks = tank_callbacks, .callback_count = sizeof tank_callbacks / sizeof tank_callbacks[0],
  .stand = {(void *)"tank_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"tank_move_walk", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn}, .attack = {NULL, attack_fn},
  .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
  .idle = {(void *)&idle_spec, qa_q2bm_sound_handler_fn},
  .initialize = {NULL, commander_initialize_fn},
  .reactions = {NULL, pain_fn, die_fn},
};
