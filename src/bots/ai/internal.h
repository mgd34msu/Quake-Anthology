#ifndef QA_BOT_AI_INTERNAL_H
#define QA_BOT_AI_INTERNAL_H

#include "qa/bots.h"
#include "qa/bots_memory.h"
#include "source_orders.h"
#include "source_team_policy.h"
#include "source_events.h"
#include "source_goal.h"
#include "source_chat.h"
#include "source_setup.h"
#include "source_match.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    BOT_C_GENDER=1, BOT_C_ATTACK=2, BOT_C_WEAPON_WEIGHTS=3, BOT_C_VIEW_FACTOR=4,
    BOT_C_VIEW_MAX=5, BOT_C_REACTION=6, BOT_C_ACCURACY=7, BOT_C_AIM_SKILL=16,
    BOT_C_CHAT_FILE=21, BOT_C_CHAT_NAME=22, BOT_C_CHAT_CPM=23, BOT_C_CHAT_ENTER_EXIT=27,
    BOT_C_CROUCHER=36, BOT_C_JUMPER=37, BOT_C_WEAPON_JUMP=38, BOT_C_GRAPPLE=39,
    BOT_C_ITEM_WEIGHTS=40, BOT_C_AGGRESSION=41, BOT_C_SELF_PRESERVATION=42,
    BOT_C_VENGEFUL=43, BOT_C_CAMPER=44, BOT_C_EASY_FRAGGER=45, BOT_C_ALERTNESS=46,
    BOT_C_FIRE_THROTTLE=47, BOT_C_WALKER=48, BOT_C_CHAT_REPLY=35
};
typedef enum bot_long_term_goal {
    BOT_LTG_NONE, BOT_LTG_TEAM_HELP, BOT_LTG_TEAM_ACCOMPANY, BOT_LTG_DEFEND,
    BOT_LTG_GET_FLAG, BOT_LTG_RUSH_BASE, BOT_LTG_RETURN_FLAG, BOT_LTG_CAMP,
    BOT_LTG_CAMP_ORDER, BOT_LTG_PATROL, BOT_LTG_GET_ITEM, BOT_LTG_KILL,
    BOT_LTG_HARVEST, BOT_LTG_ATTACK_BASE, BOT_LTG_MAKELOVE_UNDER, BOT_LTG_MAKELOVE_ONTOP
} bot_long_term_goal;
typedef enum bot_shutdown_phase {
    BOT_SHUTDOWN_RUNNING, BOT_SHUTDOWN_SESSION, BOT_SHUTDOWN_CHAT,
    BOT_SHUTDOWN_SEND, BOT_SHUTDOWN_DONE, BOT_SHUTDOWN_FAILED
} bot_shutdown_phase;
typedef struct bot_source_goals {
    qa_bot_goal red_flag, blue_flag, neutral_flag, red_obelisk, blue_obelisk, neutral_obelisk;
    int32_t game_type, max_clients, max_bsp_model_index;
} bot_source_goals;
typedef struct bot_ai_view {
    qa_actor_id actor, enemy;
    uint32_t client;
    int32_t source_client, entity;
    qa_mode_id mode;
    qa_bot_decision decision;
    qa_bot_order order;
} bot_ai_view;
typedef struct bot_ai_player {
    bool connected, observer, intermission, dead, grounded, crouched, teleported;
    bool water_jump, grapple_pull, firing, invisible, chatting, carrying_objective;
    qa_actor_id last_attacker, last_victim;
    int32_t deaths, kills, last_damage_cause;
    float air_time, teleport_time;
    uint64_t spawn_sequence, teleport_sequence;
} bot_ai_player;
typedef struct bot_ai_state {
    uint32_t acquired_source_client;
    qa_bot_source_record source_record;
    qa_bot_source_span source_span;
    bot_ai_view view;
    bot_ai_player player;
    uint32_t character, goals, weapons, chat, movement;
    float admitted_skill;
    char *admitted_character;
    char *admitted_name;
    float state_time;
    bool team_arena, retired;
    uint64_t command_sequence;
    bot_source_order_state source_order;
    bot_source_setup_state source_setup;
    bool inuse, counted;
    bot_shutdown_phase shutdown_phase;
    bool shutdown_restart, shutdown_chat_pending;
    char name[128];
} bot_ai_state;
struct qa_bots {
    qa_bot_runtime *runtime;
    qa_bot_services services;
    bot_ai_state **clients;
    bot_ai_state *source_cells[64];
    uint32_t source_clients[64];
    uint32_t client_capacity, count;
    uint32_t *actor_clients;
    uint32_t actor_capacity;
    qa_bot_controls controls;
    qa_builtin_actor_snapshot entities, players;
    int32_t local_time_ms, library_residual_ms, scheduled_think_ms;
    float time, regular_update_time;
    uint64_t command_sequence;
    struct {char name[36];int32_t preference;} team_preferences[64];
    bool not_leader[64];
    bot_source_goals source_goals;
    bot_source_orders_state source_orders;
    bot_source_team_policy_globals source_team_policy;
    bot_source_events_globals source_event_globals;
    bot_source_chat_globals source_chat;
    bot_source_match_globals source_match;
    size_t source_match_exit_depth;
    int32_t inventory_scratch[QA_BOT_INVENTORY_SIZE];
    bool busy, checking_spawn, restore_pending, shutting_down, shutdown_restart;
    qa_actor_id shutdown_actor;
};
bool bot_ai_fail(qa_error *, const char *);
bot_ai_state *bot_ai_actor(const qa_bots *, qa_actor_id);
bool bot_ai_live(const qa_bots *, qa_actor_id);
bool bot_ai_mutable(qa_bots *, qa_error *);
bool bot_ai_cleanup(qa_bots *, bot_ai_state *, qa_error *);
bool bot_ai_source_shutdown_client(qa_bots *, bot_ai_state *, bool, qa_error *);
void bot_ai_source_cell_clear(qa_bots *,bot_ai_state *);
bool bot_ai_schedule(qa_bots *,qa_error *);
bool bot_ai_reset(qa_bots *, bot_ai_state *, qa_error *);
bool bot_ai_think(qa_bots *, bot_ai_state *, float, qa_error *);
bool bot_ai_source_intermission(qa_bots *,bot_ai_state *,bool *,qa_error *);
bool bot_ai_source_observer(qa_bots *,bot_ai_state *,bool *,qa_error *);
bool bot_ai_input(qa_bots *, bot_ai_state *, int32_t time, int32_t elapsed, qa_error *);
bool bot_ai_character_float(qa_bots *, bot_ai_state *, uint32_t, float, float, float *, qa_error *);
bool bot_ai_choose_weapon(qa_bots *, bot_ai_state *, qa_error *);
bool bot_ai_battle_inventory(qa_bots *,bot_ai_state *,int32_t,qa_error *);
bool bot_ai_find_enemy(qa_bots *,bot_ai_state *,int32_t current_enemy,bool *,qa_error *);
bool bot_ai_attack(qa_bots *, bot_ai_state *, bool moving, qa_error *);
bool bot_ai_source_enemy(qa_bots *,const bot_ai_state *);
bool bot_ai_source_enemy_dead(qa_bots *,bot_ai_state *,const qa_bot_entity_info *,bool *,qa_error *);
bool bot_ai_source_aim(qa_bots *,bot_ai_state *,qa_error *);
bool bot_ai_source_check_attack(qa_bots *,bot_ai_state *,qa_error *);
bool bot_ai_enemy_visible(qa_bots *, bot_ai_state *, qa_actor_id, float *, qa_error *);
bool bot_ai_same_team(qa_bots *, bot_ai_state *, qa_actor_id, bool *, qa_error *);
bool bot_ai_target(qa_bots *, bot_ai_state *, qa_actor_id, qa_bot_player *, bool *, qa_error *);
bool bot_ai_retreat(qa_bots *, bot_ai_state *, bool *, qa_error *);
bool bot_ai_chase(qa_bots *,bot_ai_state *,bool *,qa_error *);
bool bot_ai_feeling_bad(qa_bots *,bot_ai_state *,float *,qa_error *);
bool bot_ai_move_setup(qa_bots *, bot_ai_state *, qa_error *);
bool bot_ai_attack_move(qa_bots *, bot_ai_state *, uint32_t, qa_bot_move_result *, qa_error *);
bool bot_ai_console(qa_bots *, bot_ai_state *, qa_error *);
bool bot_ai_messages(qa_bots *, bot_ai_state *, qa_error *);
bool bot_ai_voice(qa_bots *, bot_ai_state *, int32_t channel, const char *, qa_error *);
bool bot_ai_carrying(qa_bots *,bot_ai_state *,bool *,qa_error *);
bool bot_ai_remember_order(qa_bots *,bot_ai_state *,qa_error *);
bool bot_ai_team_status(qa_bots *,bot_ai_state *,qa_error *);
bool bot_ai_source_goals_load(qa_bots *,qa_error *);
bool bot_ai_session_read(qa_bots *,bot_ai_state *,qa_error *);
bool bot_ai_session_write(qa_bots *,bot_ai_state *,qa_error *);
bool bot_ai_source_client(qa_bots *,bot_ai_state *,int32_t *,qa_error *);
qa_actor_id bot_ai_source_actor(qa_bots *,int32_t);
bool bot_ai_client_name(qa_bots *,int32_t,char *,size_t,bool,qa_error *);
bool bot_ai_leader_client_name(qa_bots *,bot_ai_state *,int32_t,qa_error *);
bool bot_ai_easy_name(qa_bots *,int32_t,char *,size_t,qa_error *);
bool bot_ai_decide(qa_bots *, bot_ai_state *, qa_error *);
bool bot_ai_order_active(const bot_ai_state *);
bool bot_ai_order_goal(qa_bots *, bot_ai_state *, qa_bot_goal *, bool *, qa_error *);
bool bot_ai_point_area(qa_bots *, bot_ai_state *, qa_vec3, uint32_t *, qa_error *);
bool bot_ai_random(qa_bots *, float *, qa_error *);
qa_vec3 bot_ai_angles(qa_vec3);
#endif
