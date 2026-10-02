#ifndef QA_BOT_AI_SOURCE_FLAGS_H
#define QA_BOT_AI_SOURCE_FLAGS_H
#include "internal.h"
#include "source_alias.h"
enum { BOT_AI_STRAFE_RIGHT=1,BOT_AI_ATTACKED=2,BOT_AI_AVOID_RIGHT=16,
       BOT_AI_IDEAL_VIEW_SET=32,BOT_AI_FIGHT_SUICIDAL=64 };
static inline bool bot_ai_flag(const bot_ai_state *state,uint32_t flag) {
    return (bot_source_word_read(state->source_span.data+QA_BOT_SOURCE_FLAGS)&flag)!=0;
}
static inline void bot_ai_flag_toggle(bot_ai_state *state,uint32_t flag) {
    uint8_t *bytes=state->source_span.data+QA_BOT_SOURCE_FLAGS;
    bot_source_word_write(bytes,bot_source_word_read(bytes)^flag);
}
static inline void bot_ai_flag_set(bot_ai_state *state,uint32_t flag,bool set) {
    uint8_t *bytes=state->source_span.data+QA_BOT_SOURCE_FLAGS;
    uint32_t value=bot_source_word_read(bytes);
    bot_source_word_write(bytes,set?value|flag:value&~flag);
}
#endif
