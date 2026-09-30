#ifndef QA_BOT_ACTIONS_SAVE_H
#define QA_BOT_ACTIONS_SAVE_H

#include "qa/bot_actions.h"

/* Detached owner only. Every allocated source input slot and the initialized
 * state are preserved. Restore retains services and publishes after complete
 * validation, without issuing commands or resetting source input. */
bool qa_bot_actions_capture(const qa_bot_actions *, qa_buffer *, qa_error *);
bool qa_bot_actions_restore_bytes(qa_bot_actions *, qa_bytes, qa_error *);

#endif
