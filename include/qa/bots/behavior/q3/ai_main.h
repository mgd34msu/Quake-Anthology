/* Port of src/bots/behavior/q3/ai-main.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_MAIN_H
#define QA_BOTS_BEHAVIOR_Q3_AI_MAIN_H
#include "qa/bots/behavior/q3/ai_context.h"
#include <stdbool.h>
#include <stddef.h>
void qa_remove_color_escape_sequences_bots_a(const char *text, char *out, size_t out_len);
bool qa_bot_team_leader_bots_a(qa_bot_state_bots_a *state, const char *client_name);
typedef struct qa_game_ai_bots_a {
  qa_game_ai_context_bots_a *context;
  bool setup_done;
} qa_game_ai_bots_a;
void qa_game_ai_bots_a_init(qa_game_ai_bots_a *g, qa_game_ai_context_bots_a *ctx);
void qa_game_ai_frame_bots_a(qa_game_ai_bots_a *g, double time);
#endif
