#ifndef QA_BOT_SOURCE_ASSETS_HISTORY_H
#define QA_BOT_SOURCE_ASSETS_HISTORY_H
#include "qa/bot_runtime.h"
#include "qa/bots_allocator_checkpoint.h"
typedef struct bot_source_assets_history bot_source_assets_history;
typedef struct bot_source_assets_restore bot_source_assets_restore;
bool bot_source_assets_capture(qa_bot_runtime *, bot_source_assets_history **, qa_error *);
void bot_source_assets_history_destroy(bot_source_assets_history *);
bool bot_source_assets_prepare(qa_bot_runtime *, const bot_source_assets_history *,
                               const qa_bot_memory_prepared *, bot_source_assets_restore **, qa_error *);
void bot_source_assets_finish(bot_source_assets_restore *, bool);
#endif
