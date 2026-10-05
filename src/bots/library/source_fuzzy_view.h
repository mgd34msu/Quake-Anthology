#ifndef QA_BOT_SOURCE_FUZZY_VIEW_H
#define QA_BOT_SOURCE_FUZZY_VIEW_H
#include "qa/bot_library.h"

/* Temporary public metadata only. Evaluation and mutation use raw source
 * records directly, never the projected node/value arrays. */
bool bot_weights_source_view(qa_bot_weights *,qa_error *);
bool bot_weights_source_values_restore(qa_bot_weights *,const qa_bot_weight_value *,size_t,qa_error *);
#endif
