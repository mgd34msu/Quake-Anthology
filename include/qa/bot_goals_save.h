#ifndef QA_BOT_GOALS_SAVE_H
#define QA_BOT_GOALS_SAVE_H

#include "qa/bot_goals.h"
#include "qa/bot_runtime_assets_save.h"
#include "qa/session.h"

/* Complete private handle/map pools. Import the actual MEMORY owner first;
 * this codec restores aliases into those retained allocations. Assets come
 * from the actual runtime or standalone goal registry. The entity table is
 * the owner's qualified immutable
 * map or restored BSP table. Actor hash buckets are rebuilt for the restored
 * namespace; physical source order and all goal numbers remain unchanged.
 * Restore keeps the detached owner's address/services/workspace stable and
 * publishes only after full validation, without source or navigation calls. */
bool qa_bot_goals_save_capture(qa_session *, const qa_bot_goals *, const qa_bot_saved_assets *, qa_buffer *, qa_error *);
bool qa_bot_goals_save_restore(qa_session *, qa_bot_goals *, qa_bytes, const qa_bot_saved_assets *,
                               const qa_entities *, qa_error *);

#endif
