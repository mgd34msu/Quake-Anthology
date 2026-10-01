#ifndef QA_BOT_SOURCE_FUZZY_SAVE_H
#define QA_BOT_SOURCE_FUZZY_SAVE_H
#include "source_fuzzy_store.h"

/* The actual library MEMORY is captured/imported before these ordered aliases.
 * Decode creates isolated metadata, never allocations, parser runs or prints. */
bool bot_fuzzy_store_capture(const bot_fuzzy_store *,qa_buffer *,qa_error *);
bool bot_fuzzy_store_restore(qa_bot_library *,qa_bytes,bot_fuzzy_store **,qa_error *);
bool bot_fuzzy_store_reference(const bot_fuzzy_store *,const bot_fuzzy_owned *,size_t *,qa_error *);
bool bot_fuzzy_store_resolve(const bot_fuzzy_store *,size_t,bot_fuzzy_owned **,qa_error *);
/* Every retained separator is qualified, including orphan nodes outside any
 * published configuration's root graph. */
bool bot_fuzzy_heap_topology(bot_fuzzy_heap *,qa_error *);
#endif
