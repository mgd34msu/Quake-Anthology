/* Port of src/content/q3/base/game/ground.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_GROUND_H
#define QA_CONTENT_Q3_BASE_GAME_GROUND_H
#include <stdbool.h>
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/game/entities.h"
/* Mirror of ../world.ts ActorTraceResult["hit"] (TS owner unported). */
typedef enum qa_q3_game_trace_hit_kind_mirror { QA_Q3_HIT_NONE, QA_Q3_HIT_ACTOR, QA_Q3_HIT_WORLD } qa_q3_game_trace_hit_kind_mirror;
typedef struct qa_q3_game_trace_hit_mirror { qa_q3_game_trace_hit_kind_mirror kind; qa_actor_id actor; } qa_q3_game_trace_hit_mirror;
double qa_q3_ground_number(const qa_actor_id *ground, qa_q3_entity_pool *entities);
void qa_q3_write_ground(qa_q3_game_entity *entity, const qa_actor_id *ground, qa_q3_entity_pool *entities);
void qa_q3_trace_ground(qa_q3_game_entity *entity, qa_q3_game_trace_hit_mirror hit, qa_q3_entity_pool *entities);
void qa_q3_lose_ground(qa_q3_game_entity *entity);
bool qa_q3_rides(qa_q3_game_entity *entity, qa_q3_game_entity *support);
#endif
