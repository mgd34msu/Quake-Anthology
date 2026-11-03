/* Port of src/content/q3/base/game/ground.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_GROUND_H
#define QA_CONTENT_Q3_BASE_GAME_GROUND_H
#include <stdbool.h>
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/world.h"
double qa_q3_ground_number(const qa_actor_id *ground, qa_q3_entity_pool *entities);
void qa_q3_write_ground(qa_q3_game_entity *entity, const qa_actor_id *ground, qa_q3_entity_pool *entities);
/* TS traceGround(entity, hit: ActorTraceResult["hit"], entities): hit_actor
 * is read only for QA_Q3_BASE_HIT_ACTOR. */
void qa_q3_trace_ground(qa_q3_game_entity *entity, qa_q3_base_actor_hit_kind hit_kind, qa_actor_id hit_actor,
    qa_q3_entity_pool *entities);
void qa_q3_lose_ground(qa_q3_game_entity *entity);
bool qa_q3_rides(qa_q3_game_entity *entity, qa_q3_game_entity *support);
#endif
