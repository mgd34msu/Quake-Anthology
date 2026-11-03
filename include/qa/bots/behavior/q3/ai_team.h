/*
 * Port of src/bots/behavior/q3/ai-team.ts (game/ai_team.c). GPL-2.0-or-later.
 *
 * C accommodations: a teammates array holds QA_BOT_TEAMMATE_CAPACITY entries,
 * the TS growing array's bound; indexes past the populated count are the
 * RangeError of the TS element() helper. The context embeds this state, so the
 * context is only forward-declared here.
 */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_TEAM_H
#define QA_BOTS_BEHAVIOR_Q3_AI_TEAM_H
#include "qa/bots/behavior/library/goals.h"
#include "qa/bots/behavior/q3/ai_state.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_BOT_TEAMMATE_CAPACITY 64
typedef struct qa_game_ai_context_bots_a qa_game_ai_context_bots_a;
typedef struct qa_game_ai_task_preference_bots_a {
  char name[36];
  int64_t preference;
} qa_game_ai_task_preference_bots_a;
typedef struct qa_game_ai_team_state_bots_a {
  qa_game_ai_task_preference_bots_a task_preferences[64];
  int64_t num_team_mates_max_clients, sort_team_mates_max_clients, team_orders_max_clients,
      client_from_name_max_clients, client_on_same_team_from_name_max_clients;
} qa_game_ai_team_state_bots_a;
bool qa_bot_valid_team_leader_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
int64_t qa_bot_num_team_mates_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
double qa_bot_client_travel_time_to_goal_bots_a(qa_game_ai_context_bots_a *ctx, int64_t client,
                                                const qa_bot_goal *goal);
int64_t qa_bot_sort_team_mates_by_base_travel_time_bots_a(qa_game_ai_context_bots_a *ctx,
                                                          qa_bot_state_bots_a *state,
                                                          int64_t *teammates, int64_t maximum);
void qa_bot_set_team_mate_task_preference_bots_a(qa_game_ai_context_bots_a *ctx,
                                                 qa_bot_state_bots_a *state, int64_t teammate,
                                                 int64_t preference);
int64_t qa_bot_get_team_mate_task_preference_bots_a(qa_game_ai_context_bots_a *ctx,
                                                    qa_bot_state_bots_a *state, int64_t teammate);
int64_t qa_bot_sort_team_mates_by_task_preference_bots_a(qa_game_ai_context_bots_a *ctx,
                                                         qa_bot_state_bots_a *state,
                                                         int64_t *teammates, int64_t count);
void qa_bot_say_team_order_always_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                         int64_t client);
void qa_bot_say_team_order_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                  int64_t client);
void qa_bot_voice_chat_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                              int64_t client, const char *voice);
void qa_bot_voice_chat_only_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                   int64_t client, const char *voice);
void qa_bot_say_voice_team_order_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                        int64_t client, const char *voice);
void qa_bot_ctf_orders_both_flags_not_at_base_bots_a(qa_game_ai_context_bots_a *ctx,
                                                     qa_bot_state_bots_a *state);
void qa_bot_ctf_orders_flag_not_at_base_bots_a(qa_game_ai_context_bots_a *ctx,
                                               qa_bot_state_bots_a *state);
void qa_bot_ctf_orders_enemy_flag_not_at_base_bots_a(qa_game_ai_context_bots_a *ctx,
                                                     qa_bot_state_bots_a *state);
void qa_bot_ctf_orders_both_flags_at_base_bots_a(qa_game_ai_context_bots_a *ctx,
                                                 qa_bot_state_bots_a *state);
void qa_bot_ctf_orders_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
/* teammates holds count populated entries (TS array length). */
void qa_bot_create_group_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                const int64_t *teammates, int64_t count, int64_t group_size);
void qa_bot_team_orders_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_1fctf_orders_flag_at_center_bots_a(qa_game_ai_context_bots_a *ctx,
                                               qa_bot_state_bots_a *state);
void qa_bot_1fctf_orders_team_has_flag_bots_a(qa_game_ai_context_bots_a *ctx,
                                              qa_bot_state_bots_a *state);
void qa_bot_1fctf_orders_enemy_has_flag_bots_a(qa_game_ai_context_bots_a *ctx,
                                               qa_bot_state_bots_a *state);
void qa_bot_1fctf_orders_enemy_dropped_flag_bots_a(qa_game_ai_context_bots_a *ctx,
                                                   qa_bot_state_bots_a *state);
void qa_bot_1fctf_orders_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_obelisk_orders_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_harvester_orders_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_find_human_team_leader_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
void qa_bot_team_ai_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
#endif
