/* Port of src/content/q2/rerelease/obituary.ts. GPL-2.0-or-later.
 * score is fn+ctx; NULL = TS default (recipient.score += change).
 * mode is the exact TS string. text/args borrow static/state text. */
#ifndef QA_CONTENT_Q2_RERELEASE_OBITUARY_H
#define QA_CONTENT_Q2_RERELEASE_OBITUARY_H
#include <stddef.h>
#include "qa/content/q2/rerelease/players.h"
typedef struct qa_q2rr_obituary { char text[96]; const char *args[2]; size_t arg_count; } qa_q2rr_obituary;
qa_q2rr_obituary qa_q2_rerelease_obituary(qa_q2rr_player_state *victim, qa_q2rr_player_state *attacker,
                                          double cause, qa_q2_mode mode, bool no_point_loss,
                                          void *score_ctx,
                                          void (*score)(void *ctx, qa_q2rr_player_state *recipient, double change));
#endif
