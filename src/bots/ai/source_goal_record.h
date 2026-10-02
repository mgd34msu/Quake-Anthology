#ifndef QA_BOT_AI_SOURCE_GOAL_RECORD_H
#define QA_BOT_AI_SOURCE_GOAL_RECORD_H
#include "internal.h"
#include "source_alias.h"

static inline qa_bot_goal bot_ai_goal_record(const bot_ai_state *s,uint32_t offset) {
    const uint8_t *p=s->source_span.data+offset;
    return (qa_bot_goal){.origin=bot_source_vec3_read(p),.area=bot_source_i32_read(p+12),
        .mins=bot_source_vec3_read(p+16),.maxs=bot_source_vec3_read(p+28),
        .entity=bot_source_i32_read(p+40),.number=bot_source_i32_read(p+44),
        .flags=bot_source_i32_read(p+48),.item_info=bot_source_i32_read(p+52)};
}
static inline void bot_ai_goal_record_set(bot_ai_state *s,uint32_t offset,qa_bot_goal goal) {
    uint8_t *p=s->source_span.data+offset;
    bot_source_vec3_write(p,goal.origin);bot_source_i32_write(p+12,goal.area);
    bot_source_vec3_write(p+16,goal.mins);bot_source_vec3_write(p+28,goal.maxs);
    bot_source_i32_write(p+40,goal.entity);bot_source_i32_write(p+44,goal.number);
    bot_source_i32_write(p+48,goal.flags);bot_source_i32_write(p+52,goal.item_info);
}
static inline qa_bot_goal bot_ai_team_goal(const bot_ai_state *s) {
    return bot_ai_goal_record(s,QA_BOT_SOURCE_TEAM_GOAL);
}
static inline void bot_ai_team_goal_set(bot_ai_state *s,qa_bot_goal goal) {
    bot_ai_goal_record_set(s,QA_BOT_SOURCE_TEAM_GOAL,goal);
}
static inline qa_bot_goal bot_ai_alternate_goal(const bot_ai_state *s) {
    return bot_ai_goal_record(s,QA_BOT_SOURCE_ALT_GOAL);
}
static inline qa_bot_goal bot_ai_lead_goal(const bot_ai_state *s) {
    return bot_ai_goal_record(s,QA_BOT_SOURCE_LEAD_GOAL);
}
static inline void bot_ai_goal_record_copy(bot_ai_state *s,uint32_t destination,uint32_t source) {
    memmove(s->source_span.data+destination,s->source_span.data+source,56);
}
static inline void bot_ai_goal_entity_set(bot_ai_state *s,uint32_t offset,int32_t entity) {
    bot_source_i32_write(s->source_span.data+offset+40,entity);
}
static inline void bot_ai_goal_point_set(bot_ai_state *s,uint32_t offset,int32_t entity,
                                        int32_t area,qa_vec3 origin) {
    uint8_t *p=s->source_span.data+offset;
    bot_source_i32_write(p+40,entity);bot_source_i32_write(p+12,area);
    bot_source_vec3_write(p,origin);bot_source_vec3_write(p+16,qa_v3(-8,-8,-8));
    bot_source_vec3_write(p+28,qa_v3(8,8,8));
}
#endif
