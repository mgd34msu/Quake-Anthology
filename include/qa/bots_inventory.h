#ifndef QA_BOTS_INVENTORY_H
#define QA_BOTS_INVENTORY_H
#include "qa/bot_library.h"

/* Each successful write changes the retained source array immediately. */
typedef struct qa_bot_inventory_target {
    void *context;
    size_t count;
    bool (*write)(void *,int32_t index,int32_t value,qa_error *);
} qa_bot_inventory_target;
#endif
