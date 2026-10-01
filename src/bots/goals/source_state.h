#ifndef QA_BOT_GOAL_SOURCE_STATE_H
#define QA_BOT_GOAL_SOURCE_STATE_H
#include "qa/bot_goals.h"
#include "qa/bots_allocator.h"

enum {BOT_GOAL_STATE_BYTES=2516};
typedef enum bot_goal_record_word {
    BOT_GOAL_CONFIG_POINTER=0,BOT_GOAL_INDEX_POINTER=4,BOT_GOAL_CLIENT=8,
    BOT_GOAL_LAST_AREA=12,BOT_GOAL_STACK_TOP=464
} bot_goal_record_word;
typedef struct bot_goal_record {
    qa_bot_memory *memory;
    qa_bot_memory_allocation allocation;
} bot_goal_record;
bool bot_goal_record_bind(qa_bot_memory *,qa_bot_memory_allocation,bot_goal_record *,qa_error *);
bool bot_goal_record_allocate(qa_bot_memory *,int32_t,bot_goal_record *,qa_error *);
bool bot_goal_record_word_read(const bot_goal_record *,bot_goal_record_word,uint32_t *,qa_error *);
bool bot_goal_record_integer_read(const bot_goal_record *,bot_goal_record_word,int32_t *,qa_error *);
bool bot_goal_record_word_write(const bot_goal_record *,bot_goal_record_word,uint32_t,qa_error *);
bool bot_goal_record_goal_read(const bot_goal_record *,int32_t,qa_bot_goal *,qa_error *);
bool bot_goal_record_goal_write(const bot_goal_record *,int32_t,const qa_bot_goal *,qa_error *);
bool bot_goal_record_goal_bytes_write(const bot_goal_record *,int32_t,qa_bytes,qa_error *);
bool bot_goal_record_goal_bytes_read(const bot_goal_record *,int32_t,qa_bytes *,qa_error *);
bool bot_goal_record_goal_span(const bot_goal_record *,int32_t,qa_bot_memory_span *,qa_error *);
bool bot_goal_record_avoid_read(const bot_goal_record *,int32_t,qa_bot_avoid_goal *,qa_error *);
bool bot_goal_record_avoid_write(const bot_goal_record *,int32_t,const qa_bot_avoid_goal *,qa_error *);
bool bot_goal_record_reset(const bot_goal_record *,bool,qa_error *);
bool bot_goal_record_state_read(const bot_goal_record *,qa_bot_goal_state *,qa_error *);
bool bot_goal_record_state_write(const bot_goal_record *,const qa_bot_goal_state *,qa_error *);
#endif
