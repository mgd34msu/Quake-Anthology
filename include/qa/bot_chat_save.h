#ifndef QA_BOT_CHAT_SAVE_H
#define QA_BOT_CHAT_SAVE_H

#include "qa/bot_chat.h"

/* Complete immutable definitions and shared mutable message cooldowns. The
 * enclosing runtime retains asset aliases once across its caches and handles.
 * No script/file, diagnostic, command, test or random callback runs. */
bool qa_bot_chat_asset_capture(const qa_bot_chat_asset *, qa_buffer *, qa_error *);
bool qa_bot_chat_asset_restore_bytes(qa_bytes, qa_bot_chat_asset **, qa_error *);

#endif
