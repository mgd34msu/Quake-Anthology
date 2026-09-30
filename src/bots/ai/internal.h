#ifndef QA_BOT_AI_INTERNAL_H
#define QA_BOT_AI_INTERNAL_H

#include "qa/bots.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum {
    BOT_C_GENDER=1, BOT_C_ATTACK=2, BOT_C_WEAPON_WEIGHTS=3, BOT_C_VIEW_FACTOR=4,
    BOT_C_VIEW_MAX=5, BOT_C_REACTION=6, BOT_C_ACCURACY=7, BOT_C_AIM_SKILL=16,
    BOT_C_CHAT_FILE=21, BOT_C_CHAT_NAME=22, BOT_C_CHAT_CPM=23,
    BOT_C_CROUCHER=36, BOT_C_JUMPER=37, BOT_C_WEAPON_JUMP=38, BOT_C_GRAPPLE=39,
    BOT_C_ITEM_WEIGHTS=40, BOT_C_AGGRESSION=41, BOT_C_SELF_PRESERVATION=42,
    BOT_C_VENGEFUL=43, BOT_C_CAMPER=44, BOT_C_EASY_FRAGGER=45, BOT_C_ALERTNESS=46,
    BOT_C_FIRE_THROTTLE=47, BOT_C_WALKER=48, BOT_C_CHAT_REPLY=35
};
typedef enum bot_team_task {
    BOT_TEAM_NONE, BOT_TEAM_OFFENSE, BOT_TEAM_DEFENSE, BOT_TEAM_RETURN,
    BOT_TEAM_ESCORT, BOT_TEAM_CAMP
} bot_team_task;
typedef struct bot_ai_state {
    qa_bot_view view;
    qa_bot_player player;
    qa_bot_view_state angles;
    qa_movement_command last_command;
    uint32_t character, goals, weapons, chat, movement, area, travel_flags, setup_count;
    int32_t residual_ms, last_health;
    float local_time, walker, long_term_until, nearby_until, stand_until, stand_enemy_time;
    float respawn_time, respawn_chat_time, chase_time, enemy_visible_time, enemy_sight_time;
    float check_time, attack_crouch_time, attack_jump_time, attack_strafe_time, fire_wait_time;
    float fire_until, weapon_change_time, enemy_death_time, state_time, chase_until;
    float teleport_time;
    uint64_t teleport_sequence;
    float last_air_time, last_chat_time, blocked_time, not_blocked_time;
    qa_vec3 enemy_origin, enemy_velocity, last_enemy_origin, aim_target;
    uint32_t last_enemy_area;
    bool respawn_wait, suicidal, strafe_right, team_arena, retired, attacked, chat_pending;
    uint64_t command_sequence;
    uint32_t activation_count;
    struct {
        qa_bot_activation activation;
        qa_bot_decision resume;
        float until;
    } activations[8];
    bot_team_task team_task;
    qa_actor_id team_requester, team_leader;
    float team_task_until;
    qa_vec3 camp_origin;
    char name[128];
} bot_ai_state;
struct qa_bots {
    qa_bot_runtime *runtime;
    qa_bot_services services;
    bot_ai_state **clients;
    uint32_t client_capacity, count;
    uint32_t *actor_clients;
    uint32_t actor_capacity;
    qa_bot_controls controls;
    qa_builtin_actor_snapshot entities, players;
    int32_t local_time_ms, library_residual_ms, scheduled_think_ms;
    float time, regular_update_time;
    uint64_t command_sequence;
    int32_t inventory_scratch[QA_BOT_INVENTORY_SIZE];
    bool busy, checking_spawn, restore_pending;
};
bool bot_ai_fail(qa_error *, const char *);
bot_ai_state *bot_ai_actor(const qa_bots *, qa_actor_id);
bool bot_ai_live(const qa_bots *, qa_actor_id);
bool bot_ai_mutable(qa_bots *, qa_error *);
bool bot_ai_cleanup(qa_bots *, bot_ai_state *, qa_error *);
void bot_ai_schedule(qa_bots *);
bool bot_ai_reset(qa_bots *, bot_ai_state *, qa_error *);
bool bot_ai_think(qa_bots *, bot_ai_state *, float, qa_error *);
bool bot_ai_input(qa_bots *, bot_ai_state *, int32_t time, int32_t elapsed, qa_error *);
bool bot_ai_character_float(qa_bots *, bot_ai_state *, uint32_t, float, float, float *, qa_error *);
bool bot_ai_choose_weapon(qa_bots *, bot_ai_state *, qa_error *);
bool bot_ai_find_enemy(qa_bots *, bot_ai_state *, bool *, qa_error *);
bool bot_ai_attack(qa_bots *, bot_ai_state *, bool moving, qa_error *);
bool bot_ai_enemy_visible(qa_bots *, bot_ai_state *, qa_actor_id, float *, qa_error *);
bool bot_ai_same_team(qa_bots *, bot_ai_state *, qa_actor_id, bool *, qa_error *);
bool bot_ai_target(qa_bots *, bot_ai_state *, qa_actor_id, qa_bot_player *, bool *, qa_error *);
bool bot_ai_retreat(qa_bots *, bot_ai_state *, bool *, qa_error *);
bool bot_ai_move_setup(qa_bots *, bot_ai_state *, qa_error *);
bool bot_ai_attack_move(qa_bots *, bot_ai_state *, qa_error *);
bool bot_ai_console(qa_bots *, bot_ai_state *, qa_error *);
bool bot_ai_messages(qa_bots *, bot_ai_state *, qa_error *);
bool bot_ai_voice(qa_bots *, bot_ai_state *, int32_t channel, const char *, qa_error *);
bool bot_ai_team_goal(qa_bots *, bot_ai_state *, qa_bot_goal *, bool *, qa_error *);
bool bot_ai_carrying(qa_bots *,bot_ai_state *,bool *,qa_error *);
bool bot_ai_decide(qa_bots *, bot_ai_state *, qa_error *);
bool bot_ai_order_active(const bot_ai_state *);
bool bot_ai_order_goal(qa_bots *, bot_ai_state *, qa_bot_goal *, bool *, qa_error *);
bool bot_ai_point_area(qa_bots *, bot_ai_state *, qa_vec3, uint32_t *, qa_error *);
float bot_ai_random(qa_bots *);
qa_vec3 bot_ai_angles(qa_vec3);
#endif
