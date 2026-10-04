#ifndef QA_BOT_SOURCE_FUZZY_OPERATIONS_H
#define QA_BOT_SOURCE_FUZZY_OPERATIONS_H
#include "source_fuzzy.h"
#include "qa/bot_library.h"

typedef struct bot_fuzzy_stack {
    struct bot_fuzzy_frame *frames;
    size_t count,capacity;
} bot_fuzzy_stack;

bool bot_fuzzy_find(const bot_fuzzy_config *,qa_bytes,int32_t *,qa_error *);
bool bot_fuzzy_evaluate(const bot_fuzzy_config *,int32_t,const qa_bot_inventory_view *,
    const qa_bot_random_source *,bot_fuzzy_stack *,float *,qa_error *);
bool bot_fuzzy_separator_tree_free(bot_fuzzy_heap *,uint32_t,qa_error *);
bool bot_fuzzy_config_free(bot_fuzzy_config *,qa_error *);
bool bot_fuzzy_scale(bot_fuzzy_config *,qa_bytes,float,qa_error *);
bool bot_fuzzy_scale_range(bot_fuzzy_config *,float,qa_error *);
bool bot_fuzzy_evolve(bot_fuzzy_config *,const qa_bot_random_source *,qa_error *);
/* A reported structural mismatch is a source result, not a service failure. */
typedef struct bot_fuzzy_reporter {
    void *context;
    bool (*report)(void *,const char *,qa_error *);
} bot_fuzzy_reporter;
bool bot_fuzzy_interbreed(const bot_fuzzy_config *,const bot_fuzzy_config *,bot_fuzzy_config *,
    const bot_fuzzy_reporter *,qa_error *);
#endif
