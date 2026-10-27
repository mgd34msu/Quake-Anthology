/* Port of src/content/q2/missionpacks/monsters/rogue-variants.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/rogue_variants.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/boss2.h"
#include "qa/content/q2/base/monsters/boss_common.h"
#include "qa/content/q2/base/monsters/brain.h"
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/floater.h"
#include "qa/content/q2/base/monsters/gladiator.h"
#include "qa/content/q2/base/monsters/supertank.h"
#include "qa/content/q2/base/monsters/tables/supertank.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/muzzle.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/content/q2/missionpacks/monsters/rogue_infantry.h"
#include "qa/content/q2/missionpacks/monsters/rogue_soldier.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_brain.h"
#include "qa/content/q2/missionpacks/monsters/tables/rogue_float.h"

typedef struct variants_bundle {
  qa_q2m_monsters *monsters;
  qa_q2mpm_monster_state *source;
} variants_bundle;
static bool shot_blocked(variants_bundle *bundle, qa_q2m_context *c) {
  return qa_q2mpm_rogue_blocked_check_shot(c, 0.25 + 0.05 * c->game->options.skill, bundle->source);
}
static bool blocked_both(void *ctx, qa_q2m_context *c, double distance) {
  return shot_blocked(ctx, c) || qa_q2mpm_blocked_check_platform(c, distance);
}
static bool blocked_shot(void *ctx, qa_q2m_context *c, double distance) {
  (void)distance;
  return shot_blocked(ctx, c);
}
static void brain_duck_move(qa_q2m_context *c, double eta) {
  qa_q2mpm_rogue_duck_down(c);
  c->state->duck_wait = q2mpm_now(c->game) + eta + (c->game->options.skill == 0 ? 1 : 0.1 * (3 - c->game->options.skill));
  c->state->next_frame = qa_q2mpm_rogue_brain_frames_value.duck01;
  q2mpm_set_move(c, "brain_move_duck");
}
static void brain_duck_cb(void *ctx, qa_q2m_context *c, double eta) { (void)ctx; brain_duck_move(c, eta); }
static void brain_pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2bm_damaged_skin(c);
  if (q2mpm_now(c->game) < c->state->pain_time) return;
  c->state->pain_time = q2mpm_now(c->game) + 3;
  if (c->game->options.skill == 3) return;
  double random = q2mpm_rnd(c->game);
  q2mpm_snd(c, random < 0.33 || random >= 0.66 ? "brain/brnpain1.wav" : "brain/brnpain2.wav", 2);
  q2mpm_set_move(c, random < 0.33 ? "brain_move_pain1" : random < 0.66 ? "brain_move_pain2" : "brain_move_pain3");
  if (c->state->ducked) qa_q2mpm_rogue_duck_up(c);
}
static void brain_dodge_fn(void *ctx, qa_q2m_context *c, qa_actor_id attacker, double eta,
                           const qa_trace_result *trace, bool gravity) {
  variants_bundle *bundle = ctx;
  (void)gravity;
  qa_q2mpm_duck_fn duck = {NULL, brain_duck_cb};
  qa_q2m_handler sidestep = {NULL, NULL};
  qa_q2mpm_rogue_monster_dodge(c, bundle->monsters, attacker, eta, trace, duck, sidestep);
}
static bool brain_duck_field(void *ctx, qa_q2m_context *c, double eta) { (void)ctx; brain_duck_move(c, eta); return true; }
static void floater_attack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double skill = c->game->options.skill;
  double chance = skill == 0 ? 0 : 1 - 0.5 / skill;
  if (q2mpm_rnd(c->game) > chance) {
    c->state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
    q2mpm_set_move(c, "floater_move_attack1");
    return;
  }
  if (q2mpm_rnd(c->game) <= 0.5) c->state->lefty = !c->state->lefty;
  c->state->attack_state = QA_Q2M_ATTACK_SLIDING;
  q2mpm_set_move(c, "floater_move_attack1a");
}
static void floater_fire_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, 82, &aim)) return;
  const qa_q2mpm_rogue_float_frames *f = &qa_q2mpm_rogue_float_frames_value;
  double effect = c->entity->frame == f->attak104 || c->entity->frame == f->attak107 ? 64 : 0;
  (void)c->weapons.fire_blaster(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 1, 1000, effect,
                                false, false, 0);
  qa_q2mpm_monster_flash(c, 82, aim.start, aim.direction);
}
static void boss_rockets(qa_q2m_context *c, bool predictive) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return;
  qa_q2f_entity *entity = c->entity;
  qa_q2f_game *game = c->game;
  qa_vec3 right = qa_math_angle_vectors_rule(q2mpm_body(game, entity).angles, QA_ANGLE_RULE_NATIVE).right;
  if (predictive) game->host->diagnostic(game->host->ctx, "predictive fire");
  static const double spreads[] = {0.4, 0.025, -0.025, -0.4};
  for (double index = 0; index < 4; index++) {
    double flash = 78 + index;
    qa_vec3 start = qa_q2m_project_flash_default(c, qa_q2m_muzzle_offset(game->options.edition, flash));
    qa_vec3 direction;
    if (predictive) {
      qa_vec3 point = qa_add3_double(enemy.origin, qa_scale3_double(enemy.velocity,
          qa_hypot3(qa_sub3_double(enemy.origin, start)) / 750 - 0.3 + index * 0.15));
      direction = qa_normalize3_double_hypot(qa_sub3_double(point, start));
    } else {
      qa_vec3 target = enemy.origin;
      target.z -= index == 0 || index == 3 ? 15 : 0;
      direction = qa_normalize3_double_hypot(qa_add3_double(
          qa_normalize3_double_hypot(qa_sub3_double(target, start)),
          qa_scale3_double(right, q2mpm_at_d(spreads, 4, index))));
    }
    (void)c->weapons.fire_rocket(c->weapons.ctx, entity, game, start, direction, 50, predictive ? 750 : 500, 70, 50);
    qa_q2mpm_monster_flash(c, flash, start, direction);
  }
}
static void boss_bullet(qa_q2m_context *c, bool right) {
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot(c, right ? 133 : 73, right ? 0.2 : -0.2, &aim)) return;
  c->weapons.fire_bullet(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 6, 4, 900, 500, 0);
  qa_q2mpm_monster_flash(c, right ? 133 : 73, aim.start, aim.direction);
}
static bool boss2_check_fn(void *ctx, qa_q2m_context *c) { (void)ctx; return qa_q2bm_boss_check_attack(c, true, true); }
static void boss2_pred_fn(void *ctx, qa_q2m_context *c) { (void)ctx; boss_rockets(c, true); }
static void boss2_rocket_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  boss_rockets(c, c->entity->has_enemy && c->game->host->is_player(c->game->host->ctx, c->entity->enemy)
                       && q2mpm_rnd(c->game) < 0.9);
}
static void boss2_right_fn(void *ctx, qa_q2m_context *c) { (void)ctx; boss_bullet(c, true); }
static void boss2_left_fn(void *ctx, qa_q2m_context *c) { (void)ctx; boss_bullet(c, false); }
static void boss2_mg_fn(void *ctx, qa_q2m_context *c) { (void)ctx; boss_bullet(c, false); boss_bullet(c, true); }
static void ignore_shots_fn(void *ctx, qa_q2m_context *c) { (void)ctx; c->state->ignore_shots = true; }
static void tank_rocket_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  const qa_q2bm_supertank_frames *f = &qa_q2bm_supertank_frames_value;
  double flash = c->entity->frame == f->attak2_8 ? 70 : c->entity->frame == f->attak2_11 ? 71 : 72;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, flash, &aim)) return;
  (void)c->weapons.fire_rocket(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 50, 500, 70, 50);
  qa_q2mpm_monster_flash(c, flash, aim.start, aim.direction);
}
static void tank_mg_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_vec3 eye;
  if (!qa_q2m_enemy_eye(c, &eye)) return;
  double flash = 64 + c->entity->frame - qa_q2bm_supertank_frames_value.attak1_1;
  qa_vec3 flat = {0, q2mpm_body(c->game, c->entity).angles.y, 0};
  qa_vec3 start = qa_q2m_project_flash(c, qa_q2m_muzzle_offset(c->game->options.edition, flash), flat);
  qa_vec3 direction = qa_normalize3_double_hypot(qa_sub3_double(eye, start));
  c->weapons.fire_bullet(c->weapons.ctx, c->entity, c->game, start, direction, 6, 4, 300, 500, 0);
  qa_q2mpm_monster_flash(c, flash, start, direction);
}
typedef struct jorg_init_ctx {
  qa_q2m_handler base_initialize;
} jorg_init_ctx;
static void jorg_init_fn(void *ctx, qa_q2m_context *c) {
  jorg_init_ctx *init = ctx;
  if (init->base_initialize.fn) init->base_initialize.fn(init->base_initialize.ctx, c);
  c->state->ignore_shots = true;
}
static void with_boss(qa_q2m_definition *out, qa_q2m_definition *definition, qa_q2m_monsters *monsters) {
  qa_q2f_named_think *storage = malloc((definition->source_callbacks.think_count + 1) * sizeof *storage);
  if (!storage) qa_q2f_error("Out of memory creating rogue base variants");
  qa_q2bm_with_boss_explosion_callbacks(out, definition, monsters, storage);
}
size_t qa_q2mpm_create_rogue_base_variants(qa_q2m_definition *out, qa_q2m_monsters *monsters,
                                           qa_q2mpm_monster_state *source) {
  variants_bundle *bundle = malloc(sizeof *bundle);
  if (!bundle) qa_q2f_error("Out of memory creating rogue base variants");
  bundle->monsters = monsters;
  bundle->source = source;
  size_t n = 0;
  n += qa_q2mpm_create_rogue_soldier_definitions(out + n, monsters, source);
  out[n++] = qa_q2mpm_create_rogue_infantry_definition(monsters, source);
  /* brain */
  qa_q2m_definition brain = qa_q2bm_brain_definition;
  brain.moves = qa_q2mpm_rogue_brain_moves;
  brain.move_count = qa_q2mpm_rogue_brain_move_count;
  brain.reactions.ctx = NULL;
  brain.reactions.pain = brain_pain_fn;
  brain.dodge_ctx = bundle;
  brain.dodge = brain_dodge_fn;
  brain.duck = brain_duck_field;
  {
    const qa_q2m_named_handler extra[] = {
      {"monster_duck_down", {NULL, qa_q2mpm_rogue_duck_down_handler}},
      {"monster_duck_hold", {NULL, qa_q2mpm_rogue_duck_hold_handler}},
      {"monster_duck_up", {NULL, qa_q2mpm_rogue_duck_up_handler}},
    };
    brain.callbacks = qa_q2mpm_merge_callbacks(brain.callbacks, brain.callback_count, extra, 3, &brain.callback_count);
  }
  out[n++] = brain;
  /* floater */
  qa_q2m_definition floater = *qa_q2bm_get_floater_definition();
  {
    size_t kept = 0;
    for (size_t i = 0; i < qa_q2mpm_rogue_float_move_count; i++)
      if (strcmp(qa_q2mpm_rogue_float_moves[i].name, "floater_move_activate") != 0) kept++;
    qa_q2m_move *moves = malloc(kept * sizeof *moves);
    if (!moves) qa_q2f_error("Out of memory creating rogue base variants");
    size_t at = 0;
    for (size_t i = 0; i < qa_q2mpm_rogue_float_move_count; i++)
      if (strcmp(qa_q2mpm_rogue_float_moves[i].name, "floater_move_activate") != 0) moves[at++] = qa_q2mpm_rogue_float_moves[i];
    floater.moves = moves;
    floater.move_count = kept;
  }
  floater.blocked_ctx = bundle;
  floater.blocked = blocked_shot;
  floater.attack = (qa_q2m_handler){NULL, floater_attack_fn};
  {
    const qa_q2m_named_handler extra[] = {{"floater_fire_blaster", {NULL, floater_fire_fn}}};
    floater.callbacks = qa_q2mpm_merge_callbacks(floater.callbacks, floater.callback_count, extra, 1,
                                                 &floater.callback_count);
  }
  out[n++] = floater;
  /* gladiator */
  qa_q2m_definition gladiator = qa_q2bm_gladiator_definition;
  gladiator.blocked_ctx = bundle;
  gladiator.blocked = blocked_both;
  out[n++] = gladiator;
  /* boss2 */
  qa_q2m_definition boss2 = qa_q2bm_boss2_definition;
  boss2.has_yaw_speed = true;
  boss2.yaw_speed = 50;
  boss2.check_attack = boss2_check_fn;
  {
    const qa_q2m_named_handler extra[] = {
      {"Boss2PredictiveRocket", {NULL, boss2_pred_fn}},
      {"Boss2Rocket", {NULL, boss2_rocket_fn}},
      {"boss2_firebullet_right", {NULL, boss2_right_fn}},
      {"boss2_firebullet_left", {NULL, boss2_left_fn}},
      {"Boss2MachineGun", {NULL, boss2_mg_fn}},
    };
    boss2.callbacks = qa_q2mpm_merge_callbacks(boss2.callbacks, boss2.callback_count, extra, 5, &boss2.callback_count);
  }
  with_boss(&out[n], &boss2, monsters);
  n++;
  /* supertank */
  qa_q2m_definition tank = qa_q2bm_supertank_definition;
  tank.blocked_ctx = bundle;
  tank.blocked = blocked_both;
  tank.initialize = (qa_q2m_handler){NULL, ignore_shots_fn};
  {
    const qa_q2m_named_handler extra[] = {
      {"supertankRocket", {NULL, tank_rocket_fn}},
      {"supertankMachineGun", {NULL, tank_mg_fn}},
    };
    tank.callbacks = qa_q2mpm_merge_callbacks(tank.callbacks, tank.callback_count, extra, 2, &tank.callback_count);
  }
  with_boss(&out[n], &tank, monsters);
  n++;
  /* jorg */
  qa_q2m_definition jorg = qa_q2mpm_base_create_jorg_definition(monsters);
  {
    jorg_init_ctx *init = malloc(sizeof *init);
    if (!init) qa_q2f_error("Out of memory creating rogue base variants");
    init->base_initialize = jorg.initialize;
    qa_q2m_definition wrapped = jorg;
    wrapped.initialize = (qa_q2m_handler){init, jorg_init_fn};
    with_boss(&out[n], &wrapped, monsters);
    n++;
  }
  /* makron */
  {
    qa_q2m_definition makron = qa_q2mpm_base_makron_definition;
    makron.initialize = (qa_q2m_handler){NULL, ignore_shots_fn};
    qa_q2f_named_think *think_storage = malloc((makron.source_callbacks.think_count + 2) * sizeof *think_storage);
    qa_q2f_named_use *use_storage = malloc((makron.source_callbacks.use_count + 2) * sizeof *use_storage);
    if (!think_storage || !use_storage) qa_q2f_error("Out of memory creating rogue base variants");
    qa_q2mpm_with_makron_spawn_callbacks(&out[n], &makron, monsters, think_storage, use_storage);
    n++;
  }
  return n;
}
