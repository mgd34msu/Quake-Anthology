#ifndef QA_GAME_Q3_SAVE_H
#define QA_GAME_Q3_SAVE_H

#include "qa/game_q3.h"
#include "qa/targets.h"

/* Portable native and authored continuation. Shared stores retain one owner.
 * Restore requires an isolated candidate with restored shared actors. A failed
 * restore invalidates that candidate; the save coordinator must discard it. */
bool qa_q3_game_capture(qa_q3_game *, qa_buffer *, qa_error *);
bool qa_q3_game_restore(qa_q3_game *, qa_bytes, qa_error *);
/* Reconnect after shared world/combat/inventory/pickups/targets restoration.
 * Target resolution itself borrows the already restored authored owner. */
bool qa_q3_game_reconnect(qa_q3_game *, qa_error *);
bool qa_q3_game_target_binding(qa_q3_game *, qa_actor_id, qa_target_binding *, qa_error *);

#endif
