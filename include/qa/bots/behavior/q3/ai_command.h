/*
 * Port of src/bots/behavior/q3/ai-command.ts (game/ai_cmd.c). GPL-2.0-or-later.
 * The context embeds this state, so the context is only forward-declared here.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_COMMAND_H
#define QA_BOTS_BEHAVIOR_Q3_AI_COMMAND_H
#include "qa/bots/behavior/library/chat.h"
#include "qa/bots/behavior/library/goals.h"
#include "qa/bots/behavior/q3/ai_state.h"
#include "qa/contracts/math.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_game_ai_context_bots_a qa_game_ai_context_bots_a;
#ifndef QA_GAME_AI_MAX_CLIENTS_CELL_DEFINED
#define QA_GAME_AI_MAX_CLIENTS_CELL_DEFINED
/* One entry of the TS Map<string, number> of function-static maxclients caches;
 * entries keep the Map's insertion order. */
typedef struct qa_game_ai_max_clients_cell_bots_a {
  const char *name; /* static string */
  int64_t value;
} qa_game_ai_max_clients_cell_bots_a;
#endif
/* ai_cmd.c globals and function-static caches, owned by the game AI instance. */
typedef struct qa_game_ai_command_state_bots_a {
  bool not_leader[64];
  qa_game_ai_max_clients_cell_bots_a max_clients[3];
  size_t max_client_count;
} qa_game_ai_command_state_bots_a;
#define QA_BOT_MATCH_FN(name)                                                                      \
  void name(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, const qa_chat_match *match)
void qa_bot_print_team_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_get_item_team_goal_bots_a(qa_game_ai_context_bots_a *ctx, const char *name,
                                      qa_bot_goal_state_bots_a *goal);
bool qa_bot_get_message_team_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                         const char *name, qa_bot_goal_state_bots_a *goal);
double qa_bot_get_time_bots_a(qa_game_ai_context_bots_a *ctx, const qa_chat_match *match);
int64_t qa_find_client_by_name_bots_a(qa_game_ai_context_bots_a *ctx, const char *name);
int64_t qa_find_enemy_by_name_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                     const char *name);
int64_t qa_num_players_on_same_team_bots_a(qa_game_ai_context_bots_a *ctx,
                                           qa_bot_state_bots_a *state);
bool qa_bot_get_patrol_waypoints_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                        const qa_chat_match *match);
bool qa_bot_addressed_to_bot_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                    const qa_chat_match *match);
qa_vec3 qa_bot_gps_to_position_bots_a(qa_game_ai_context_bots_a *ctx, const char *text);
QA_BOT_MATCH_FN(qa_bot_match_help_accompany_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_defend_key_area_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_get_item_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_camp_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_patrol_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_get_flag_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_attack_enemy_base_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_harvest_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_rush_base_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_task_preference_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_return_flag_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_join_subteam_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_leave_subteam_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_which_team_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_check_point_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_formation_space_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_dismiss_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_suicide_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_start_team_leader_ship_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_stop_team_leader_ship_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_who_is_team_leader_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_what_are_you_doing_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_what_is_my_command_bots_a);
double qa_bot_nearest_visible_item_bots_a(qa_game_ai_context_bots_a *ctx,
                                          qa_bot_state_bots_a *state, const char *name,
                                          qa_bot_goal_state_bots_a *goal);
QA_BOT_MATCH_FN(qa_bot_match_where_are_you_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_lead_the_way_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_kill_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_ctf_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_enter_game_bots_a);
QA_BOT_MATCH_FN(qa_bot_match_new_leader_bots_a);
bool qa_bot_match_message_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                 const char *message);
#endif
