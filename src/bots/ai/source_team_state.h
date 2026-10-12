#ifndef QA_BOT_AI_SOURCE_TEAM_STATE_H
#define QA_BOT_AI_SOURCE_TEAM_STATE_H
#include "internal.h"
#include "qa/binary.h"
#include "source_alias.h"

static inline int32_t bot_ai_decisionmaker(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_DECISIONMAKER);
}
static inline void bot_ai_decisionmaker_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_DECISIONMAKER, (uint32_t)value);
}
static inline int32_t bot_ai_long_term_goal(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_LTG_TYPE);
}
static inline void bot_ai_long_term_goal_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_LTG_TYPE, (uint32_t)value);
}
static inline int32_t bot_ai_teammate(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_TEAMMATE);
}
static inline void bot_ai_teammate_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_TEAMMATE, (uint32_t)value);
}
static inline int32_t bot_ai_lead_teammate(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_LEAD_TEAMMATE);
}
static inline void bot_ai_lead_teammate_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_LEAD_TEAMMATE, (uint32_t)value);
}
static inline bool bot_ai_ordered(const bot_ai_state *s) {
    return qa_load_u32le(s->source_span.data+QA_BOT_SOURCE_ORDERED)!=0;
}
static inline void bot_ai_ordered_set(bot_ai_state *s,bool value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_ORDERED,value?1u:0u);
}
static inline int32_t bot_ai_num_teammates(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_TEAMMATE_COUNT);
}
static inline void bot_ai_num_teammates_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_TEAMMATE_COUNT, (uint32_t)value);
}
static inline int32_t bot_ai_red_flag_status(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_RED_FLAG_STATUS);
}
static inline void bot_ai_red_flag_status_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_RED_FLAG_STATUS, (uint32_t)value);
}
static inline int32_t bot_ai_blue_flag_status(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_BLUE_FLAG_STATUS);
}
static inline void bot_ai_blue_flag_status_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_BLUE_FLAG_STATUS, (uint32_t)value);
}
static inline int32_t bot_ai_neutral_flag_status(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_NEUTRAL_FLAG_STATUS);
}
static inline void bot_ai_neutral_flag_status_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_NEUTRAL_FLAG_STATUS, (uint32_t)value);
}
static inline bool bot_ai_flag_status_changed(const bot_ai_state *s) {
    return qa_load_u32le(s->source_span.data+QA_BOT_SOURCE_FLAG_STATUS_CHANGED)!=0;
}
static inline void bot_ai_flag_status_changed_set(bot_ai_state *s,bool value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_FLAG_STATUS_CHANGED,value?1u:0u);
}
static inline bool bot_ai_force_orders(const bot_ai_state *s) {
    return qa_load_u32le(s->source_span.data+QA_BOT_SOURCE_FORCE_ORDERS)!=0;
}
static inline void bot_ai_force_orders_set(bot_ai_state *s,bool value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_FORCE_ORDERS,value?1u:0u);
}
static inline int32_t bot_ai_flag_carrier(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_FLAG_CARRIER);
}
static inline void bot_ai_flag_carrier_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_FLAG_CARRIER, (uint32_t)value);
}
static inline int32_t bot_ai_ctf_strategy(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_CTF_STRATEGY);
}
static inline void bot_ai_ctf_strategy_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_CTF_STRATEGY, (uint32_t)value);
}
static inline void bot_ai_team_leader_clear(bot_ai_state *s) {
    s->source_span.data[QA_BOT_SOURCE_TEAM_LEADER]=0;
}
static inline int32_t bot_ai_own_decision_time(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_OWN_DECISION_TIME);
}
static inline void bot_ai_own_decision_time_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_OWN_DECISION_TIME, (uint32_t)value);
}
static inline int32_t bot_ai_team_task_preference(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_TEAM_TASK_PREFERENCE);
}
static inline void bot_ai_team_task_preference_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_TEAM_TASK_PREFERENCE, (uint32_t)value);
}
static inline int32_t bot_ai_patrol_flags(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_PATROL_FLAGS);
}
static inline void bot_ai_patrol_flags_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_PATROL_FLAGS, (uint32_t)value);
}
#endif
