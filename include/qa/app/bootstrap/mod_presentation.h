/* Port of src/app/bootstrap/mod-presentation.ts. One QVM cgame
 * component presentation; QVM/UI host execution rides landed
 * compat/qvm services behind the host below. The TS
 * ComponentPresentationMediaRequest re-export is the landed
 * qa_component_presentation_media_request (contracts/presentation.h).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_MOD_PRESENTATION_H
#define QA_APP_BOOTSTRAP_MOD_PRESENTATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/presentation.h"
typedef struct qa_app_mod_presentation_host {
  void *ctx;
  /* QVM instance + lifecycle; borrowed errors. */
  const char *(*initialize)(void *ctx);
  const char *(*frame)(void *ctx, double sequence);
  const char *(*receive)(void *ctx, const void *events, size_t count);
  const char *(*key)(void *ctx, const void *event);
  const char *(*close)(void *ctx);
  bool (*component_killed)(void *ctx, const void **error_out); /* may be NULL */
} qa_app_mod_presentation_host;
typedef struct qa_app_application_mod_presentation qa_app_application_mod_presentation;
qa_app_application_mod_presentation *qa_app_application_mod_presentation_create(
    qa_app_mod_presentation_host host, qa_provider_id provider);
qa_provider_id qa_app_application_mod_presentation_provider(const qa_app_application_mod_presentation *presentation);
bool qa_app_application_mod_presentation_ready(const qa_app_application_mod_presentation *presentation);
const char *qa_app_application_mod_presentation_initialize(qa_app_application_mod_presentation *presentation);
const char *qa_app_application_mod_presentation_frame(qa_app_application_mod_presentation *presentation, double sequence);
const char *qa_app_application_mod_presentation_receive(qa_app_application_mod_presentation *presentation,
    const void *events, size_t count);
const char *qa_app_application_mod_presentation_key(qa_app_application_mod_presentation *presentation, const void *event);
void qa_app_application_mod_presentation_close(qa_app_application_mod_presentation *presentation);
#endif
