#ifndef QA_BOT_AI_SOURCE_EVENT_STATE_H
#define QA_BOT_AI_SOURCE_EVENT_STATE_H
#include "internal.h"
#include "source_alias.h"

static inline int32_t bot_ai_last_killed_player(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_LAST_KILLED_PLAYER);
}
static inline void bot_ai_last_killed_player_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_LAST_KILLED_PLAYER,value);
}
static inline int32_t bot_ai_last_killed_by(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_LAST_KILLED_BY);
}
static inline void bot_ai_last_killed_by_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_LAST_KILLED_BY,value);
}
static inline int32_t bot_ai_bot_death_type(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_BOT_DEATH_TYPE);
}
static inline void bot_ai_bot_death_type_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_BOT_DEATH_TYPE,value);
}
static inline int32_t bot_ai_enemy_death_type(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_ENEMY_DEATH_TYPE);
}
static inline void bot_ai_enemy_death_type_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_ENEMY_DEATH_TYPE,value);
}
static inline int32_t bot_ai_num_deaths(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_DEATHS);
}
static inline void bot_ai_num_deaths_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_DEATHS,value);
}
static inline int32_t bot_ai_num_kills(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_KILLS);
}
static inline void bot_ai_num_kills_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_KILLS,value);
}
static inline int32_t bot_ai_last_e_flags(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_LAST_EFLAGS);
}
static inline void bot_ai_last_e_flags_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_LAST_EFLAGS,value);
}
static inline int32_t bot_ai_kamikaze_body(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_KAMIKAZE_BODY);
}
static inline void bot_ai_kamikaze_body_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_KAMIKAZE_BODY,value);
}
static inline int32_t bot_ai_num_prox_mines(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_PROX_COUNT);
}
static inline void bot_ai_num_prox_mines_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_PROX_COUNT,value);
}
static inline float bot_ai_killed_enemy_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_KILLED_ENEMY_TIME);
}
static inline void bot_ai_killed_enemy_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_KILLED_ENEMY_TIME,value);
}
static inline bool bot_ai_bot_suicide(const bot_ai_state *s) {
    return bot_source_word_read(s->source_span.data+QA_BOT_SOURCE_BOT_SUICIDE)!=0;
}
static inline void bot_ai_bot_suicide_set(bot_ai_state *s,bool value) {
    bot_source_word_write(s->source_span.data+QA_BOT_SOURCE_BOT_SUICIDE,value?1u:0u);
}
static inline bool bot_ai_enemy_suicide(const bot_ai_state *s) {
    return bot_source_word_read(s->source_span.data+QA_BOT_SOURCE_ENEMY_SUICIDE)!=0;
}
static inline void bot_ai_enemy_suicide_set(bot_ai_state *s,bool value) {
    bot_source_word_write(s->source_span.data+QA_BOT_SOURCE_ENEMY_SUICIDE,value?1u:0u);
}
static inline int32_t bot_ai_entity_event_time(const bot_ai_state *s,uint32_t index) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_EVENTS+index*4);
}
static inline void bot_ai_entity_event_time_set(bot_ai_state *s,uint32_t index,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_EVENTS+index*4,value);
}
static inline int32_t bot_ai_prox_mine(const bot_ai_state *s,uint32_t index) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_PROX_MINES+index*4);
}
static inline void bot_ai_prox_mine_set(bot_ai_state *s,uint32_t index,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_PROX_MINES+index*4,value);
}
#endif
