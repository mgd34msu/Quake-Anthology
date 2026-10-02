#ifndef QA_BOT_AI_SOURCE_TIMERS_H
#define QA_BOT_AI_SOURCE_TIMERS_H
#include "internal.h"
#include "source_alias.h"

static inline bool bot_ai_respawn_wait(const bot_ai_state *s) {
    return bot_source_word_read(s->source_span.data+QA_BOT_SOURCE_RESPAWN_WAIT)!=0;
}
static inline void bot_ai_respawn_wait_set(bot_ai_state *s,bool value) {
    bot_source_word_write(s->source_span.data+QA_BOT_SOURCE_RESPAWN_WAIT,value?1u:0u);
}
static inline float bot_ai_respawn_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_RESPAWN_TIME);
}
static inline void bot_ai_respawn_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_RESPAWN_TIME,value);
}
static inline float bot_ai_respawn_chat_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_RESPAWN_CHAT_TIME);
}
static inline void bot_ai_respawn_chat_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_RESPAWN_CHAT_TIME,value);
}

static inline float bot_ai_enemy_visible_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_ENEMY_VISIBLE_TIME);
}
static inline void bot_ai_enemy_visible_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_ENEMY_VISIBLE_TIME,value);
}
static inline float bot_ai_enemy_sight_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_ENEMY_SIGHT_TIME);
}
static inline void bot_ai_enemy_sight_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_ENEMY_SIGHT_TIME,value);
}
static inline float bot_ai_enemy_death_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_ENEMY_DEATH_TIME);
}
static inline void bot_ai_enemy_death_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_ENEMY_DEATH_TIME,value);
}
static inline float bot_ai_last_air_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_LAST_AIR_TIME);
}
static inline void bot_ai_last_air_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_LAST_AIR_TIME,value);
}
static inline float bot_ai_last_chat_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_LAST_CHAT_TIME);
}
static inline void bot_ai_last_chat_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_LAST_CHAT_TIME,value);
}
static inline float bot_ai_weapon_change_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_WEAPON_CHANGE_TIME);
}
static inline void bot_ai_weapon_change_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_WEAPON_CHANGE_TIME,value);
}
static inline float bot_ai_fire_wait_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_FIRE_WAIT_TIME);
}
static inline void bot_ai_fire_wait_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_FIRE_WAIT_TIME,value);
}
static inline float bot_ai_fire_shoot_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_FIRE_SHOOT_TIME);
}
static inline void bot_ai_fire_shoot_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_FIRE_SHOOT_TIME,value);
}
static inline float bot_ai_teleport_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_TELEPORT_TIME);
}
static inline void bot_ai_teleport_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_TELEPORT_TIME,value);
}
static inline float bot_ai_attack_crouch_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_ATTACK_CROUCH_TIME);
}
static inline void bot_ai_attack_crouch_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_ATTACK_CROUCH_TIME,value);
}
static inline float bot_ai_attack_jump_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_ATTACK_JUMP_TIME);
}
static inline void bot_ai_attack_jump_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_ATTACK_JUMP_TIME,value);
}
static inline float bot_ai_attack_strafe_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_ATTACK_STRAFE_TIME);
}
static inline void bot_ai_attack_strafe_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_ATTACK_STRAFE_TIME,value);
}
#endif
