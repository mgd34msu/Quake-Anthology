/* Port of src/content/q1/foundation/movers.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_FOUNDATION_MOVERS_H
#define QA_CONTENT_Q1_FOUNDATION_MOVERS_H

#include "qa/content/q1/foundation/entity_services.h"

void qa_q1_door_down(qa_q1_services *game, qa_q1_actor *entity);
void qa_q1_door_up(qa_q1_services *game, qa_q1_actor *entity);
void qa_q1_spawn_door(qa_q1_services *game, qa_q1_actor *entity);
void qa_q1_link_doors(qa_q1_services *game);
void qa_q1_spawn_button(qa_q1_services *game, qa_q1_actor *entity);
void qa_q1_spawn_secret_door(qa_q1_services *game, qa_q1_actor *entity);
void qa_q1_spawn_plat(qa_q1_services *game, qa_q1_actor *entity);
void qa_q1_register_mover_callbacks(qa_q1_services *game);

#endif
