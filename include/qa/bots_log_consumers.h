#ifndef QA_BOTS_LOG_CONSUMERS_H
#define QA_BOTS_LOG_CONSUMERS_H
#include "qa/bot_chat.h"
#include "qa/bot_log.h"

/* These source operations borrow the existing log owner. Capture/import never
 * call them. The resource itself remains retained across each file callback. */
bool qa_bot_character_dump(qa_bot_library *, qa_bot_log *, const qa_bot_character *, qa_error *);
bool qa_bot_chat_system_log_bind(qa_bot_chat_system *, qa_bot_log *, qa_error *);
bool qa_bot_chat_dump_asset(qa_bot_log *, qa_bot_chat_asset *, qa_error *);
bool qa_bot_chat_log_initial(qa_bot_log *, qa_bot_chat_asset *, qa_error *);
#endif
