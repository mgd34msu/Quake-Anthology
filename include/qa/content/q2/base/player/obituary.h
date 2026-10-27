/* Port of src/content/q2/base/player/obituary.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_PLAYER_OBITUARY_H
#define QA_CONTENT_Q2_BASE_PLAYER_OBITUARY_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q2/base/player/types.h"

/* TS q2Obituary. attacker NULL is TS null. score NULL uses the TS
 * default (recipient->score += change). Writes the message to out. */
void qa_q2_obituary(qa_q2_base_player_state *victim, qa_q2_base_player_state *attacker,
                    double means, bool deathmatch, bool coop,
                    void *score_ctx, void (*score)(void *ctx, qa_q2_base_player_state *recipient, double change),
                    char *out, size_t out_size);

#endif
