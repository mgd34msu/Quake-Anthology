/* Port of src/content/q2/base/monsters/supertank.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/supertank.h"

#include "base_internal.h"
#include "qa/content/q2/base/monsters/boss_common.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/supertank.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->state->stand_ground ? "supertank_move_stand" : "supertank_move_run");
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, qa_q2m_target_distance(c) <= 160 || q2bm_rnd(c->game) < 0.3
                   ? "supertank_move_attack1" : "supertank_move_attack2");
}
static void search_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_sound(c->game, c->entity, q2bm_rnd(c->game) < 0.5 ? "bosstank/btkunqv1.wav" : "bosstank/btkunqv2.wav",
             true, 2, false, 0, false, 0);
}
static const qa_q2bm_sound_spec tread_spec = {"bosstank/btkengn1.wav", 4, 1};

static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2bm_damaged_skin(c);
  if (q2bm_now(c->game) < c->state->pain_time
      || (reaction.damage <= 25 && q2bm_rnd(c->game) < 0.2))
    return;
  if (c->game->options.skill >= 2 && c->entity->frame >= qa_q2bm_supertank_frames_value.attak2_1
      && c->entity->frame <= qa_q2bm_supertank_frames_value.attak2_14)
    return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  if (c->game->options.skill == 3) return;
  q2bm_sound(c->game, c->entity,
             reaction.damage <= 10 ? "bosstank/btkpain1.wav"
             : reaction.damage <= 25 ? "bosstank/btkpain3.wav" : "bosstank/btkpain2.wav",
             true, 2, false, 0, false, 0);
  q2bm_set_move(c, reaction.damage <= 10 ? "supertank_move_pain1"
                   : reaction.damage <= 25 ? "supertank_move_pain2" : "supertank_move_pain3");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  q2bm_sound(c->game, c->entity, "bosstank/btkdeth1.wav", true, 2, false, 0, false, 0);
  c->state->dead = true; c->state->can_take_damage = false; c->entity->count = 0;
  q2bm_set_traits_damage(c->game, c->entity, false);
  q2bm_set_move(c, "supertank_move_death");
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = {{-60, -60, 0}, {60, 60, 72}};
  qa_q2bm_finish_corpse_bounds(c, bounds);
}
static void reattack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, qa_q2m_visible_enemy(c) && q2bm_rnd(c->game) < 0.9
                   ? "supertank_move_attack1" : "supertank_move_end_attack1");
}
static void rocket_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double flash = c->entity->frame == qa_q2bm_supertank_frames_value.attak2_8 ? 70
               : c->entity->frame == qa_q2bm_supertank_frames_value.attak2_11 ? 71 : 72;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, flash, &aim)) return;
  (void)c->weapons.fire_rocket(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 50, 500, 70, 50);
  qa_q2bm_muzzle(c, flash, aim.direction, aim.start);
}
static void machine_gun_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double flash = 64 + c->entity->frame - qa_q2bm_supertank_frames_value.attak1_1;
  qa_body_state body = q2bm_body(c->game, c->entity);
  double yaw = body.angles.y;
  qa_angle_vectors axes = qa_math_angle_vectors_rule(q2bm_v3(0, yaw, 0), QA_ANGLE_RULE_NATIVE);
  qa_vec3 offset = qa_q2m_muzzle_offset(c->game->options.edition, flash);
  qa_vec3 start = {body.origin.x + axes.forward.x * offset.x + axes.right.x * offset.y,
                   body.origin.y + axes.forward.y * offset.x + axes.right.y * offset.y,
                   body.origin.z + offset.z};
  qa_vec3 eye;
  qa_vec3 direction = qa_q2m_enemy_eye(c, &eye)
      ? qa_normalize3_double_hypot(qa_sub3_double(eye, start)) : axes.forward;
  c->weapons.fire_bullet(c->weapons.ctx, c->entity, c->game, start, direction, 6, 4, 300, 500, 0);
  qa_q2bm_muzzle(c, flash, direction, start);
}
#define CB(name, fn) {name, {NULL, fn}}
static const qa_q2m_named_handler supertank_callbacks[] = {
  CB("supertank_run", run_fn),
  {"TreadSound", {(void *)&tread_spec, qa_q2bm_sound_handler_fn}},
  CB("BossExplode", qa_q2bm_boss_explode_handler),
  CB("supertank_dead", dead_fn), CB("supertank_reattack1", reattack_fn),
  CB("supertankRocket", rocket_fn), CB("supertankMachineGun", machine_gun_fn),
};
const qa_q2m_definition qa_q2bm_supertank_definition = {
  .classname = "monster_supertank", .kind = "supertank", .model = "models/monsters/boss1/tris.md2",
  .health = 1500, .gib_health = -500, .mass = 800, .bounds = {{-64, -64, 0}, {64, 64, 112}}, .scale = 1,
  .initial_move = "supertank_move_stand",
  .moves = qa_q2bm_supertank_moves, .move_count = 15,
  .callbacks = supertank_callbacks, .callback_count = sizeof supertank_callbacks / sizeof supertank_callbacks[0],
  .stand = {(void *)"supertank_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"supertank_move_forward", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn}, .attack = {NULL, attack_fn}, .search = {NULL, search_fn},
  .reactions = {NULL, pain_fn, die_fn},
};
