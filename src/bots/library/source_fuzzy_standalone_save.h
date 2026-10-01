#ifndef QA_BOT_SOURCE_FUZZY_STANDALONE_SAVE_H
#define QA_BOT_SOURCE_FUZZY_STANDALONE_SAVE_H
#include "qa/bot_library.h"
#include "qa/source_save.h"

/* An independent resource snapshot carries actual MEMORY first, followed by
 * ordered source aliases. No library or filesystem owner is constructed. */
bool bot_weights_source_fields(qa_source_save_io *,const qa_bot_weights *,qa_bot_weights **);
/* Later configurations from the same captured heap reuse its actual imported
 * MEMORY and ordered aliases; only their genuine parent allocation differs. */
bool bot_weights_source_alias_fields(qa_source_save_io *,qa_bot_weights *,
    const qa_bot_weights *,qa_bot_weights **);
#endif
