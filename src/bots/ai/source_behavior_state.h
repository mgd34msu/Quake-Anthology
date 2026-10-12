#ifndef QA_BOT_AI_SOURCE_BEHAVIOR_STATE_H
#define QA_BOT_AI_SOURCE_BEHAVIOR_STATE_H

#include "internal.h"
#include "qa/binary.h"
#include "source_alias.h"

static inline int32_t bot_ai_chat_to(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_CHAT_TO);
}
static inline void bot_ai_chat_to_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_CHAT_TO, (uint32_t)value);
}
static inline int32_t bot_ai_last_frame_health(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_LAST_FRAME_HEALTH);
}
static inline void bot_ai_last_frame_health_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_LAST_FRAME_HEALTH, (uint32_t)value);
}
static inline int32_t bot_ai_last_hit_count(const bot_ai_state *s) {
    return qa_load_i32le(s->source_span.data+QA_BOT_SOURCE_LAST_HIT_COUNT);
}
static inline void bot_ai_last_hit_count_set(bot_ai_state *s,int32_t value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_LAST_HIT_COUNT, (uint32_t)value);
}
static inline bool bot_ai_enter_game_chat(const bot_ai_state *s) {
    return qa_load_u32le(s->source_span.data+QA_BOT_SOURCE_ENTER_GAME_CHAT)!=0;
}
static inline void bot_ai_enter_game_chat_set(bot_ai_state *s,bool value) {
    qa_store_u32le(s->source_span.data+QA_BOT_SOURCE_ENTER_GAME_CHAT,value?1u:0u);
}
static inline float bot_ai_defend_away_range(const bot_ai_state *s) {
    return qa_load_f32le(s->source_span.data+QA_BOT_SOURCE_DEFEND_AWAY_RANGE);
}
static inline void bot_ai_defend_away_range_set(bot_ai_state *s,float value) {
    qa_store_f32le(s->source_span.data+QA_BOT_SOURCE_DEFEND_AWAY_RANGE,value);
}
static inline float bot_ai_camp_time(const bot_ai_state *s) {
    return qa_load_f32le(s->source_span.data+QA_BOT_SOURCE_CAMP_TIME);
}
static inline void bot_ai_camp_time_set(bot_ai_state *s,float value) {
    qa_store_f32le(s->source_span.data+QA_BOT_SOURCE_CAMP_TIME,value);
}

#endif
