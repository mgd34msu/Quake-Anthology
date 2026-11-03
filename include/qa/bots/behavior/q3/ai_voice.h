/* Port of src/bots/behavior/q3/ai-voice.ts (game/ai_vcmd.c). GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_AI_VOICE_H
#define QA_BOTS_BEHAVIOR_Q3_AI_VOICE_H
#include "qa/bots/behavior/q3/ai_state.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct qa_game_ai_context_bots_a qa_game_ai_context_bots_a;
#define QA_BOT_VOICE_FN(name)                                                                      \
  void name(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state, int64_t client, int64_t mode)
QA_BOT_VOICE_FN(qa_bot_voice_chat_get_flag_bots_a);
QA_BOT_VOICE_FN(qa_bot_voice_chat_offense_bots_a);
QA_BOT_VOICE_FN(qa_bot_voice_chat_defend_bots_a);
QA_BOT_VOICE_FN(qa_bot_voice_chat_defend_flag_bots_a);
QA_BOT_VOICE_FN(qa_bot_voice_chat_patrol_bots_a);
QA_BOT_VOICE_FN(qa_bot_voice_chat_camp_bots_a);
QA_BOT_VOICE_FN(qa_bot_voice_chat_follow_me_bots_a);
QA_BOT_VOICE_FN(qa_bot_voice_chat_follow_flag_carrier_bots_a);
QA_BOT_VOICE_FN(qa_bot_voice_chat_return_flag_bots_a);
QA_BOT_VOICE_FN(qa_bot_voice_chat_start_leader_bots_a);
QA_BOT_VOICE_FN(qa_bot_voice_chat_stop_leader_bots_a);
QA_BOT_VOICE_FN(qa_bot_voice_chat_who_is_leader_bots_a);
QA_BOT_VOICE_FN(qa_bot_voice_chat_want_on_defense_bots_a);
QA_BOT_VOICE_FN(qa_bot_voice_chat_want_on_offense_bots_a);
bool qa_bot_voice_chat_command_bots_a(qa_game_ai_context_bots_a *ctx, qa_bot_state_bots_a *state,
                                      int64_t mode, const char *voice_chat);
#endif
