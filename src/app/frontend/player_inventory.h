#ifndef QA_FRONTEND_PLAYER_INVENTORY_H
#define QA_FRONTEND_PLAYER_INVENTORY_H
#include "internal.h"

/* Actual per-seat player-event projection, separate from HUD-owned messages
 * and the application's pending events. Capture requires the frontend lease;
 * import owns copied buffers in an isolated candidate at stable seat addresses.
 * Neither operation consumes events or observes current inventory definitions. */
bool frontend_players_checkpoint(qa_frontend *, qa_buffer *, qa_error *);
bool frontend_players_restore(qa_frontend *, qa_bytes, qa_error *);
#endif
