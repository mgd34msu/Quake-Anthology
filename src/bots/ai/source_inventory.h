#ifndef QA_BOT_AI_SOURCE_INVENTORY_H
#define QA_BOT_AI_SOURCE_INVENTORY_H
#include "internal.h"

typedef struct bot_source_inventory {
    qa_bots *bots;
    bot_ai_state *state;
} bot_source_inventory;

qa_bot_inventory_view bot_ai_source_inventory_view(bot_source_inventory *);
bool bot_ai_source_inventory_read(void *,int32_t,int32_t *,qa_error *);
bool bot_ai_source_inventory_write(bot_source_inventory *,int32_t,int32_t,qa_error *);
#endif
