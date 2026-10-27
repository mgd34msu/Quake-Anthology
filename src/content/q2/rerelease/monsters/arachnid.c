/* Port of src/content/q2/rerelease/monsters/arachnid.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/monsters/arachnid.h"

#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/arachnid.h"
#include "qa/content/q2/rerelease/monsters/tables/flashes.h"
#include "rrm_internal.h"

static void run_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_set_move(c, c->state->stand_ground ? "arachnid_move_stand" : "arachnid_move_run");
}
static void attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  if (c->state->melee_time < q2rm_now(c->game) && qa_q2m_target_distance(c) < 80) {
    q2rm_set_move(c, "arachnid_melee");
    return;
  }
  q2rm_set_move(c, enemy.origin.z - q2rm_body(c->game, c->entity).origin.z > 150
      ? "arachnid_attack_up1" : "arachnid_attack1");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  if (q2rm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2rm_now(c->game) + 3;
  q2rm_snd(c, "arachnid/pain.wav", 2);
  if (!qa_q2rm_reacts_to_pain(c)) return;
  q2rm_set_move(c, q2rm_rnd(c->game) < 0.5 ? "arachnid_move_pain1" : "arachnid_move_pain2");
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  if (qa_q2rm_check_gib(c)) {
    q2rm_snd(c, "misc/udeath.wav", 2);
    qa_q2m_gib_options plain = {false, false, false, false};
    for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/bone/tris.md2", reaction.pain.damage, plain);
    for (int i = 0; i < 4; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", reaction.pain.damage, plain);
    qa_q2m_gib_options head = {true, false, false, false};
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/head2/tris.md2", reaction.pain.damage, head);
    state->dead = true; state->gibbed = true;
    return;
  }
  if (state->dead) return;
  q2rm_snd(c, "arachnid/death.wav", 2);
  state->dead = true; state->can_take_damage = true;
  q2rm_set_damageable(game, entity, true);
  q2rm_set_move(c, "arachnid_move_death");
}
static void footstep_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  q2rm_snd4(c, "insane/insane11.wav", 4, 0.5, 2);
}
static void melee_hit_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (!c->weapons.fire_hit(c->weapons.ctx, c->entity, c->game, q2rm_v3(80, 0, 0), 15, 50))
    c->state->melee_time = q2rm_now(c->game) + 1;
}
static void charge_rail_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 target;
  if (!qa_q2m_enemy_eye(c, &target)) return;
  c->entity->pos1 = target;
  q2rm_snd(c, "gladiator/railgun.wav", 1);
}
static void rail_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double frame = c->entity->frame;
  double flash = frame == qa_q2rm_arachnid_frames_value.rails8 ? qa_q2rm_rereleaseFlash_value.ARACHNID_RAIL2
      : frame == qa_q2rm_arachnid_frames_value.rails_up7 ? qa_q2rm_rereleaseFlash_value.ARACHNID_RAIL_UP1
      : frame == qa_q2rm_arachnid_frames_value.rails_up11 ? qa_q2rm_rereleaseFlash_value.ARACHNID_RAIL_UP2
      : qa_q2rm_rereleaseFlash_value.ARACHNID_RAIL1;
  qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(QA_Q2F_RERELEASE, flash));
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(c->entity->pos1, start));
  c->weapons.fire_rail(c->weapons.ctx, c->entity, c->game, start, direction, 35, 100);
  qa_q2rm_monster_flash(c, flash, start, direction);
}
static const qa_q2bm_sound_spec sight_spec = {"arachnid/sight.wav", 2, 1};
static const qa_q2bm_sound_spec melee_charge_spec = {"gladiator/melee3.wav", 1, 1};
static const qa_q2m_named_handler arachnid_callbacks[] = {
  {"arachnid_run", {NULL, run_fn}},
  {"arachnid_dead", {NULL, qa_q2bm_finish_corpse_handler}},
  {"arachnid_footstep", {NULL, footstep_fn}},
  {"arachnid_melee_charge", {(void *)&melee_charge_spec, qa_q2bm_sound_handler_fn}},
  {"arachnid_melee_hit", {NULL, melee_hit_fn}},
  {"arachnid_charge_rail", {NULL, charge_rail_fn}},
  {"arachnid_rail", {NULL, rail_fn}},
};
const qa_q2m_definition qa_q2rm_arachnid_definition = {
  .classname = "monster_arachnid", .kind = "arachnid", .model = "models/monsters/arachnid/tris.md2",
  .health = 1000, .gib_health = -200, .mass = 450, .scale = 1,
  .bounds = {{-48, -48, -20}, {48, 48, 48}},
  .initial_move = "arachnid_move_stand",
  .moves = qa_q2rm_arachnid_moves, .move_count = 9,
  .callbacks = arachnid_callbacks, .callback_count = sizeof arachnid_callbacks / sizeof arachnid_callbacks[0],
  .stand = {(void *)"arachnid_move_stand", qa_q2bm_move_handler_fn},
  .walk = {(void *)"arachnid_move_walk", qa_q2bm_move_handler_fn},
  .run = {NULL, run_fn},
  .attack = {NULL, attack_fn},
  .sight = {(void *)&sight_spec, qa_q2bm_sound_handler_fn},
  .reactions = {NULL, pain_fn, die_fn},
};
