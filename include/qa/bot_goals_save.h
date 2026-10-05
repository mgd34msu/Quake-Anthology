#ifndef QA_BOT_GOALS_SAVE_H
#define QA_BOT_GOALS_SAVE_H

#include "qa/bot_goals.h"
#include "qa/bot_runtime_assets_save.h"
#include "qa/session.h"

/* Rebuild immutable map goals from the held map/navigation, then restore
 * typed mutable handle, goal-stack, avoidance and level-item state. */
bool qa_bot_goals_save_capture(qa_session *, const qa_bot_goals *, const qa_bot_saved_assets *, qa_buffer *, qa_error *);
bool qa_bot_goals_save_restore(qa_session *, qa_bot_goals *, qa_bytes, const qa_bot_saved_assets *,
                               const qa_entities *,qa_bot_navigation *, qa_error *);

#endif
