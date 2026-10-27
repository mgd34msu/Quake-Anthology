/* Port of src/bots/behavior/rerelease/chat-text.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_CHAT_TEXT_H
#define QA_BOTS_BEHAVIOR_RERELEASE_CHAT_TEXT_H
#include "qa/bots/behavior/rerelease/rng.h"
/* lookup returns NULL for TS null. Result: malloc'd string, caller frees (NULL on OOM). */
char *qa_q1_bot_chat_text(const char *locstring, const char *(*lookup)(void *ctx, const char *key), void *lookup_ctx, qa_bot_random random);
#endif
