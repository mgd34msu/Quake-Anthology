#ifndef QA_BOT_ACTIONS_SAVE_H
#define QA_BOT_ACTIONS_SAVE_H

#include "qa/bot_actions.h"

/* Typed current EA inputs. Standalone restore publishes a freshly constructed
 * source allocation after isolated decode and retains command services. */
bool qa_bot_actions_capture(const qa_bot_actions *, qa_buffer *, qa_error *);
bool qa_bot_actions_restore_bytes(qa_bot_actions *, qa_bytes, qa_error *);
/* Shared-runtime restore allocates inputs in its retained MEMORY owner. */
bool qa_bot_actions_source_capture(const qa_bot_actions *,qa_buffer *,qa_error *);
bool qa_bot_actions_source_restore(qa_bot_actions *,qa_bytes,qa_error *);

#endif
