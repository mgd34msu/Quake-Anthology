#ifndef QA_BOT_AI_SOURCE_COMBAT_VECTORS_H
#define QA_BOT_AI_SOURCE_COMBAT_VECTORS_H
#include "internal.h"
#include "source_alias.h"

static inline qa_vec3 bot_ai_enemy_origin(const bot_ai_state *state) {
    return bot_source_vec3_read(state->source_span.data+QA_BOT_SOURCE_ENEMY_ORIGIN);
}
static inline void bot_ai_enemy_origin_set(bot_ai_state *state,qa_vec3 value) {
    bot_source_vec3_write(state->source_span.data+QA_BOT_SOURCE_ENEMY_ORIGIN,value);
}
static inline qa_vec3 bot_ai_enemy_velocity(const bot_ai_state *state) {
    return bot_source_vec3_read(state->source_span.data+QA_BOT_SOURCE_ENEMY_VELOCITY);
}
static inline void bot_ai_enemy_velocity_set(bot_ai_state *state,qa_vec3 value) {
    bot_source_vec3_write(state->source_span.data+QA_BOT_SOURCE_ENEMY_VELOCITY,value);
}
static inline qa_vec3 bot_ai_last_enemy_origin(const bot_ai_state *state) {
    return bot_source_vec3_read(state->source_span.data+QA_BOT_SOURCE_LAST_ENEMY_ORIGIN);
}
static inline void bot_ai_last_enemy_origin_set(bot_ai_state *state,qa_vec3 value) {
    bot_source_vec3_write(state->source_span.data+QA_BOT_SOURCE_LAST_ENEMY_ORIGIN,value);
}
static inline qa_vec3 bot_ai_aim_target(const bot_ai_state *state) {
    return bot_source_vec3_read(state->source_span.data+QA_BOT_SOURCE_AIM_TARGET);
}
static inline void bot_ai_aim_target_set(bot_ai_state *state,qa_vec3 value) {
    bot_source_vec3_write(state->source_span.data+QA_BOT_SOURCE_AIM_TARGET,value);
}
#endif
