#ifndef QA_BOT_CHAT_SOURCE_INITIAL_H
#define QA_BOT_CHAT_SOURCE_INITIAL_H
#include "qa/bots_allocator.h"

enum {BOT_CHAT_INITIAL_BYTES=4,BOT_CHAT_TYPE_BYTES=44,BOT_CHAT_MESSAGE_BYTES=12};
typedef struct bot_chat_initial {
    qa_bot_memory *memory;
    qa_bot_memory_allocation allocation;
    uint32_t pointer;
    uint32_t *types,*messages;
    size_t type_count,type_capacity,message_count,message_capacity;
} bot_chat_initial;
/* Membership is the parser's actual offset-plus-one typed view table. It does
 * not follow arbitrary raw links to manufacture a new type or message. */
bool bot_chat_initial_bind(qa_bot_memory *,qa_bot_memory_allocation,uint32_t,
    bot_chat_initial *,qa_error *);
bool bot_chat_initial_allocate(qa_bot_memory *,uint32_t,uint32_t,bot_chat_initial *,qa_error *);
void bot_chat_initial_dispose(bot_chat_initial *);
bool bot_chat_initial_free(bot_chat_initial *,qa_error *);
bool bot_chat_initial_first(const bot_chat_initial *,uint32_t *,qa_error *);
bool bot_chat_initial_type(const bot_chat_initial *,uint32_t,qa_bot_memory_span *,qa_error *);
bool bot_chat_initial_message(const bot_chat_initial *,uint32_t,qa_bot_memory_span *,qa_error *);
bool bot_chat_initial_text(const bot_chat_initial *,uint32_t,qa_bytes *,qa_error *);
bool bot_chat_initial_type_add(bot_chat_initial *,uint32_t,qa_bytes,uint32_t *,qa_error *);
bool bot_chat_initial_message_add(bot_chat_initial *,uint32_t,uint32_t,uint32_t *,qa_error *);
bool bot_chat_initial_message_write(bot_chat_initial *,uint32_t,uint32_t,qa_bytes,qa_error *);
bool bot_chat_initial_type_next(const bot_chat_initial *,uint32_t,uint32_t *,qa_error *);
bool bot_chat_initial_type_first(const bot_chat_initial *,uint32_t,uint32_t *,qa_error *);
bool bot_chat_initial_type_count(const bot_chat_initial *,uint32_t,int32_t *,qa_error *);
bool bot_chat_initial_message_next(const bot_chat_initial *,uint32_t,uint32_t *,qa_error *);
bool bot_chat_initial_message_text(const bot_chat_initial *,uint32_t,qa_bytes *,qa_error *);
bool bot_chat_initial_message_time(const bot_chat_initial *,uint32_t,float *,bool,qa_error *);
#endif
