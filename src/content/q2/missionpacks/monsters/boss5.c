/* Port of src/content/q2/missionpacks/monsters/boss5.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/boss5.h"

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/boss_common.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/missionpacks/monsters/power_armor.h"
#include "qa/content/q2/missionpacks/monsters/tables/xatrix_boss5.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_set_move(c, c->state->stand_ground ? "boss5_move_stand" : "boss5_move_run");
}
static void init_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2mpm_monster_power_armor(c, "shield", 400); }
static void search_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_snd(c, q2mpm_rnd(c->game) < 0.5 ? "bosstank/btkunqv1.wav" : "bosstank/btkunqv2.wav", 2);
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_set_move(c, qa_q2m_target_distance(c) <= 160 || q2mpm_rnd(c->game) < 0.3 ? "boss5_move_attack1"
                                                                                  : "boss5_move_attack2");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  const qa_q2mpm_xatrix_boss5_frames *frame = &qa_q2mpm_xatrix_boss5_frames_value;
  qa_q2bm_damaged_skin(c);
  if (q2mpm_now(c->game) < c->state->pain_time || (reaction.damage <= 25 && q2mpm_rnd(c->game) < 0.2)) return;
  if (c->game->options.skill >= 2 && c->entity->frame >= frame->attak2_1 && c->entity->frame <= frame->attak2_14)
    return;
  c->state->pain_time = q2mpm_now(c->game) + 3;
  q2mpm_snd(c, reaction.damage <= 10 ? "bosstank/btkpain1.wav" : reaction.damage <= 25 ? "bosstank/btkpain3.wav"
                                                                                        : "bosstank/btkpain2.wav", 2);
  q2mpm_set_move(c, reaction.damage <= 10 ? "boss5_move_pain1" : reaction.damage <= 25 ? "boss5_move_pain2"
                                                                                        : "boss5_move_pain3");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  q2mpm_snd(c, "bosstank/btkdeth1.wav", 2);
  c->state->dead = true;
  c->state->can_take_damage = false;
  c->entity->count = 0;
  q2mpm_set_damageable(c->game, c->entity, false);
  q2mpm_set_move(c, "boss5_move_death");
}
static const qa_q2bm_sound_spec tread_spec = {"bosstank/btkengn1.wav", 2, 1};
static void dead_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_bounds bounds = {{-60, -60, 0}, {60, 60, 72}};
  qa_q2bm_finish_corpse_bounds(c, bounds);
}
static void reattack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2mpm_set_move(c, qa_q2m_visible_enemy(c) && q2mpm_rnd(c->game) < 0.9 ? "boss5_move_attack1"
                                                                        : "boss5_move_end_attack1");
}
static void rocket_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  const qa_q2mpm_xatrix_boss5_frames *frame = &qa_q2mpm_xatrix_boss5_frames_value;
  double flash = c->entity->frame == frame->attak2_8 ? 70 : c->entity->frame == frame->attak2_11 ? 71 : 72;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, flash, &aim)) return;
  (void)c->weapons.fire_rocket(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 50, 500, 70, 50);
  qa_q2bm_muzzle(c, flash, aim.direction, aim.start);
}
static void machine_gun_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  const qa_q2mpm_xatrix_boss5_frames *frame = &qa_q2mpm_xatrix_boss5_frames_value;
  double flash = 64 + c->entity->frame - frame->attak1_1;
  qa_body_state body = q2mpm_body(c->game, c->entity);
  qa_vec3 flat = {0, body.angles.y, 0};
  qa_vec3 start = qa_q2m_project_flash(c, qa_q2m_muzzle_offset(c->game->options.edition, flash), flat);
  qa_vec3 eye;
  qa_vec3 direction = qa_q2m_enemy_eye(c, &eye)
      ? qa_normalize3_double_hypot(qa_sub3_double(eye, start))
      : qa_math_angle_vectors_rule(flat, QA_ANGLE_RULE_NATIVE).forward;
  c->weapons.fire_bullet(c->weapons.ctx, c->entity, c->game, start, direction, 6, 4, 300, 500, 0);
  qa_q2bm_muzzle(c, flash, direction, start);
}
static const qa_q2m_named_handler boss5_callbacks[] = {
  {"boss5_run", {NULL, run_fn}},
  {"BossExplode2", {NULL, qa_q2bm_boss_explode_handler}},
  {"TreadSound2", {(void *)&tread_spec, qa_q2bm_sound_handler_fn}},
  {"boss5_dead", {NULL, dead_fn}},
  {"boss5_reattack1", {NULL, reattack_fn}},
  {"boss5Rocket", {NULL, rocket_fn}},
  {"boss5MachineGun", {NULL, machine_gun_fn}},
};
const qa_q2m_definition qa_q2mpm_boss5_definition = {
  .classname = "monster_boss5", .kind = "boss5", .model = "models/monsters/boss5/tris.md2",
  .health = 1500, .gib_health = -500, .mass = 800, .bounds = {{-64, -64, 0}, {64, 64, 112}}, .scale = 1,
  .initial_move = "boss5_move_stand",
  .moves = qa_q2mpm_xatrix_boss5_moves, .move_count = 15,
  .callbacks = boss5_callbacks, .callback_count = sizeof boss5_callbacks / sizeof boss5_callbacks[0],
  .stand = {(void *)"boss5_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"boss5_move_forward", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn}, .attack = {NULL, attack_fn}, .search = {NULL, search_fn},
  .reactions = {NULL, pain_fn, die_fn},
  .initialize = {NULL, init_fn}, .restore = {NULL, qa_q2mpm_restore_monster_power_armor_handler},
};
