#ifndef QA_BOT_SOURCE_LIBRARY_H
#define QA_BOT_SOURCE_LIBRARY_H
#include "qa/bots.h"

bool bot_ai_context_create(qa_bot_runtime *, const qa_bot_services *, uint32_t, qa_bots **, qa_error *);
bool bot_ai_source_setup_cvars(qa_bots *, qa_error *);
#endif
