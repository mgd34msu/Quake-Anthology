/* Port of src/bots/behavior/q3/ai-chat.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_CHAT_H
#define QA_BOTS_BEHAVIOR_Q3_AI_CHAT_H
#include "qa/bots/behavior/q3/ai_context.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_game_ai_chat_state_bots_a {
  int64_t dummy;
} qa_game_ai_chat_state_bots_a;
int64_t qa_bot_num_active_players_bots_a(qa_game_ai_context_bots_a *ctx, const int64_t *scores,
                                         const bool *connected, size_t count);
bool qa_bot_is_first_in_rankings_bots_a(qa_bot_state_bots_a *state, const int64_t *scores,
                                        size_t count);
bool qa_bot_is_last_in_rankings_bots_a(qa_bot_state_bots_a *state, const int64_t *scores,
                                       size_t count);
bool qa_bot_valid_chat_position_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                       bool visible_enemies);
double qa_bot_chat_time_bots_a(qa_bot_state_bots_a *state);
void qa_bot_initial_chat_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                const char *type);
bool qa_bot_chat_enter_game_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
bool qa_bot_chat_exit_game_bots_a(qa_bot_state_bots_a *state);
bool qa_bot_chat_start_level_bots_a(qa_bot_state_bots_a *state);
bool qa_bot_chat_end_level_bots_a(qa_bot_state_bots_a *state);
bool qa_bot_chat_death_bots_a(qa_bot_state_bots_a *state);
bool qa_bot_chat_kill_bots_a(qa_bot_state_bots_a *state);
bool qa_bot_chat_enemy_suicide_bots_a(qa_bot_state_bots_a *state);
bool qa_bot_chat_hit_talking_bots_a(qa_bot_state_bots_a *state);
bool qa_bot_chat_hit_no_death_bots_a(qa_bot_state_bots_a *state);
bool qa_bot_chat_hit_no_kill_bots_a(qa_bot_state_bots_a *state);
bool qa_bot_chat_random_bots_a(qa_bot_state_bots_a *state);
void qa_bot_chat_test_bots_a(qa_bot_state_bots_a *state);
#endif
