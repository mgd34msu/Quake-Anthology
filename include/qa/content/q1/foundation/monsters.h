/* Port of src/content/q1/foundation/monsters.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_FOUNDATION_MONSTERS_H
#define QA_CONTENT_Q1_FOUNDATION_MONSTERS_H

#include "qa/content/q1/foundation/entity_services.h"

double qa_q1_path_end_time(double time);
void qa_q1_set_monster_route(qa_q1_services *game, qa_q1_actor *entity, const qa_actor_id *goal, double pause_until);
void qa_q1_spawn_monster(qa_q1_services *game, qa_q1_actor *entity);
void qa_q1_register_monster_callbacks(qa_q1_services *game);

#endif
