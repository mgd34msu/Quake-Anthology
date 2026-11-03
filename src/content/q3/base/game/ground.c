/* Port of src/content/q3/base/game/ground.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/ground.h"
#include <string.h>
double qa_q3_ground_number(const qa_actor_id *ground, qa_q3_entity_pool *entities) {
  if (!ground) return 1023;
  qa_q3_game_entity *e = entities->options.records.native_by_actor(entities->options.records.ctx, *ground);
  return e ? e->slot : 1023;
}
void qa_q3_write_ground(qa_q3_game_entity *entity, const qa_actor_id *ground, qa_q3_entity_pool *entities) {
  qa_body_state body = entity->binding.body.read(entity->binding.body.ctx);
  body.ground = ground;
  entity->binding.body.write(entity->binding.body.ctx, body);
  entity->s.ground_entity_num = qa_q3_ground_number(ground, entities);
}
void qa_q3_trace_ground(qa_q3_game_entity *entity, qa_q3_base_actor_hit_kind hit_kind, qa_actor_id hit_actor,
    qa_q3_entity_pool *entities) {
  if (hit_kind == QA_Q3_BASE_HIT_ACTOR) qa_q3_write_ground(entity, &hit_actor, entities);
  else if (hit_kind == QA_Q3_BASE_HIT_WORLD) {
    qa_actor_id id = qa_q3_game_entity_actor(qa_q3_entity_pool_at(entities, 1022)).id;
    qa_q3_write_ground(entity, &id, entities);
  } else qa_q3_write_ground(entity, NULL, entities);
}
void qa_q3_lose_ground(qa_q3_game_entity *entity) {
  qa_body_state body = entity->binding.body.read(entity->binding.body.ctx);
  body.ground = NULL;
  entity->binding.body.write(entity->binding.body.ctx, body);
  entity->s.ground_entity_num = -1;
}
bool qa_q3_rides(qa_q3_game_entity *entity, qa_q3_game_entity *support) {
  qa_body_state body = entity->binding.body.read(entity->binding.body.ctx);
  return body.ground != NULL && qa_same_actor(*body.ground, qa_q3_game_entity_actor(support).id);
}
