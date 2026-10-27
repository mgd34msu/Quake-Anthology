/* Port of src/content/q2/rerelease/monsters/boss.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/monsters/boss.h"

#include "rrm_internal.h"

qa_vec3 qa_q2rm_random_body_point(qa_q2f_entity *entity, qa_q2f_game *game) {
  qa_body_state body = q2rm_body(game, entity);
  qa_vec3 point = {
    body.origin.x + body.bounds.min.x + q2rm_rnd(game) * (body.bounds.max.x - body.bounds.min.x),
    body.origin.y + body.bounds.min.y + q2rm_rnd(game) * (body.bounds.max.y - body.bounds.min.y),
    body.origin.z + body.bounds.min.z + q2rm_rnd(game) * (body.bounds.max.z - body.bounds.min.z),
  };
  return point;
}
void qa_q2rm_boss_explode_think_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  (void)ctx;
  qa_q2f_entity *owner = game->entity(game->ctx, entity->has_owner ? &entity->owner : NULL);
  if (!owner || strcmp(owner->model ? owner->model : "", entity->model ? entity->model : "") != 0) {
    game->remove(game->ctx, entity);
    return;
  }
  q2rm_emit_effect(game, (int32_t)entity->view_height % 3 == 0 ? "q2:explosion1" : "q2:explosion1-nl",
                   qa_q2rm_random_body_point(owner, game), q2rm_zero(), 1, 0);
  entity->view_height++;
  game->schedule(game->ctx, entity, 0.05 + q2rm_rnd(game) * 0.15, qa_q2rm_boss_explode_think());
}
qa_q2f_think qa_q2rm_boss_explode_think(void) {
  qa_q2f_think think = {NULL, qa_q2rm_boss_explode_think_fn};
  return think;
}
void qa_q2rm_boss_explode(qa_q2m_context *context) {
  qa_q2f_entity *entity = context->entity;
  qa_q2f_game *game = context->game;
  if (q2rm_has(entity->spawnflags, 1 << 16)) return;
  qa_q2f_named_think named[1];
  named[0].name = "BossExplode_think";
  named[0].cb = qa_q2rm_boss_explode_think();
  qa_q2f_callback_definitions definitions;
  memset(&definitions, 0, sizeof definitions);
  definitions.think = named; definitions.think_count = 1;
  game->source_callbacks.register_defs(game->source_callbacks.ctx, definitions);
  qa_q2f_entity *exploder = game->create(game->ctx, "boss_exploder", NULL);
  exploder->owner = entity->actor.id; exploder->has_owner = true;
  exploder->model = entity->model; exploder->visible = false; exploder->view_height = 0;
  game->schedule(game->ctx, exploder, 0.075 + q2rm_rnd(game) * 0.175, qa_q2rm_boss_explode_think());
}
void qa_q2rm_boss_explode_handler(void *ctx, qa_q2m_context *context) {
  (void)ctx;
  qa_q2rm_boss_explode(context);
}
