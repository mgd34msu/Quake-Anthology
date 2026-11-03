/* Port of src/content/q2/missionpacks/monsters/rogue-common.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"
#include "qa/foundation/js.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/parasite.h"
#include "qa/content/q2/foundation/monsters/ai.h"

qa_q2m_named_handler *qa_q2mpm_merge_callbacks(qa_q2mpm_arena *arena, const qa_q2m_named_handler *base, size_t base_count,
                                               const qa_q2m_named_handler *extra, size_t extra_count,
                                               size_t *out_count) {
  size_t total = base_count + extra_count;
  qa_q2m_named_handler *merged = qa_q2mpm_arena_alloc(arena, (total ? total : 1) * sizeof *merged);
  if (!merged) qa_raise("Error", 0, 0, "Out of memory merging mission-pack monster callbacks");
  size_t count = 0;
  for (size_t i = 0; i < base_count; i++) {
    bool overridden = false;
    for (size_t j = 0; j < extra_count; j++)
      if (strcmp(base[i].name, extra[j].name) == 0) overridden = true;
    if (!overridden) merged[count++] = base[i];
  }
  for (size_t j = 0; j < extra_count; j++) merged[count++] = extra[j];
  *out_count = count;
  return merged;
}
const qa_q2m_handler *qa_q2mpm_find_callback(const qa_q2m_named_handler *table, size_t count, const char *name) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(table[i].name, name) == 0) return &table[i].handler;
  return NULL;
}

bool qa_q2mpm_source_trace_world(qa_q2_game_services *game, qa_trace_result trace) {
  return trace.hit.kind == QA_TRACE_HIT_NONE || trace.hit.kind == QA_TRACE_HIT_WORLD
      || qa_same_actor(trace.hit.actor, game->host->world_actor(game->host->ctx));
}
double qa_q2mpm_monster_mass(qa_q2m_context *context) {
  qa_combat_state_result combat = qa_gameplay_authority_read(context->game->host->combat, context->entity->actor.id);
  if (!combat.has_value) qa_raise("Error", 0, 0, "Source monster has no shared combat state");
  return combat.value.mass;
}
bool qa_q2mpm_rogue_parasite_drain_trace(qa_q2m_context *context, qa_trace_result *out) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(context, &enemy)) return false;
  qa_vec3 start = qa_q2m_project_flash_default(context, q2mpm_v3(24, 0, 6));
  qa_vec3 top = enemy.origin; top.z += enemy.bounds.max.z - 8;
  qa_vec3 bottom = enemy.origin; bottom.z += enemy.bounds.min.z + 8;
  if (!qa_q2bm_parasite_drain_reachable(start, enemy.origin) && !qa_q2bm_parasite_drain_reachable(start, top)
      && !qa_q2bm_parasite_drain_reachable(start, bottom)) return false;
  *out = q2mpm_trace(context->game, start, enemy.origin, NULL, &context->entity->actor.id, 0x6000003);
  return true;
}
bool qa_q2mpm_rogue_blocked_check_shot(qa_q2m_context *context, double chance, qa_q2mpm_monster_state *source) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  if (!entity->has_enemy || !game->host->is_player(game->host->ctx, entity->enemy) || q2mpm_rnd(game) < chance)
    return false;
  if (strcmp(entity->classname, "monster_parasite") == 0) {
    qa_trace_result trace;
    if (!qa_q2mpm_rogue_parasite_drain_trace(context, &trace)) return false;
    if (trace.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(trace.hit.actor, entity->enemy)) {
      qa_q2mpm_state_get(source, entity)->blocked = true;
      context->attack(context->impl_ctx, context);
      qa_q2mpm_state_get(source, entity)->blocked = false;
      return true;
    }
  }
  if (!qa_q2m_visible_enemy(context)) return false;
  qa_q2_entity *enemy = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
  const char *tesla = q2mpm_rr(game) ? "tesla_mine" : "tesla";
  if (!enemy || strcmp(enemy->classname, tesla) != 0) return false;
  qa_q2mpm_state_get(source, entity)->blocked = true;
  context->attack(context->impl_ctx, context);
  qa_q2mpm_state_get(source, entity)->blocked = false;
  return true;
}
void qa_q2mpm_rogue_duck_down(qa_q2m_context *context) {
  qa_q2m_state *state = context->state;
  qa_body_state body = q2mpm_body(context->game, context->entity);
  state->ducked = true;
  state->can_take_damage = true;
  q2mpm_set_damageable(context->game, context->entity, true);
  if (state->duck_wait < q2mpm_now(context->game)) state->duck_wait = q2mpm_now(context->game) + 1;
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds = body.bounds;
  change.bounds.max.z = state->normal_height - 32;
  q2mpm_move_body(context->game, context->entity, change, false, false);
}
void qa_q2mpm_rogue_duck_hold(qa_q2m_context *context) {
  context->state->hold_frame = q2mpm_now(context->game) < context->state->duck_wait;
}
void qa_q2mpm_rogue_duck_up(qa_q2m_context *context) {
  qa_q2m_state *state = context->state;
  qa_body_state body = q2mpm_body(context->game, context->entity);
  state->ducked = false;
  state->can_take_damage = true;
  state->next_duck_time = q2mpm_now(context->game) + 0.5;
  q2mpm_set_damageable(context->game, context->entity, true);
  qa_q2_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds = body.bounds;
  change.bounds.max.z = state->normal_height;
  q2mpm_move_body(context->game, context->entity, change, false, false);
}
void qa_q2mpm_rogue_duck_down_handler(void *ctx, qa_q2m_context *context) { (void)ctx; qa_q2mpm_rogue_duck_down(context); }
void qa_q2mpm_rogue_duck_hold_handler(void *ctx, qa_q2m_context *context) { (void)ctx; qa_q2mpm_rogue_duck_hold(context); }
void qa_q2mpm_rogue_duck_up_handler(void *ctx, qa_q2m_context *context) { (void)ctx; qa_q2mpm_rogue_duck_up(context); }

void qa_q2mpm_rogue_monster_dodge(qa_q2m_context *context, qa_q2m_monsters *monsters, qa_actor_id attacker,
                                  double eta, const qa_trace_result *trace, qa_q2mpm_duck_fn duck,
                                  qa_q2m_handler sidestep) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  qa_q2m_state *state = context->state;
  double random = q2mpm_rnd(game);
  if (qa_q2m_health(game, &entity->actor.id) < 1 || (duck.fn == NULL && (sidestep.fn == NULL || state->stand_ground)))
    return;
  if (!entity->has_enemy) {
    entity->has_enemy = true;
    entity->enemy = attacker;
    qa_q2m_monsters_found_target(monsters, context);
  }
  if (eta < 0.1 || eta > 5 || random > 0.25 * (game->options.skill + 1) || trace == NULL) return;
  qa_body_state body = q2mpm_body(game, entity);
  double height = body.origin.z + body.bounds.max.z - (duck.fn == NULL ? 0 : 33);
  bool dodger = sidestep.fn != NULL && !state->stand_ground;
  if (duck.fn != NULL && !dodger && (trace->end.z <= height || state->ducked)) return;
  if (dodger) {
    if (state->dodging) return;
    if (trace->end.z <= height || state->ducked) {
      state->lefty = qa_dot3_double(qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).right,
                                    qa_sub3_double(trace->end, body.origin)) >= 0;
      if (duck.fn != NULL && state->ducked) qa_q2mpm_rogue_duck_up(context);
      state->dodging = true;
      state->attack_state = QA_Q2M_ATTACK_SLIDING;
      sidestep.fn(sidestep.ctx, context);
      return;
    }
  }
  if (duck.fn != NULL && state->next_duck_time <= q2mpm_now(game)) {
    qa_q2m_finish_dodge(context);
    state->ducked = true;
    duck.fn(duck.ctx, context, eta);
  }
}
