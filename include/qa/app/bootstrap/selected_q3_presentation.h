/* Port of src/app/bootstrap/selected-q3-presentation.ts. Q3 source /
 * scene / assets owners (q3-client and content/q3 slices) are host
 * fn+ctx; ownership, busy and closed guards are kept here.
 * Async -> synchronous; failures are error strings. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SELECTED_Q3_PRESENTATION_H
#define QA_APP_BOOTSTRAP_SELECTED_Q3_PRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
/* TS SelectedQ3Presentation: content/source/state stay opaque + hardware. */
typedef struct qa_app_selected_q3_source_ref {
  qa_content_id content;
  const void *source; /* Q3SelectedSource mirror */
  const void *state;  /* Q3SourcePresentationState mirror */
  const void *identity; /* TS object identity for owns() */
} qa_app_selected_q3_source_ref;
typedef struct qa_app_selected_q3_host {
  void *ctx;
  const char *(*hardware)(void *ctx, const qa_app_selected_q3_source_ref *source);
  qa_actor_id (*presentation_actor)(void *ctx, const void *presentation);
  double (*presentation_generation)(void *ctx, const void *presentation);
  bool (*source_owns_presentation)(void *ctx, const qa_app_selected_q3_source_ref *source, const void *presentation);
  const char *(*initialize)(void *ctx, const qa_app_selected_q3_source_ref *source, const void *presentation);
  const char *(*prepare_frame)(void *ctx, const qa_app_selected_q3_source_ref *source, const void *presentation, const void *const *events, size_t count, double frame);
  const char *(*close_one)(void *ctx);
  void (*flush_audio)(void *ctx);
} qa_app_selected_q3_host;
typedef struct qa_app_selected_q3_presentation qa_app_selected_q3_presentation;
qa_app_selected_q3_presentation *qa_app_selected_q3_presentation_create(qa_app_selected_q3_host host,
    qa_app_selected_q3_source_ref source, const void *presentation);
void qa_app_selected_q3_presentation_destroy(qa_app_selected_q3_presentation *p);
const char *qa_app_selected_q3_presentation_hardware(qa_app_selected_q3_presentation *p);
bool qa_app_selected_q3_presentation_owns(qa_app_selected_q3_presentation *p, qa_app_selected_q3_source_ref source, const void *presentation);
void qa_app_selected_q3_presentation_prepare(qa_app_selected_q3_presentation *p, qa_app_selected_q3_source_ref source,
                                             const void *const *events, size_t count, double frame);
void qa_app_selected_q3_presentation_close(qa_app_selected_q3_presentation *p);
typedef struct qa_app_selected_q3_presentations qa_app_selected_q3_presentations;
qa_app_selected_q3_presentations *qa_app_selected_q3_presentations_create(qa_app_selected_q3_host host);
void qa_app_selected_q3_presentations_destroy(qa_app_selected_q3_presentations *c);
void qa_app_selected_q3_presentations_retain(qa_app_selected_q3_presentations *c, const void *const *presentations, size_t count);
void qa_app_selected_q3_presentations_prepare(qa_app_selected_q3_presentations *c, const void *const *presentations, size_t count,
                                              const qa_app_selected_q3_source_ref *source, const void *const *events, size_t event_count, double frame);
void qa_app_selected_q3_presentations_close(qa_app_selected_q3_presentations *c);
#endif
