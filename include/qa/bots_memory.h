#ifndef QA_BOTS_MEMORY_H
#define QA_BOTS_MEMORY_H
#include "qa/bots.h"
#include "qa/source_save.h"

enum { QA_BOT_STATE_SOURCE_BYTES=9088, QA_BOT_GAME_MEMORY_BYTES=262144 };
typedef struct qa_bot_source_memory {
    void *context;
    bool (*allocate)(void *,uint32_t,uint32_t *,qa_error *);
    bool (*read)(void *,uint32_t,void *,uint32_t,qa_error *);
    bool (*write)(void *,uint32_t,const void *,uint32_t,qa_error *);
} qa_bot_source_memory;
typedef struct qa_bot_source_record { uint32_t offset,length; } qa_bot_source_record;
typedef struct qa_bot_source_activation {
    bool inuse,shoot,areas_disabled;
    qa_bot_goal goal;
    float time,start_time,just_used_time;
    int32_t weapon,areas[32],area_count;
    qa_vec3 target,origin;
} qa_bot_source_activation;
enum qa_bot_source_record_offset {
    QA_BOT_SOURCE_INUSE=0,QA_BOT_SOURCE_RESIDUAL=4,QA_BOT_SOURCE_CLIENT=8,QA_BOT_SOURCE_ENTITY=12,
    QA_BOT_SOURCE_PLAYER=16,QA_BOT_SOURCE_LAST_EFLAGS=484,QA_BOT_SOURCE_COMMAND=488,
    QA_BOT_SOURCE_EVENTS=512,QA_BOT_SOURCE_CHARACTER_FILE=4608,QA_BOT_SOURCE_SKILL=4752,
    QA_BOT_SOURCE_TEAM=4756,QA_BOT_SOURCE_THINK_TIME=4904,QA_BOT_SOURCE_ORIGIN=4908,
    QA_BOT_SOURCE_VELOCITY=4920,QA_BOT_SOURCE_PRESENCE=4932,QA_BOT_SOURCE_EYE=4936,
    QA_BOT_SOURCE_AREA=4948,QA_BOT_SOURCE_INVENTORY=4952,QA_BOT_SOURCE_TRAVEL_FLAGS=5976,
    QA_BOT_SOURCE_SETUP_COUNT=6016,QA_BOT_SOURCE_MAP_RESTART=6020,QA_BOT_SOURCE_WALKER=6056,
    QA_BOT_SOURCE_ENTER_TIME=6064,QA_BOT_SOURCE_CHARACTER=6520,QA_BOT_SOURCE_MOVEMENT=6524,
    QA_BOT_SOURCE_GOALS=6528,QA_BOT_SOURCE_CHAT=6532,QA_BOT_SOURCE_WEAPONS=6536,
    QA_BOT_SOURCE_TEAM_GOAL=6624,QA_BOT_SOURCE_ALT_GOAL=6680,QA_BOT_SOURCE_LAST_TEAM_GOAL=6768,
    QA_BOT_SOURCE_LEAD_GOAL=6828,QA_BOT_SOURCE_TEAM_LEADER=6900,QA_BOT_SOURCE_SUBTEAM=6980,
    QA_BOT_SOURCE_FORMATION_TEAMMATE=7016,QA_BOT_SOURCE_FORMATION_GOAL=7060,
    QA_BOT_SOURCE_ACTIVATION_HEAP=7120,QA_BOT_SOURCE_PATROL_FLAGS=9084,
    QA_BOT_SOURCE_FLAGS=5980,QA_BOT_SOURCE_RESPAWN_WAIT=5984,QA_BOT_SOURCE_LAST_HEALTH=5988,
    QA_BOT_SOURCE_LAST_KILLED_PLAYER=5992,QA_BOT_SOURCE_LAST_KILLED_BY=5996,
    QA_BOT_SOURCE_BOT_DEATH_TYPE=6000,QA_BOT_SOURCE_ENEMY_DEATH_TYPE=6004,
    QA_BOT_SOURCE_BOT_SUICIDE=6008,QA_BOT_SOURCE_ENEMY_SUICIDE=6012,
    QA_BOT_SOURCE_ENTER_GAME_CHAT=6024,QA_BOT_SOURCE_DEATHS=6028,QA_BOT_SOURCE_KILLS=6032,
    QA_BOT_SOURCE_REVENGE_ENEMY=6036,QA_BOT_SOURCE_REVENGE_KILLS=6040,
    QA_BOT_SOURCE_LAST_FRAME_HEALTH=6044,QA_BOT_SOURCE_LAST_HIT_COUNT=6048,QA_BOT_SOURCE_CHAT_TO=6052,
    QA_BOT_SOURCE_LOCAL_TIME=6060,QA_BOT_SOURCE_LTG_TIME=6068,QA_BOT_SOURCE_NBG_TIME=6072,
    QA_BOT_SOURCE_RESPAWN_TIME=6076,QA_BOT_SOURCE_RESPAWN_CHAT_TIME=6080,QA_BOT_SOURCE_CHASE_TIME=6084,
    QA_BOT_SOURCE_ENEMY_VISIBLE_TIME=6088,QA_BOT_SOURCE_CHECK_TIME=6092,QA_BOT_SOURCE_STAND_TIME=6096,
    QA_BOT_SOURCE_LAST_CHAT_TIME=6100,QA_BOT_SOURCE_KAMIKAZE_TIME=6104,QA_BOT_SOURCE_INVULNERABILITY_TIME=6108,
    QA_BOT_SOURCE_STAND_FIND_ENEMY_TIME=6112,QA_BOT_SOURCE_ATTACK_STRAFE_TIME=6116,
    QA_BOT_SOURCE_ATTACK_CROUCH_TIME=6120,QA_BOT_SOURCE_ATTACK_CHASE_TIME=6124,
    QA_BOT_SOURCE_ATTACK_JUMP_TIME=6128,QA_BOT_SOURCE_ENEMY_SIGHT_TIME=6132,
    QA_BOT_SOURCE_ENEMY_DEATH_TIME=6136,QA_BOT_SOURCE_ENEMY_POSITION_TIME=6140,
    QA_BOT_SOURCE_DEFEND_AWAY_TIME=6144,QA_BOT_SOURCE_DEFEND_AWAY_RANGE=6148,
    QA_BOT_SOURCE_RUSH_BASE_AWAY_TIME=6152,QA_BOT_SOURCE_ATTACK_AWAY_TIME=6156,
    QA_BOT_SOURCE_HARVEST_AWAY_TIME=6160,QA_BOT_SOURCE_CTF_ROAM_TIME=6164,
    QA_BOT_SOURCE_KILLED_ENEMY_TIME=6168,QA_BOT_SOURCE_ARRIVE_TIME=6172,QA_BOT_SOURCE_LAST_AIR_TIME=6176,
    QA_BOT_SOURCE_TELEPORT_TIME=6180,QA_BOT_SOURCE_CAMP_TIME=6184,QA_BOT_SOURCE_CAMP_RANGE=6188,
    QA_BOT_SOURCE_WEAPON_CHANGE_TIME=6192,QA_BOT_SOURCE_FIRE_WAIT_TIME=6196,
    QA_BOT_SOURCE_FIRE_SHOOT_TIME=6200,QA_BOT_SOURCE_NOT_BLOCKED_TIME=6204,
    QA_BOT_SOURCE_BLOCKED_AVOID_TIME=6208,QA_BOT_SOURCE_PREDICT_OBSTACLES_TIME=6212,
    QA_BOT_SOURCE_PREDICT_OBSTACLES_AREA=6216,QA_BOT_SOURCE_AIM_TARGET=6220,
    QA_BOT_SOURCE_ENEMY_VELOCITY=6232,QA_BOT_SOURCE_ENEMY_ORIGIN=6244,
    QA_BOT_SOURCE_KAMIKAZE_BODY=6256,QA_BOT_SOURCE_PROX_MINES=6260,QA_BOT_SOURCE_PROX_COUNT=6516,
    QA_BOT_SOURCE_ENEMY=6540,QA_BOT_SOURCE_LAST_ENEMY_AREA=6544,QA_BOT_SOURCE_LAST_ENEMY_ORIGIN=6548,
    QA_BOT_SOURCE_WEAPON_NUMBER=6560,QA_BOT_SOURCE_VIEW_ANGLES=6564,
    QA_BOT_SOURCE_IDEAL_VIEW_ANGLES=6576,QA_BOT_SOURCE_VIEW_SPEED=6588,
    QA_BOT_SOURCE_LTG_TYPE=6600,QA_BOT_SOURCE_TEAMMATE=6604,QA_BOT_SOURCE_DECISIONMAKER=6608,
    QA_BOT_SOURCE_ORDERED=6612,QA_BOT_SOURCE_ORDER_TIME=6616,QA_BOT_SOURCE_OWN_DECISION_TIME=6620,
    QA_BOT_SOURCE_ALT_GOAL_REACHED_TIME=6736,QA_BOT_SOURCE_TEAM_MESSAGE_TIME=6740,
    QA_BOT_SOURCE_TEAM_GOAL_TIME=6744,QA_BOT_SOURCE_TEAMMATE_VISIBLE_TIME=6748,
    QA_BOT_SOURCE_TEAM_TASK_PREFERENCE=6752,QA_BOT_SOURCE_LAST_GOAL_DECISIONMAKER=6756,
    QA_BOT_SOURCE_LAST_GOAL_LTG_TYPE=6760,QA_BOT_SOURCE_LAST_GOAL_TEAMMATE=6764,
    QA_BOT_SOURCE_LEAD_TEAMMATE=6824,QA_BOT_SOURCE_LEAD_TIME=6884,
    QA_BOT_SOURCE_LEAD_VISIBLE_TIME=6888,QA_BOT_SOURCE_LEAD_MESSAGE_TIME=6892,QA_BOT_SOURCE_LEAD_BACKUP_TIME=6896,
    QA_BOT_SOURCE_ASK_TEAM_LEADER_TIME=6932,QA_BOT_SOURCE_BECOME_TEAM_LEADER_TIME=6936,
    QA_BOT_SOURCE_GIVE_ORDERS_TIME=6940,QA_BOT_SOURCE_LAST_FLAG_CAPTURE_TIME=6944,
    QA_BOT_SOURCE_TEAMMATE_COUNT=6948,QA_BOT_SOURCE_RED_FLAG_STATUS=6952,
    QA_BOT_SOURCE_BLUE_FLAG_STATUS=6956,QA_BOT_SOURCE_NEUTRAL_FLAG_STATUS=6960,
    QA_BOT_SOURCE_FLAG_STATUS_CHANGED=6964,QA_BOT_SOURCE_FORCE_ORDERS=6968,
    QA_BOT_SOURCE_FLAG_CARRIER=6972,QA_BOT_SOURCE_CTF_STRATEGY=6976,
    QA_BOT_SOURCE_FORMATION_DISTANCE=7012,QA_BOT_SOURCE_FORMATION_ANGLE=7032,
    QA_BOT_SOURCE_FORMATION_DIRECTION=7036,QA_BOT_SOURCE_FORMATION_ORIGIN=7048
};
/* Allocate borrows retained GAME bytes without clearing them. Every accessor
 * addresses that same pool through its real source owner; there is no mirror. */
bool qa_bot_source_record_allocate(const qa_bot_source_memory *,qa_bot_source_record *,qa_error *);
bool qa_bot_source_record_alias(qa_bot_source_record,uint32_t,uint32_t,qa_bot_source_record *,qa_error *);
bool qa_bot_source_record_read(const qa_bot_source_memory *,qa_bot_source_record,void *,qa_error *);
bool qa_bot_source_record_write(const qa_bot_source_memory *,qa_bot_source_record,const void *,qa_error *);
bool qa_bot_source_record_i32(const qa_bot_source_memory *,qa_bot_source_record,uint32_t,int32_t *,bool write,qa_error *);
bool qa_bot_source_record_f32(const qa_bot_source_memory *,qa_bot_source_record,uint32_t,float *,bool write,qa_error *);
bool qa_bot_source_record_bool(const qa_bot_source_memory *,qa_bot_source_record,uint32_t,bool *,bool write,qa_error *);
bool qa_bot_source_record_vec3(const qa_bot_source_memory *,qa_bot_source_record,uint32_t,qa_vec3 *,bool write,qa_error *);
bool qa_bot_source_record_text_read(const qa_bot_source_memory *,qa_bot_source_record,uint32_t,char *,size_t,qa_error *);
bool qa_bot_source_record_text_write(const qa_bot_source_memory *,qa_bot_source_record,uint32_t,uint32_t,const char *,qa_error *);
bool qa_bot_source_record_goal(const qa_bot_source_memory *,qa_bot_source_record,uint32_t,qa_bot_goal *,bool write,qa_error *);
bool qa_bot_source_record_player(const qa_bot_source_memory *,qa_bot_source_record,qa_q3_player *,bool write,qa_error *);
bool qa_bot_source_record_command(const qa_bot_source_memory *,qa_bot_source_record,qa_q3_usercmd *,bool write,qa_error *);
bool qa_bot_source_record_activation(const qa_bot_source_memory *,qa_bot_source_record,uint32_t,
    qa_bot_source_activation *,bool write,qa_error *);
bool qa_bot_source_record_presence(const qa_bot_source_memory *,qa_bot_source_record,int32_t *,bool write,qa_error *);
bool qa_bot_source_record_team_leader(const qa_bot_source_memory *,qa_bot_source_record,const char *,
    bool overflow,bool clean,qa_error *);
bool qa_bot_source_record_subteam(const qa_bot_source_memory *,qa_bot_source_record,const char *,bool clear,qa_error *);
bool qa_bot_source_record_clear(const qa_bot_source_memory *,qa_bot_source_record,bool decision_only,qa_error *);
bool qa_bot_source_record_fields(qa_source_save_io *,const qa_bot_source_memory *,qa_bot_source_record *);
#endif
