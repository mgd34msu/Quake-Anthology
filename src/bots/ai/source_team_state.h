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
static inline int32_t bot_ai_lead_teammate(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_LEAD_TEAMMATE);
}
static inline void bot_ai_lead_teammate_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_LEAD_TEAMMATE,value);
}
static inline bool bot_ai_ordered(const bot_ai_state *s) {
    return bot_source_word_read(s->source_span.data+QA_BOT_SOURCE_ORDERED)!=0;
}
static inline void bot_ai_ordered_set(bot_ai_state *s,bool value) {
    bot_source_word_write(s->source_span.data+QA_BOT_SOURCE_ORDERED,value?1u:0u);
}
static inline int32_t bot_ai_num_teammates(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_TEAMMATE_COUNT);
}
static inline void bot_ai_num_teammates_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_TEAMMATE_COUNT,value);
}
static inline int32_t bot_ai_red_flag_status(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_RED_FLAG_STATUS);
}
static inline void bot_ai_red_flag_status_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_RED_FLAG_STATUS,value);
}
static inline int32_t bot_ai_blue_flag_status(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_BLUE_FLAG_STATUS);
}
static inline void bot_ai_blue_flag_status_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_BLUE_FLAG_STATUS,value);
}
static inline int32_t bot_ai_neutral_flag_status(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_NEUTRAL_FLAG_STATUS);
}
static inline void bot_ai_neutral_flag_status_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_NEUTRAL_FLAG_STATUS,value);
}
static inline bool bot_ai_flag_status_changed(const bot_ai_state *s) {
    return bot_source_word_read(s->source_span.data+QA_BOT_SOURCE_FLAG_STATUS_CHANGED)!=0;
}
static inline void bot_ai_flag_status_changed_set(bot_ai_state *s,bool value) {
    bot_source_word_write(s->source_span.data+QA_BOT_SOURCE_FLAG_STATUS_CHANGED,value?1u:0u);
}
static inline bool bot_ai_force_orders(const bot_ai_state *s) {
    return bot_source_word_read(s->source_span.data+QA_BOT_SOURCE_FORCE_ORDERS)!=0;
}
static inline void bot_ai_force_orders_set(bot_ai_state *s,bool value) {
    bot_source_word_write(s->source_span.data+QA_BOT_SOURCE_FORCE_ORDERS,value?1u:0u);
}
static inline int32_t bot_ai_flag_carrier(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_FLAG_CARRIER);
}
static inline void bot_ai_flag_carrier_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_FLAG_CARRIER,value);
}
static inline int32_t bot_ai_ctf_strategy(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_CTF_STRATEGY);
}
static inline void bot_ai_ctf_strategy_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_CTF_STRATEGY,value);
}
static inline void bot_ai_team_leader_clear(bot_ai_state *s) {
    s->source_span.data[QA_BOT_SOURCE_TEAM_LEADER]=0;
}
static inline int32_t bot_ai_own_decision_time(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_OWN_DECISION_TIME);
}
static inline void bot_ai_own_decision_time_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_OWN_DECISION_TIME,value);
}
static inline int32_t bot_ai_team_task_preference(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_TEAM_TASK_PREFERENCE);
}
static inline void bot_ai_team_task_preference_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_TEAM_TASK_PREFERENCE,value);
}
#endif
