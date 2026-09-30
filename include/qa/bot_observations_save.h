#ifndef QA_BOT_OBSERVATIONS_SAVE_H
#define QA_BOT_OBSERVATIONS_SAVE_H

#include "qa/bot_runtime.h"
#include "qa/session.h"

/* All physical observations and module insertion/hash/free order. Retired
 * actor provenance is remapped through the restored session. The profile must
 * match the detached runtime; no update, invalidation or source frame runs. */
bool qa_bot_observations_capture(qa_session *, const qa_bot_runtime *, qa_buffer *, qa_error *);
bool qa_bot_observations_restore(qa_session *, qa_bot_runtime *, qa_bytes, qa_error *);

#endif
