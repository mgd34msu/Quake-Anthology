#ifndef QA_BOT_ACTIONS_SAVE_H
#define QA_BOT_ACTIONS_SAVE_H

#include "qa/bot_actions.h"

/* Standalone allocator and source alias. Preserves all current and orphan HUNK
 * blocks, publishes only after isolated decode, and retains command services. */
bool qa_bot_actions_capture(const qa_bot_actions *, qa_buffer *, qa_error *);
bool qa_bot_actions_restore_bytes(qa_bot_actions *, qa_bytes, qa_error *);
/* Runtime imports its shared MEMORY section before these allocation aliases. */
bool qa_bot_actions_source_capture(const qa_bot_actions *,qa_buffer *,qa_error *);
bool qa_bot_actions_source_restore(qa_bot_actions *,qa_bytes,qa_error *);

#endif
