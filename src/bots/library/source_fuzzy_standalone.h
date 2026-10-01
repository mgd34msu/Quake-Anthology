#ifndef QA_BOT_SOURCE_FUZZY_STANDALONE_H
#define QA_BOT_SOURCE_FUZZY_STANDALONE_H
#include "qa/bot_library.h"

/* Public detached metadata import owns genuine standalone source allocations.
 * It creates no library, runtime, filesystem services or cached configuration. */
bool bot_weights_standalone_create(const qa_bot_weights_view *,qa_bot_weights **,qa_error *);
#endif
