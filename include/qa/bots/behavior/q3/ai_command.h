/* Port of src/bots/behavior/q3/ai-command.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_COMMAND_H
#define QA_BOTS_BEHAVIOR_Q3_AI_COMMAND_H
#include "qa/bots/behavior/library/chat.h"
#include "qa/bots/behavior/q3/ai_context.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct qa_game_ai_command_state_bots_a {
  int64_t dummy;
} qa_game_ai_command_state_bots_a;
qa_vec3 qa_bot_gps_to_position_bots_a(qa_game_ai_context_bots_a *ctx, const char *text);
int64_t qa_find_client_by_name_bots_a(const char *const *names, size_t count, const char *name);
bool qa_bot_match_message_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                 const char *message,
                                 bool (*dispatch)(void *hook_ctx, qa_game_ai_context_bots_a *c,
                                                  qa_bot_state_bots_a *s, const char *msg),
                                 void *hook_ctx);
void qa_bot_print_team_goal_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state);
#endif
