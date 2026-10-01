#ifndef QA_GAME_Q1_SOURCE_TRAVEL_H
#define QA_GAME_Q1_SOURCE_TRAVEL_H
#include "qa/game_q1.h"

/* The physical source admits its authored inventory on the existing actor
 * before the selected arsenal replaces source supplies. No arsenal, movement
 * or character ownership changes here. */
bool qa_q1_source_inventory_initialize(qa_q1_game *, qa_actor_id, qa_error *);
/* Prepare the physical source's real clock, cause and next attack ordinal for
 * a spawn overlap. The caller supplies the admitted selected role providers
 * and actual target/body geometry before submitting the direct damage. */
bool qa_q1_source_telefrag_attack(qa_q1_game *, qa_actor_id, qa_attack *, qa_error *);
#endif
