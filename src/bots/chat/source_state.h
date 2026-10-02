#ifndef QA_BOT_CHAT_SOURCE_STATE_H
#define QA_BOT_CHAT_SOURCE_STATE_H
#include "qa/bots_allocator.h"
enum {CHAT_STATE_BYTES=316,CHAT_CONSOLE_BYTES=276,
    CHAT_GENDER=0,CHAT_CLIENT=4,CHAT_NAME=8,CHAT_MESSAGE=40,CHAT_HANDLE=296,
    CHAT_FIRST=300,CHAT_LAST=304,CHAT_COUNT=308,CHAT_INITIAL=312};
struct qa_bot_chat;
struct qa_bot_chat_system;
struct qa_bot_chat_asset;
uint32_t chat_raw_word(const uint8_t *);
void chat_raw_store(uint8_t *,uint32_t);
bool chat_state_span(const struct qa_bot_chat *,qa_bot_memory_span *,qa_error *);
bool chat_state_get(const struct qa_bot_chat *,uint32_t,uint32_t *,qa_error *);
bool chat_state_set(struct qa_bot_chat *,uint32_t,uint32_t,qa_error *);
bool chat_state_text(const struct qa_bot_chat *,uint32_t,uint32_t,char **,qa_error *);
bool chat_console_span(const struct qa_bot_chat_system *,uint32_t,qa_bot_memory_span *,qa_error *);
bool chat_console_get(const struct qa_bot_chat_system *,uint32_t,uint32_t,uint32_t *,qa_error *);
bool chat_console_set(struct qa_bot_chat_system *,uint32_t,uint32_t,uint32_t,qa_error *);
bool chat_console_heap(struct qa_bot_chat_system *,uint32_t,bool,qa_error *);
bool chat_state_initial(const struct qa_bot_chat *,struct qa_bot_chat_asset **,qa_error *);
#endif
