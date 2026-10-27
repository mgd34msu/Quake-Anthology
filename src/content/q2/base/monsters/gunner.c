/* Port of src/content/q2/base/monsters/gunner.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/gunner.h"

#include "base_internal.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/tables/gunner.h"
#include "qa/content/q2/foundation/monsters/ai.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, c->state->stand_ground ? "gunner_move_stand" : "gunner_move_run");
}
static void grenade_fire(qa_q2m_context *c) {
  double frame = c->entity->frame;
  double flash = frame == qa_q2bm_gunner_frames_value.attak105 ? 53
               : frame == qa_q2bm_gunner_frames_value.attak108 ? 54
               : frame == qa_q2bm_gunner_frames_value.attak111 ? 55 : 56;
  qa_q2bm_shot_result shot = qa_q2bm_forward_shot(c, flash);
  (void)c->weapons.fire_grenade(c->weapons.ctx, c->entity, c->game, shot.start, shot.direction,
                                50, 600, 2.5, 90, false, false, true, NULL);
  qa_q2bm_muzzle(c, flash, shot.direction, shot.start);
}
static void grenade_fn(void *ctx, qa_q2m_context *c) { (void)ctx; grenade_fire(c); }
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, qa_q2m_target_distance(c) < 80 ? "gunner_move_attack_chain"
                   : q2bm_rnd(c->game) <= 0.5 ? "gunner_move_attack_grenade" : "gunner_move_attack_chain");
}
static const qa_q2bm_sound_spec sight_spec = {"gunner/sight1.wav", 2, 1};
static const qa_q2bm_sound_spec search_spec = {"gunner/gunsrch1.wav", 2, 1};
static const qa_q2bm_sound_spec idle_spec = {"gunner/gunidle1.wav", 2, 2};
static const qa_q2bm_sound_spec open_spec = {"gunner/gunatck1.wav", 2, 2};

static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2bm_damaged_skin(c);
  if (q2bm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2bm_now(c->game) + 3;
  q2bm_sound(c->game, c->entity, q2bm_rnd(c->game) < 0.5 ? "gunner/gunpain2.wav" : "gunner/gunpain1.wav",
             true, 2, false, 0, false, 0);
  if (c->game->options.skill == 3) return;
  q2bm_set_move(c, reaction.damage <= 10 ? "gunner_move_pain3"
                   : reaction.damage <= 25 ? "gunner_move_pain2" : "gunner_move_pain1");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2bm_begin_death_default(c, reaction, "gunner/death1.wav", "gunner_move_death");
}
static void dodge_fn(void *ctx, qa_q2m_context *c, qa_actor_id attacker, double eta,
                     const qa_trace_result *trace, bool gravity) {
  (void)ctx; (void)eta; (void)trace; (void)gravity;
  if (q2bm_rnd(c->game) > 0.25) return;
  if (!c->entity->has_enemy) { c->entity->has_enemy = true; c->entity->enemy = attacker; }
  q2bm_set_move(c, "gunner_move_duck");
}
static void fidget_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!c->state->stand_ground && q2bm_rnd(c->game) <= 0.05) q2bm_set_move(c, "gunner_move_fidget");
}
static void duck_down_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (c->state->ducked) return;
  if (c->game->options.skill >= 2 && q2bm_rnd(c->game) > 0.5) grenade_fire(c);
  qa_q2m_set_duck(c, true);
  c->state->pause_time = q2bm_now(c->game) + 1;
}
static void duck_hold_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  c->state->hold_frame = q2bm_now(c->game) < c->state->pause_time;
}
static void duck_up_fn(void *ctx, qa_q2m_context *c) { (void)ctx; qa_q2m_set_duck(c, false); }
static void fire_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double flash = 45 + c->entity->frame - qa_q2bm_gunner_frames_value.attak216;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot(c, flash, -0.2, &aim)) return;
  c->weapons.fire_bullet(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 3, 4, 300, 500, 0);
  qa_q2bm_muzzle(c, flash, aim.direction, aim.start);
}
static void refire_chain_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2bm_set_move(c, qa_q2bm_alive_enemy(c) && qa_q2m_visible_enemy(c) && q2bm_rnd(c->game) <= 0.5
                   ? "gunner_move_fire_chain" : "gunner_move_endfire_chain");
}
#define CB(name, fn) {name, {NULL, fn}}
static const qa_q2m_named_handler gunner_callbacks[] = {
  {"gunner_stand", {(void *)"gunner_move_stand", qa_q2bm_move_handler_fn}},
  CB("gunner_run", run_fn), CB("gunner_dead", qa_q2bm_finish_corpse_handler),
  {"gunner_idlesound", {(void *)&idle_spec, qa_q2bm_sound_handler_fn}},
  {"gunner_opengun", {(void *)&open_spec, qa_q2bm_sound_handler_fn}},
  CB("gunner_fidget", fidget_fn),
  CB("gunner_duck_down", duck_down_fn), CB("gunner_duck_hold", duck_hold_fn), CB("gunner_duck_up", duck_up_fn),
  CB("GunnerGrenade", grenade_fn), CB("GunnerFire", fire_fn),
  {"gunner_fire_chain", {(void *)"gunner_move_fire_chain", qa_q2bm_move_handler_fn}},
  CB("gunner_refire_chain", refire_chain_fn),
};
const qa_q2m_definition qa_q2bm_gunner_definition = {
  .classname = "monster_gunner", .kind = "gunner", .model = "models/monsters/gunner/tris.md2",
  .health = 175, .gib_health = -70, .mass = 200, .bounds = {{-16, -16, -24}, {16, 16, 32}}, .scale = 1,
  .initial_move = "gunner_move_stand",
  .moves = qa_q2bm_gunner_moves, .move_count = 14,
  .callbacks = gunner_callbacks, .callback_count = sizeof gunner_callbacks / sizeof gunner_callbacks[0],
  .stand = {(void *)"gunner_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"gunner_move_walk", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn}, .attack = {NULL, attack_fn},
  .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
  .search = {(void *)&search_spec, qa_q2bm_sound_handler_fn},
  .reactions = {NULL, pain_fn, die_fn},
  .dodge = dodge_fn,
};
