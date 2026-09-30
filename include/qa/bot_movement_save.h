#ifndef QA_BOT_MOVEMENT_SAVE_H
#define QA_BOT_MOVEMENT_SAVE_H

#include "qa/bot_movement.h"

/* Import the complete private handle table into the detached owner after its
 * library variable table. The actual navigation service qualifies retained
 * walk edges; no movement, input or source setup callback runs. Derived query
 * workspaces are invalidated after publication. */
bool qa_bot_moves_save_capture(const qa_bot_moves *, qa_buffer *, qa_error *);
bool qa_bot_moves_save_restore(qa_bot_moves *, qa_bytes, qa_error *);

#endif
