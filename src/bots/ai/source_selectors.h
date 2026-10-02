#ifndef QA_BOT_AI_SOURCE_SELECTORS_H
#define QA_BOT_AI_SOURCE_SELECTORS_H

#include "internal.h"
#include "source_alias.h"

static inline uint32_t bot_ai_area(const bot_ai_state *s) {
    return bot_source_word_read(s->source_span.data+QA_BOT_SOURCE_AREA);
}
static inline void bot_ai_area_set(bot_ai_state *s,uint32_t value) {
    bot_source_word_write(s->source_span.data+QA_BOT_SOURCE_AREA,value);
}
static inline uint32_t bot_ai_travel_flags(const bot_ai_state *s) {
    return bot_source_word_read(s->source_span.data+QA_BOT_SOURCE_TRAVEL_FLAGS);
}
static inline void bot_ai_travel_flags_set(bot_ai_state *s,uint32_t value) {
    bot_source_word_write(s->source_span.data+QA_BOT_SOURCE_TRAVEL_FLAGS,value);
}
static inline int32_t bot_ai_enemy_number(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_ENEMY);
}
static inline void bot_ai_enemy_number_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_ENEMY,value);
}
static inline uint32_t bot_ai_last_enemy_area(const bot_ai_state *s) {
    return bot_source_word_read(s->source_span.data+QA_BOT_SOURCE_LAST_ENEMY_AREA);
}
static inline void bot_ai_last_enemy_area_set(bot_ai_state *s,uint32_t value) {
    bot_source_word_write(s->source_span.data+QA_BOT_SOURCE_LAST_ENEMY_AREA,value);
}
static inline qa_actor_id bot_ai_enemy_actor(const qa_bots *b,const bot_ai_state *s) {
    int32_t number=bot_ai_enemy_number(s);
    return number>=0?b->services.entity_actor(b->services.context,number):(qa_actor_id){0};
}

#endif
