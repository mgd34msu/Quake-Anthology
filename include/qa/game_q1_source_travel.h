#ifndef QA_GAME_Q1_SOURCE_TRAVEL_H
#define QA_GAME_Q1_SOURCE_TRAVEL_H
#include "qa/game_q1.h"

/* The physical source admits its authored inventory on the existing actor
 * before the selected arsenal replaces source supplies. No arsenal, movement
 * or character ownership changes here. */
bool qa_q1_source_inventory_initialize(qa_q1_game *, qa_actor_id, qa_error *);
/* Create the admitted client's actual timed teledeath trigger. The caller
 * delivers initial overlapping contacts to that entity through Source touch. */
bool qa_q1_source_spawn_teledeath(qa_q1_game *, qa_actor_id, qa_actor_id *, qa_error *);
#endif
