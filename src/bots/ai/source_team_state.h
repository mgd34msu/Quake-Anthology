#ifndef QA_BOT_AI_SOURCE_TEAM_STATE_H
#define QA_BOT_AI_SOURCE_TEAM_STATE_H
#include "internal.h"
#include "source_alias.h"

static inline int32_t bot_ai_decisionmaker(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_DECISIONMAKER);
}
static inline void bot_ai_decisionmaker_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_DECISIONMAKER,value);
}
static inline int32_t bot_ai_long_term_goal(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_LTG_TYPE);
}
static inline void bot_ai_long_term_goal_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_LTG_TYPE,value);
}
static inline int32_t bot_ai_teammate(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_TEAMMATE);
}
static inline void bot_ai_teammate_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_TEAMMATE,value);
}
static inline bool bot_ai_ordered(const bot_ai_state *s) {
    return bot_source_word_read(s->source_span.data+QA_BOT_SOURCE_ORDERED)!=0;
}
static inline void bot_ai_ordered_set(bot_ai_state *s,bool value) {
    bot_source_word_write(s->source_span.data+QA_BOT_SOURCE_ORDERED,value?1u:0u);
}
#endif
