#ifndef QA_Q3_SOURCE_OBJECTIVES_H
#define QA_Q3_SOURCE_OBJECTIVES_H

#include "qa/game_q3.h"

bool q3_obelisk_step(qa_q3_game *, qa_actor_id, qa_error *);
bool q3_obelisk_touch(qa_q3_game *, qa_actor_id, qa_actor_id, qa_error *);
/* Restore reattaches the source damage admission to the imported combat owner;
 * it does not run a constructor, source think or publication callback. */
bool q3_obelisk_reconnect(qa_q3_game *, qa_actor_id, qa_error *);

#endif
