/* Port of src/content/q2/rerelease/monsters/base-variants/hover.ts.
 * Rerelease m_hover.cpp. ZeniMax Media, GPL-2.0. */
#include "qa/content/q2/rerelease/monsters/base_variants/hover.h"

#include <stdlib.h>

#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/base/monsters/hover.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/content/q2/rerelease/monsters/tables/flashes.h"
#include "qa/content/q2/rerelease/monsters/tables/hover.h"
#include "../rrm_internal.h"

static void gib(qa_q2m_context *c) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  q2rm_emit_effect(game, "q2:explosion1", q2rm_body(game, entity).origin, q2rm_zero(), 1, 0);
  entity->skin = trunc(entity->skin / 2);
  qa_q2m_gib_options plain = {false, false, false, false};
  qa_q2m_gib_options metal = {false, true, false, false};
  qa_q2m_gib_options skinned = {false, false, true, false};
  qa_q2m_gib_options skin_metal = {false, true, true, false};
  qa_q2m_gib_options head = {true, false, true, false};
  for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", 150, plain);
  for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_metal/tris.md2", 150, metal);
  (void)qa_q2m_throw_gib(entity, game, "models/monsters/hover/gibs/chest.md2", 150, skinned);
  for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/monsters/hover/gibs/ring.md2", 150, skin_metal);
  for (int i = 0; i < 2; i++) (void)qa_q2m_throw_gib(entity, game, "models/monsters/hover/gibs/foot.md2", 150, skinned);
  (void)qa_q2m_throw_gib(entity, game, "models/monsters/hover/gibs/head.md2", 150, head);
  state->dead = true; state->gibbed = true;
}
static void dead_think_fn(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game) {
  qa_q2m_monsters *monsters = ctx;
  if (!q2rm_body(game, entity).ground && q2rm_now(game) < entity->timestamp) {
    qa_q2_think self = {monsters, dead_think_fn};
    game->schedule(game->ctx, entity, 0.1, self);
    return;
  }
  qa_q2m_context *context = qa_q2m_monsters_context(monsters, entity->actor.id);
  if (context) gib(context);
}
static void init_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  qa_q2m_state *state = c->state;
  state->yaw_speed = 18;
  state->alt.alternate_fly = true;
  state->alt.fly_thrusters = false;
  state->alt.fly_acceleration = 20;
  state->alt.fly_speed = 120;
  state->alt.fly_min_distance = 150;
  state->alt.fly_max_distance = 350;
  qa_q2bm_loop_sound(c, "hover/hovidle1.wav");
}
static void pain_fn(void *ctx, qa_q2m_context *c, qa_pain_reaction reaction) {
  (void)ctx;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  entity->skin = qa_q2m_health(game, &entity->actor.id) < entity->max_health / 2
      ? q2rm_or(entity->skin, 1) : q2rm_and(entity->skin, ~1);
  if (q2rm_now(game) < state->pain_time) return;
  state->pain_time = q2rm_now(game) + 3;
  q2rm_snd(c, q2rm_rnd(game) < 0.5 ? "hover/hovpain1.wav" : "hover/hovpain2.wav", 2);
  if (!qa_q2rm_reacts_to_pain(c)) return;
  double choice = q2rm_rnd(game);
  q2rm_set_move(c, reaction.damage <= 25 ? (choice < 0.5 ? "hover_move_pain3" : "hover_move_pain2")
                                          : (choice < 0.3 ? "hover_move_pain1" : "hover_move_pain2"));
}
static void die_fn(void *ctx, qa_q2m_context *c, qa_death_reaction reaction) {
  (void)ctx; (void)reaction;
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  qa_q2m_state *state = c->state;
  entity->effects = 0;
  qa_q2rm_set_powered(game, entity->actor, "none", 0);
  if (qa_q2rm_check_gib(c)) { gib(c); return; }
  if (state->dead) return;
  q2rm_snd(c, q2rm_rnd(game) < 0.5 ? "hover/hovdeth1.wav" : "hover/hovdeth2.wav", 2);
  state->dead = true; state->can_take_damage = true;
  q2rm_set_damageable(game, entity, true);
  q2rm_set_move(c, "hover_move_death1");
}
static void attack_cb_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (q2rm_rnd(c->game) > 0.5) {
    c->state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
    q2rm_set_move(c, "hover_move_attack1");
    return;
  }
  if (q2rm_rnd(c->game) <= 0.5) c->state->lefty = !c->state->lefty;
  c->state->attack_state = QA_Q2M_ATTACK_SLIDING;
  q2rm_set_move(c, "hover_move_attack2");
}
static void reattack_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  if (qa_q2bm_alive_enemy(c) && qa_q2m_visible_enemy(c) && q2rm_rnd(c->game) <= 0.6) {
    if (c->state->attack_state == QA_Q2M_ATTACK_STRAIGHT) { q2rm_set_move(c, "hover_move_attack1"); return; }
    if (c->state->attack_state == QA_Q2M_ATTACK_SLIDING) { q2rm_set_move(c, "hover_move_attack2"); return; }
  }
  q2rm_set_move(c, "hover_move_end_attack");
}
static void fire_blaster_fn(void *ctx, qa_q2m_context *c) {
  (void)ctx;
  double flash = q2rm_has(c->entity->frame, 1) ? qa_q2rm_rereleaseFlash_value.HOVER_BLASTER_2
                                                : qa_q2rm_rereleaseFlash_value.HOVER_BLASTER_1;
  qa_q2bm_shot_result aim;
  if (!qa_q2bm_shot_default(c, flash, &aim)) return;
  double effects = fmod(c->entity->frame, 4) == 0 ? 64 : 0;
  (void)c->weapons.fire_blaster(c->weapons.ctx, c->entity, c->game, aim.start, aim.direction, 1, 1000, effects, false, false, 0);
  qa_q2rm_monster_flash(c, flash, aim.start, aim.direction);
}
static void dead_fn(void *ctx, qa_q2m_context *c) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  c->state->corpse = true;
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds.min = q2rm_v3(-16, -16, -24);
  change.bounds.max = q2rm_v3(16, 16, -8);
  q2rm_move_body(game, entity, change, false, false);
  game->motion(game->ctx, entity, "toss");
  entity->timestamp = q2rm_now(game) + 15;
  qa_q2_think think = {ctx, dead_think_fn};
  game->schedule(game->ctx, entity, 0.1, think);
}
static void dying_fn(void *ctx, qa_q2m_context *c) {
  qa_q2_entity *entity = c->entity;
  qa_q2_game_services *game = c->game;
  if (q2rm_body(game, entity).ground) { dead_think_fn(ctx, entity, game); return; }
  if (qa_q2_rerelease_random_integer_max(qa_q2rm_rerelease_random(c), 2) != 0) return;
  q2rm_emit_effect(game, "q2:plain-explosion", q2rm_body(game, entity).origin, q2rm_zero(), 1, 0);
  bool organic = qa_q2_rerelease_random_integer_max(qa_q2rm_rerelease_random(c), 2) != 0;
  qa_q2m_gib_options options = {false, !organic, false, false};
  (void)qa_q2m_throw_gib(entity, game, organic ? "models/objects/gibs/sm_meat/tris.md2"
                                               : "models/objects/gibs/sm_metal/tris.md2", 120, options);
}

qa_q2m_definition qa_q2rm_create_rerelease_hover_definition(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters) {
  qa_q2_named_think *think_defs = qa_q2mpm_arena_alloc(arena, sizeof *think_defs);
  if (!think_defs) qa_raise("Error", 0, 0, "Out of memory creating rerelease hover definition");
  think_defs[0].name = "hover_deadthink";
  think_defs[0].cb.ctx = monsters;
  think_defs[0].cb.fn = dead_think_fn;
  const qa_q2m_named_handler extra[] = {
    {"hover_attack", {NULL, attack_cb_fn}},
    {"hover_reattack", {NULL, reattack_fn}},
    {"hover_fire_blaster", {NULL, fire_blaster_fn}},
    {"hover_dead", {monsters, dead_fn}},
    {"hover_dying", {monsters, dying_fn}},
  };
  qa_q2m_definition definition = qa_q2bm_hover_definition;
  definition.moves = qa_q2rm_hover_moves;
  definition.move_count = qa_q2rm_hover_move_count;
  definition.has_source_callbacks = true;
  memset(&definition.source_callbacks, 0, sizeof definition.source_callbacks);
  definition.source_callbacks.think = think_defs;
  definition.source_callbacks.think_count = 1;
  definition.initialize = (qa_q2m_handler){NULL, init_fn};
  definition.reactions.pain = pain_fn;
  definition.reactions.die = die_fn;
  definition.callbacks = qa_q2rm_merge_callbacks(arena, definition.callbacks, definition.callback_count,
                                                 extra, sizeof extra / sizeof extra[0], &definition.callback_count);
  return definition;
}
