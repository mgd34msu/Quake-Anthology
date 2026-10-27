/* Port of src/bots/behavior/q3/ai-voice.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_VOICE_H
#define QA_BOTS_BEHAVIOR_Q3_AI_VOICE_H
#include "qa/bots/behavior/q3/ai_context.h"
#include <stdbool.h>
#include <stdint.h>
typedef void (*qa_bot_voice_order_fn)(void *hook_ctx, qa_game_ai_context_bots_a *c,
                                      qa_bot_state_bots_a *s, int64_t client, const char *order);
bool qa_bot_voice_chat_command_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                      int64_t mode, const char *voice_chat,
                                      qa_bot_voice_order_fn on_order, void *hook_ctx);
void qa_bot_voice_chat_defend_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                     int64_t client, int64_t mode);
void qa_bot_voice_chat_get_flag_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                       int64_t client, int64_t mode);
#endif
