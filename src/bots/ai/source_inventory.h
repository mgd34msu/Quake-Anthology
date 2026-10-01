#ifndef QA_BOT_AI_SOURCE_INVENTORY_H
#define QA_BOT_AI_SOURCE_INVENTORY_H
#include "internal.h"
#include "qa/bots_inventory.h"

typedef struct bot_source_inventory {
    qa_bots *bots;
    bot_ai_state *state;
} bot_source_inventory;

/* Live setup, frame admission and completed restore qualify this same span. */
static inline qa_bot_inventory_bytes bot_ai_inventory(const bot_ai_state *state) {
    return (qa_bot_inventory_bytes){state->source_span.data+QA_BOT_SOURCE_INVENTORY,
        QA_BOT_INVENTORY_SIZE};
}
static inline int32_t bot_ai_inventory_value(const bot_ai_state *state,int32_t index) {
    return qa_bot_inventory_value(bot_ai_inventory(state),index);
}

qa_bot_inventory_view bot_ai_source_inventory_view(bot_source_inventory *);
qa_bot_inventory_target bot_ai_source_inventory_target(bot_source_inventory *);
bool bot_ai_source_inventory_snapshot(bot_source_inventory *,int32_t [QA_BOT_INVENTORY_SIZE],qa_error *);
bool bot_ai_source_inventory_read(void *,int32_t,int32_t *,qa_error *);
bool bot_ai_source_inventory_write(bot_source_inventory *,int32_t,int32_t,qa_error *);
#endif
