/* Port of src/bots/behavior/q3/ai-orders.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_ORDERS_H
#define QA_BOTS_BEHAVIOR_Q3_AI_ORDERS_H
#include "qa/bots/behavior/q3/ai_context.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Output must hold strlen(text)+1. */
void qa_bot_clean_string_bots_a(const char *text, char *out, size_t out_len);
const char *qa_stristr_bots_a(const char *text, const char *part);
bool qa_team_play_is_on_bots_a(qa_game_ai_context_bots_a *ctx);
int64_t qa_bot_team_bots_a(qa_bot_state_bots_a *state, int64_t team_value);
int64_t qa_bot_opposite_team_bots_a(int64_t team);
bool qa_bot_same_team_bots_a(qa_game_ai_context_bots_a *ctx, int64_t team_a, int64_t team_b,
                             int64_t entity);
int64_t qa_bot_ctf_carrying_flag_bots_a(qa_bot_state_bots_a *state);
bool qa_bot_1fctf_carrying_flag_bots_a(qa_bot_state_bots_a *state);
bool qa_bot_harvester_carrying_cubes_bots_a(qa_bot_state_bots_a *state, int64_t team);
void qa_bot_team_goals_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                              bool retreat);
void qa_bot_remember_last_ordered_task_bots_a(qa_bot_state_bots_a *state, const char *task);
#endif
