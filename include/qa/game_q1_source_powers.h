#ifndef QA_GAME_Q1_SOURCE_POWERS_H
#define QA_GAME_Q1_SOURCE_POWERS_H

#include "qa/game_q1.h"

/* Publish removal in the actual player's insertion order before clearing
 * its timed powers. Callback failure preserves completed collection mutations. */
bool qa_q1_player_powers_clear(qa_q1_game *, qa_actor_id, qa_error *);

#endif
