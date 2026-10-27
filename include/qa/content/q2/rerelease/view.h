/*
 * Port of src/content/q2/rerelease/view.ts. GPL-2.0-or-later.
 * Base q2BuildView/q2DamageFeedback (TS owner:
 * src/content/q2/base/player/view.ts, unported) are supplied as
 * fn+ctx in qa_q2rr_base_view_ops; NULL ops abort via qa_q2rr_error.
 */
#ifndef QA_CONTENT_Q2_RERELEASE_VIEW_H
#define QA_CONTENT_Q2_RERELEASE_VIEW_H
#include "qa/content/q2/rerelease/players.h"
typedef struct qa_q2rr_feedback_result { double flashes, pain_index; } qa_q2rr_feedback_result;
typedef struct qa_q2rr_base_view_ops {
  void *ctx;
  qa_q2rr_feedback_result (*damage_feedback)(void *ctx, qa_q2rr_player_context *c, double pain_index);
  qa_q2rr_player_view (*build_view)(void *ctx, qa_q2rr_player_context *c, double flashes, bool intermission);
} qa_q2rr_base_view_ops;
extern qa_q2rr_base_view_ops qa_q2rr_base_view; /* set by base port/test */
qa_q2rr_feedback_result qa_q2_rerelease_damage_feedback(qa_q2rr_player_context *c, qa_q2rr_player_extra_state *extra, double pain_index);
qa_q2rr_player_view qa_q2_rerelease_build_view(qa_q2rr_player_context *c, qa_q2rr_player_extra_state *extra, double flashes, bool intermission);
void qa_q2_rerelease_client_animation(qa_q2rr_player_context *c, qa_q2rr_player_extra_state *extra);
#endif
