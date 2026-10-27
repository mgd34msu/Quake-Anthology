/* Port of src/content/q2/base/monsters/boss-common.ts. GPL-2.0-or-later. */
#include "qa/content/q2/base/monsters/boss_common.h"

#include <math.h>

#include "base_internal.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/gibs.h"

void qa_q2bm_stop_loop(qa_q2m_context *context) {
  q2bm_emit_loop(context->game, context->entity, "", "stop", 0);
}
bool qa_q2bm_boss_check_attack(qa_q2m_context *context, bool hover, bool allow_non_solid) {
  qa_q2f_game *game = context->game;
  qa_q2f_entity *entity = context->entity;
  qa_q2m_state *state = context->state;
  qa_body_state body = q2bm_body(game, entity);
  qa_body_state enemy;
  qa_vec3 eye;
  if (!qa_q2m_enemy_body(context, &enemy) || !qa_q2m_enemy_eye(context, &eye) || !entity->has_enemy) return false;
  if (qa_q2m_health(game, &entity->enemy) > 0) {
    qa_vec3 start = body.origin;
    start.z = body.origin.z + entity->view_height;
    qa_trace_result trace = q2bm_trace(game, start, eye, NULL, &entity->actor.id, (double)(1 | 0x2000000 | 16 | 8));
    qa_q2f_entity *enemy_entity = game->entity(game->ctx, &entity->enemy);
    bool solid_none = enemy_entity && enemy_entity->solid == QA_Q2F_SOLID_NONE;
    if ((trace.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(trace.hit.actor, entity->enemy))
        && (!allow_non_solid || !solid_none || trace.fraction < 1))
      return false;
  }
  double distance = qa_q2m_target_distance(context);
  state->ideal_yaw = qa_vector_to_angles_with_rule(
      qa_sub3_double(enemy.origin, body.origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  if (distance < 80) {
    state->attack_state = state->has_melee ? QA_Q2M_ATTACK_MELEE : QA_Q2M_ATTACK_MISSILE;
    return true;
  }
  if (!state->has_ranged_attack || q2bm_now(game) < state->attack_finished || distance >= 1000) return false;
  double chance = state->stand_ground ? 0.4 : hover ? 0.8 : distance < 500 ? 0.4 : 0.2;
  qa_q2f_entity *enemy_entity = game->entity(game->ctx, &entity->enemy);
  bool solid_none = enemy_entity && enemy_entity->solid == QA_Q2F_SOLID_NONE;
  if (q2bm_rnd(game) < chance || (allow_non_solid && solid_none)) {
    state->attack_state = QA_Q2M_ATTACK_MISSILE;
    state->attack_finished = q2bm_now(game) + 2 * q2bm_rnd(game);
    return true;
  }
  if (state->locomotion == QA_Q2M_LOCO_FLY)
    state->attack_state = q2bm_rnd(game) < 0.3 ? QA_Q2M_ATTACK_SLIDING : QA_Q2M_ATTACK_STRAIGHT;
  return false;
}
bool qa_q2bm_boss_check_attack_default(qa_q2m_context *context) {
  return qa_q2bm_boss_check_attack(context, false, false);
}

static void advance_boss_explosion(qa_q2m_context *context);
void qa_q2bm_boss_explosion_think_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_q2m_context *context = qa_q2m_monsters_context(ctx, entity->actor.id);
  if (context == NULL || context->game != game)
    qa_q2f_error("Boss explosion has no restored source monster context");
  advance_boss_explosion(context);
}
qa_q2f_think qa_q2bm_boss_explosion_think(qa_q2m_monsters *monsters) {
  qa_q2f_think think = {monsters, qa_q2bm_boss_explosion_think_fn};
  return think;
}
void qa_q2bm_with_boss_explosion_callbacks(qa_q2m_definition *out, const qa_q2m_definition *definition,
                                           qa_q2m_monsters *monsters, qa_q2f_named_think *think_storage) {
  *out = *definition;
  size_t count = definition->has_source_callbacks ? definition->source_callbacks.think_count : 0;
  for (size_t i = 0; i < count; i++) think_storage[i] = definition->source_callbacks.think[i];
  think_storage[count].name = "q2:BossExplode";
  think_storage[count].cb = qa_q2bm_boss_explosion_think(monsters);
  out->has_source_callbacks = true;
  out->source_callbacks.think = think_storage;
  out->source_callbacks.think_count = count + 1;
}
static qa_q2f_think resolve_boss_explode(qa_q2f_game *game) {
  qa_q2f_think_result think = game->source_callbacks.think_resolve(game->source_callbacks.ctx, "q2:BossExplode");
  if (!think.has_value) qa_q2f_error("Boss explosion source callback was not registered");
  return think.value;
}
void qa_q2bm_boss_explode(qa_q2m_context *context) {
  qa_q2f_think think = resolve_boss_explode(context->game);
  think.fn(think.ctx, context->entity, context->game);
}
void qa_q2bm_boss_explode_handler(void *ctx, qa_q2m_context *context) {
  (void)ctx;
  qa_q2bm_boss_explode(context);
}
static void advance_boss_explosion(qa_q2m_context *context) {
  qa_q2f_entity *entity = context->entity;
  qa_q2f_game *game = context->game;
  static const qa_vec3 positions[8] = {
      {-24, -24, 0}, {24, 24, 0}, {24, -24, 0}, {-24, 24, 0},
      {-48, -48, 0}, {48, 48, 0}, {-48, 48, 0}, {48, -48, 0}};
  double height = 24 + floor(q2bm_rnd(game) * 16);
  double index = entity->count++;
  if (index == 8) {
    qa_q2bm_stop_loop(context);
    qa_q2m_gib_options plain = {.head = false, .metallic = false, .skinned = false, .upright = false};
    qa_q2m_gib_options metallic = {.head = false, .metallic = true, .skinned = false, .upright = false};
    qa_q2m_gib_options gear = {.head = true, .metallic = true, .skinned = false, .upright = false};
    for (int i = 0; i < 4; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_meat/tris.md2", 500, plain);
    for (int i = 0; i < 8; i++) (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/sm_metal/tris.md2", 500, metallic);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/chest/tris.md2", 500, plain);
    (void)qa_q2m_throw_gib(entity, game, "models/objects/gibs/gear/tris.md2", 500, gear);
    context->state->dead = true; context->state->gibbed = true;
    return;
  }
  qa_vec3 offset = (index == trunc(index) && index >= 0 && index < 8) ? positions[(size_t)index] : q2bm_zero();
  offset.z = height;
  q2bm_emit_effect(game, "q2:explosion1", qa_add3_double(q2bm_body(game, entity).origin, offset), q2bm_zero(), 1, 0);
  qa_q2f_think think = resolve_boss_explode(game);
  game->schedule(game->ctx, entity, 0.1, think);
}
