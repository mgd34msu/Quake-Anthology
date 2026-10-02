#ifndef QA_BOT_CHAT_SOURCE_HISTORY_H
#define QA_BOT_CHAT_SOURCE_HISTORY_H
#include "qa/bot_runtime.h"
#include "qa/bots_allocator_checkpoint.h"
typedef struct bot_chat_history bot_chat_history;
typedef struct bot_chat_history_restore bot_chat_history_restore;
bool bot_chat_history_capture(qa_bot_runtime *,bot_chat_history **,qa_error *);
void bot_chat_history_destroy(bot_chat_history *);
bool bot_chat_history_prepare(qa_bot_runtime *,const bot_chat_history *,
    const qa_bot_memory_prepared *,bot_chat_history_restore **,qa_error *);
void bot_chat_history_finish(bot_chat_history_restore *,bool);
#endif
