/* Port of src/app/bootstrap/presentation-state.ts. Owner lifecycle
 * and capacity accounting are ported here; persistent event coding
 * and local-media registration ride the host below.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_PRESENTATION_STATE_H
#define QA_APP_BOOTSTRAP_PRESENTATION_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/presentation.h"
#define QA_APP_PRESENTATION_EVENT_CAPACITY 65536
/* TS LocalPresentationMedia view. */
typedef struct qa_app_local_presentation_media {
  const char *kind; /* borrowed cue kind */
  qa_content_id content;
  const char *path; /* borrowed */
} qa_app_local_presentation_media;
typedef struct qa_app_presentation_state_host {
  void *ctx;
  /* Persistent event/restoration coding; borrowed error or NULL. */
  const char *(*capture)(void *ctx, void *out_state);
  const char *(*restore)(void *ctx, const void *state);
} qa_app_presentation_state_host;
typedef struct qa_app_presentation_state qa_app_presentation_state;
qa_app_presentation_state *qa_app_presentation_state_create(qa_app_presentation_state_host host);
void qa_app_presentation_state_destroy(qa_app_presentation_state *state);
/* Owner activation. Aborts with the TS Error text on duplicate or
 * mismatched restored owners. On success *out_token is the owner. */
void qa_app_presentation_state_activate(qa_app_presentation_state *state,
    qa_provider_id provider, qa_content_id content, bool restoring, bool component_persistent,
    qa_presentation_owner *out_token);
void qa_app_presentation_state_retire(qa_app_presentation_state *state, qa_provider_id provider);
bool qa_app_presentation_state_is_active(const qa_app_presentation_state *state, qa_provider_id provider);
/* Aborts when a restored owner was never activated. */
void qa_app_presentation_state_require_restored(qa_app_presentation_state *state);
/* Event accounting towards the capacity error. */
void qa_app_presentation_state_admit_event(qa_app_presentation_state *state);
void qa_app_presentation_state_drain_events(qa_app_presentation_state *state, size_t count);
size_t qa_app_presentation_state_pending_events(const qa_app_presentation_state *state);
void qa_app_presentation_state_close(qa_app_presentation_state *state);
#endif
