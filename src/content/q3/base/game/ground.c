/* Port of src/content/q3/base/game/ground.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/ground.h"
#include <string.h>
static int actor_equal(qa_actor_id a, qa_actor_id b) {
  return a.slot == b.slot && a.generation == b.generation && a.token == b.token && a.session.token == b.session.token;
}
double qa_q3_ground_number(const qa_actor_id *ground, qa_q3_entity_pool *entities) {
  if (!ground) return 1023;
  qa_q3_game_entity *e = entities->options.records.native_by_actor(entities->options.records.ctx, *ground);
  return e ? e->slot : 1023;
}
void qa_q3_write_ground(qa_q3_game_entity *entity, const qa_actor_id *ground, qa_q3_entity_pool *entities) {
  entity->binding.body.has_ground = ground != NULL; if (ground) entity->binding.body.ground = *ground;
  entity->s.ground_entity_num = qa_q3_ground_number(ground, entities);
}
void qa_q3_trace_ground(qa_q3_game_entity *entity, qa_q3_game_trace_hit_mirror hit, qa_q3_entity_pool *entities) {
  if (hit.kind == QA_Q3_HIT_ACTOR) qa_q3_write_ground(entity, &hit.actor, entities);
  else if (hit.kind == QA_Q3_HIT_WORLD) { qa_actor_id id = qa_q3_game_entity_actor(qa_q3_entity_pool_at(entities, 1022)).id; qa_q3_write_ground(entity, &id, entities); }
  else qa_q3_write_ground(entity, NULL, entities);
}
void qa_q3_lose_ground(qa_q3_game_entity *entity) { entity->binding.body.has_ground = false; entity->s.ground_entity_num = -1; }
bool qa_q3_rides(qa_q3_game_entity *entity, qa_q3_game_entity *support) {
  return entity->binding.body.has_ground && actor_equal(entity->binding.body.ground, qa_q3_game_entity_actor(support).id);
}
