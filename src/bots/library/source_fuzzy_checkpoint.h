#ifndef QA_BOT_SOURCE_FUZZY_CHECKPOINT_H
#define QA_BOT_SOURCE_FUZZY_CHECKPOINT_H
#include "source_fuzzy_store.h"
#include "qa/bots_allocator_checkpoint.h"

typedef struct bot_fuzzy_history bot_fuzzy_history;
typedef struct bot_fuzzy_history_restore bot_fuzzy_history_restore;
/* This is a same-owner image. MEMORY owns the bytes; the image retains the
 * actual configuration identities and ordered source pointer aliases. */
bool bot_fuzzy_history_capture(qa_bot_library *,bot_fuzzy_history **,qa_error *);
/* Public goal/weapon bindings may use a genuine independent source heap. Its
 * allocator and aliases join this image once, by actual owner identity. */
bool bot_fuzzy_history_include(bot_fuzzy_history *,qa_bot_weights *,qa_error *);
void bot_fuzzy_history_destroy(bot_fuzzy_history *);
bool bot_fuzzy_history_prepare(qa_bot_library *,const bot_fuzzy_history *,
    const qa_bot_memory_prepared *,bot_fuzzy_history_restore **,qa_error *);
void bot_fuzzy_history_finish(bot_fuzzy_history_restore *,bool);
#endif
