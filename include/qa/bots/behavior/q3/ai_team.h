/* Port of src/bots/behavior/q3/ai-team.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_TEAM_H
#define QA_BOTS_BEHAVIOR_Q3_AI_TEAM_H
#include "qa/bots/behavior/q3/ai_context.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_game_ai_team_state_bots_a {
  int64_t task_preferences[64];
  int64_t client_from_name_max_clients;
} qa_game_ai_team_state_bots_a;
bool qa_bot_valid_team_leader_bots_a(qa_bot_state_bots_a *state);
int64_t qa_bot_num_teammates_bots_a(const bool *same_team, size_t count, int64_t self_client);
void qa_bot_set_teammate_task_preference_bots_a(qa_game_ai_team_state_bots_a *team,
                                                int64_t teammate, int64_t preference);
int64_t qa_bot_get_teammate_task_preference_bots_a(qa_game_ai_team_state_bots_a *team,
                                                   int64_t teammate);
void qa_bot_voice_chat_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                              int64_t client, const char *voice);
void qa_bot_voice_chat_only_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                   int64_t client, const char *voice);
void qa_bot_team_ai_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
#endif
