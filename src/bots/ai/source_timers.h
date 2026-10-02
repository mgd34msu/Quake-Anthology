#ifndef QA_BOT_AI_SOURCE_TIMERS_H
#define QA_BOT_AI_SOURCE_TIMERS_H
#include "internal.h"
#include "source_alias.h"

static inline bool bot_ai_respawn_wait(const bot_ai_state *s) {
    return bot_source_word_read(s->source_span.data+QA_BOT_SOURCE_RESPAWN_WAIT)!=0;
}
static inline float bot_ai_enter_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_ENTER_TIME);
}
static inline float bot_ai_think_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_THINK_TIME);
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
static inline float bot_ai_long_term_until(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_LTG_TIME);
}
static inline void bot_ai_long_term_until_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_LTG_TIME,value);
}
static inline float bot_ai_nearby_until(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_NBG_TIME);
}
static inline void bot_ai_nearby_until_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_NBG_TIME,value);
}
static inline float bot_ai_stand_until(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_STAND_TIME);
}
static inline void bot_ai_stand_until_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_STAND_TIME,value);
}
static inline float bot_ai_stand_enemy_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_STAND_FIND_ENEMY_TIME);
}
static inline void bot_ai_stand_enemy_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_STAND_FIND_ENEMY_TIME,value);
}
static inline float bot_ai_chase_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_CHASE_TIME);
}
static inline void bot_ai_chase_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_CHASE_TIME,value);
}
static inline float bot_ai_check_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_CHECK_TIME);
}
static inline void bot_ai_check_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_CHECK_TIME,value);
}
static inline float bot_ai_not_blocked_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_NOT_BLOCKED_TIME);
}
static inline void bot_ai_not_blocked_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_NOT_BLOCKED_TIME,value);
}
static inline float bot_ai_blocked_avoid_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_BLOCKED_AVOID_TIME);
}
static inline void bot_ai_blocked_avoid_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_BLOCKED_AVOID_TIME,value);
}
static inline float bot_ai_predict_obstacles_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_PREDICT_OBSTACLES_TIME);
}
static inline void bot_ai_predict_obstacles_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_PREDICT_OBSTACLES_TIME,value);
}
static inline int32_t bot_ai_predict_obstacles_area(const bot_ai_state *s) {
    return bot_source_i32_read(s->source_span.data+QA_BOT_SOURCE_PREDICT_OBSTACLES_AREA);
}
static inline void bot_ai_predict_obstacles_area_set(bot_ai_state *s,int32_t value) {
    bot_source_i32_write(s->source_span.data+QA_BOT_SOURCE_PREDICT_OBSTACLES_AREA,value);
}
static inline float bot_ai_order_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_ORDER_TIME);
}
static inline void bot_ai_order_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_ORDER_TIME,value);
}
static inline float bot_ai_team_message_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_TEAM_MESSAGE_TIME);
}
static inline void bot_ai_team_message_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_TEAM_MESSAGE_TIME,value);
}
static inline float bot_ai_team_goal_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_TEAM_GOAL_TIME);
}
static inline void bot_ai_team_goal_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_TEAM_GOAL_TIME,value);
}
static inline float bot_ai_teammate_visible_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_TEAMMATE_VISIBLE_TIME);
}
static inline void bot_ai_teammate_visible_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_TEAMMATE_VISIBLE_TIME,value);
}
static inline float bot_ai_formation_distance(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_FORMATION_DISTANCE);
}
static inline void bot_ai_formation_distance_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_FORMATION_DISTANCE,value);
}
static inline float bot_ai_arrive_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_ARRIVE_TIME);
}
static inline void bot_ai_arrive_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_ARRIVE_TIME,value);
}
static inline float bot_ai_defend_away_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_DEFEND_AWAY_TIME);
}
static inline void bot_ai_defend_away_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_DEFEND_AWAY_TIME,value);
}
static inline float bot_ai_harvest_away_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_HARVEST_AWAY_TIME);
}
static inline void bot_ai_harvest_away_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_HARVEST_AWAY_TIME,value);
}
static inline float bot_ai_attack_away_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_ATTACK_AWAY_TIME);
}
static inline void bot_ai_attack_away_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_ATTACK_AWAY_TIME,value);
}
static inline float bot_ai_rush_base_away_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_RUSH_BASE_AWAY_TIME);
}
static inline void bot_ai_rush_base_away_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_RUSH_BASE_AWAY_TIME,value);
}
static inline float bot_ai_lead_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_LEAD_TIME);
}
static inline void bot_ai_lead_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_LEAD_TIME,value);
}
static inline float bot_ai_lead_visible_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_LEAD_VISIBLE_TIME);
}
static inline void bot_ai_lead_visible_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_LEAD_VISIBLE_TIME,value);
}
static inline float bot_ai_lead_message_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_LEAD_MESSAGE_TIME);
}
static inline void bot_ai_lead_message_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_LEAD_MESSAGE_TIME,value);
}
static inline float bot_ai_lead_backup_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_LEAD_BACKUP_TIME);
}
static inline void bot_ai_lead_backup_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_LEAD_BACKUP_TIME,value);
}
static inline float bot_ai_last_flag_capture_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_LAST_FLAG_CAPTURE_TIME);
}
static inline void bot_ai_last_flag_capture_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_LAST_FLAG_CAPTURE_TIME,value);
}
static inline float bot_ai_ask_team_leader_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_ASK_TEAM_LEADER_TIME);
}
static inline void bot_ai_ask_team_leader_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_ASK_TEAM_LEADER_TIME,value);
}
static inline float bot_ai_become_team_leader_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_BECOME_TEAM_LEADER_TIME);
}
static inline void bot_ai_become_team_leader_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_BECOME_TEAM_LEADER_TIME,value);
}
static inline float bot_ai_give_orders_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_GIVE_ORDERS_TIME);
}
static inline void bot_ai_give_orders_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_GIVE_ORDERS_TIME,value);
}
static inline float bot_ai_ctf_roam_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_CTF_ROAM_TIME);
}
static inline void bot_ai_ctf_roam_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_CTF_ROAM_TIME,value);
}
static inline float bot_ai_alternate_goal_reached_time(const bot_ai_state *s) {
    return bot_source_f32_read(s->source_span.data+QA_BOT_SOURCE_ALT_GOAL_REACHED_TIME);
}
static inline void bot_ai_alternate_goal_reached_time_set(bot_ai_state *s,float value) {
    bot_source_f32_write(s->source_span.data+QA_BOT_SOURCE_ALT_GOAL_REACHED_TIME,value);
}
#endif
