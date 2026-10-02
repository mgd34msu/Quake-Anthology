#ifndef QA_BOT_AI_SOURCE_VIEW_H
#define QA_BOT_AI_SOURCE_VIEW_H
#include "internal.h"
#include "source_alias.h"

/* Setup, live-frame admission and restore qualify this same GAME allocation. */
static inline int32_t bot_ai_weapon_number(const bot_ai_state *state) {
    return bot_source_i32_read(state->source_span.data+QA_BOT_SOURCE_WEAPON_NUMBER);
}
static inline void bot_ai_weapon_number_set(bot_ai_state *state,int32_t value) {
    bot_source_i32_write(state->source_span.data+QA_BOT_SOURCE_WEAPON_NUMBER,value);
}
static inline qa_vec3 bot_ai_view_angles(const bot_ai_state *state) {
    return bot_source_vec3_read(state->source_span.data+QA_BOT_SOURCE_VIEW_ANGLES);
}
static inline qa_vec3 bot_ai_view_ideal(const bot_ai_state *state) {
    return bot_source_vec3_read(state->source_span.data+QA_BOT_SOURCE_IDEAL_VIEW_ANGLES);
}
static inline qa_vec3 bot_ai_view_velocity(const bot_ai_state *state) {
    return bot_source_vec3_read(state->source_span.data+QA_BOT_SOURCE_VIEW_SPEED);
}
static inline void bot_ai_view_angles_set(bot_ai_state *state,qa_vec3 value) {
    bot_source_vec3_write(state->source_span.data+QA_BOT_SOURCE_VIEW_ANGLES,value);
}
static inline void bot_ai_view_ideal_set(bot_ai_state *state,qa_vec3 value) {
    bot_source_vec3_write(state->source_span.data+QA_BOT_SOURCE_IDEAL_VIEW_ANGLES,value);
}
static inline void bot_ai_view_ideal_axis_set(bot_ai_state *state,uint32_t axis,float value) {
    bot_source_f32_write(state->source_span.data+QA_BOT_SOURCE_IDEAL_VIEW_ANGLES+axis*4,value);
}
static inline void bot_ai_view_delta(bot_ai_state *state,const int32_t delta[3],bool add) {
    qa_bot_view_state value={.angles=bot_ai_view_angles(state)};
    qa_bot_view_delta(&value,delta,add);
    bot_ai_view_angles_set(state,value.angles);
}
static inline void bot_ai_view_prepare(bot_ai_state *state) {
    qa_vec3 ideal=bot_ai_view_ideal(state);
    if(ideal.x>180) bot_ai_view_ideal_axis_set(state,0,ideal.x-360);
}
static inline void bot_ai_view_change(bot_ai_state *state,float factor,float maximum,
                                      float elapsed,bool challenge) {
    qa_bot_view_state value={.angles=bot_ai_view_angles(state),
        .ideal=bot_ai_view_ideal(state),.velocity=bot_ai_view_velocity(state)};
    qa_bot_change_view(&value,factor,maximum,elapsed,challenge);
    bot_source_f32_write(state->source_span.data+QA_BOT_SOURCE_VIEW_ANGLES,value.angles.x);
    bot_source_f32_write(state->source_span.data+QA_BOT_SOURCE_VIEW_ANGLES+4,value.angles.y);
    bot_ai_view_ideal_axis_set(state,0,value.ideal.x);
    bot_ai_view_ideal_axis_set(state,1,value.ideal.y);
    bot_source_f32_write(state->source_span.data+QA_BOT_SOURCE_VIEW_SPEED,value.velocity.x);
    bot_source_f32_write(state->source_span.data+QA_BOT_SOURCE_VIEW_SPEED+4,value.velocity.y);
}
#endif
