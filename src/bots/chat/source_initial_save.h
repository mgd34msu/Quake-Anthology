#ifndef QA_BOT_CHAT_SOURCE_INITIAL_SAVE_H
#define QA_BOT_CHAT_SOURCE_INITIAL_SAVE_H
#include "qa/source_save.h"
#include "qa/bot_chat.h"
#include "qa/bots_allocator.h"
/* A supplied MEMORY has already been imported. A null MEMORY encodes/imports
 * a standalone owner before resolving any allocation or retained PC alias. */
bool bot_chat_initial_asset_fields(qa_source_save_io *,const qa_bot_chat_asset *,
    qa_bot_library *,qa_bot_memory *,qa_bot_chat_asset **);
#endif
