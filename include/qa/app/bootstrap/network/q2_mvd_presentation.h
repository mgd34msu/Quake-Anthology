/* Port of src/app/bootstrap/network/q2-mvd-presentation.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q2_MVD_PRESENTATION_H
#define QA_APP_BOOTSTRAP_NETWORK_Q2_MVD_PRESENTATION_H
#include "qa/app/bootstrap/network/q2_layout.h"
#include "qa/network/q2/mvd_playback.h"
#include "qa/world/collision/index.h"
qa_app_q2_layout qa_app_q2_mvd_layout(qa_q2_mvd_profile profile);
typedef struct qa_app_q2_mvd_visibility_host {
  void *ctx;
  qa_q2_mvd_profile profile;
  qa_shared_scene_queries *(*scene)(void *ctx);
  const char *(*model_path)(void *ctx, double index); /* NULL = undefined */
} qa_app_q2_mvd_visibility_host;
typedef struct qa_app_q2_mvd_visibility_state qa_app_q2_mvd_visibility_state;
/* NULL only on OOM. The returned landed visibility vtable borrows
 * the state; destroy the state after the playback. */
qa_app_q2_mvd_visibility_state *qa_app_q2_mvd_visibility_create(qa_app_q2_mvd_visibility_host host);
qa_q2_mvd_visibility qa_app_q2_mvd_visibility_vtable(qa_app_q2_mvd_visibility_state *state);
void qa_app_q2_mvd_visibility_destroy(qa_app_q2_mvd_visibility_state *state);
#endif
