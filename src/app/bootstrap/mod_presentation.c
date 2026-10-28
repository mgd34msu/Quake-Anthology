/* Port of src/app/bootstrap/mod-presentation.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/mod_presentation.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
struct qa_app_application_mod_presentation {
  qa_app_mod_presentation_host host;
  qa_provider_id provider;
  bool ready, closed;
};
qa_app_application_mod_presentation *qa_app_application_mod_presentation_create(
    qa_app_mod_presentation_host host, qa_provider_id provider) {
  qa_app_application_mod_presentation *presentation = calloc(1, sizeof *presentation);
  if (presentation == NULL) qa_range_error("Out of memory");
  presentation->host = host;
  presentation->provider = provider;
  return presentation;
}
qa_provider_id qa_app_application_mod_presentation_provider(const qa_app_application_mod_presentation *presentation) {
  return presentation->provider;
}
static void check_killed(qa_app_application_mod_presentation *presentation) {
  if (presentation->host.component_killed == NULL) return;
  const void *error = NULL;
  if (presentation->host.component_killed(presentation->host.ctx, &error))
    qa_range_error("Mod presentation component was killed");
}
bool qa_app_application_mod_presentation_ready(const qa_app_application_mod_presentation *presentation) {
  return presentation->ready && !presentation->closed;
}
const char *qa_app_application_mod_presentation_initialize(qa_app_application_mod_presentation *presentation) {
  if (presentation->closed) return "Mod presentation is closed";
  check_killed(presentation);
  if (presentation->ready) return NULL;
  const char *error = presentation->host.initialize(presentation->host.ctx);
  if (error == NULL) presentation->ready = true;
  return error;
}
const char *qa_app_application_mod_presentation_frame(qa_app_application_mod_presentation *presentation, double sequence) {
  if (presentation->closed) return "Mod presentation is closed";
  check_killed(presentation);
  if (!presentation->ready) return "Mod presentation is not initialized";
  return presentation->host.frame(presentation->host.ctx, sequence);
}
const char *qa_app_application_mod_presentation_receive(qa_app_application_mod_presentation *presentation,
    const void *events, size_t count) {
  if (presentation->closed) return "Mod presentation is closed";
  check_killed(presentation);
  return presentation->host.receive(presentation->host.ctx, events, count);
}
const char *qa_app_application_mod_presentation_key(qa_app_application_mod_presentation *presentation, const void *event) {
  if (presentation->closed) return "Mod presentation is closed";
  check_killed(presentation);
  return presentation->host.key(presentation->host.ctx, event);
}
void qa_app_application_mod_presentation_close(qa_app_application_mod_presentation *presentation) {
  if (presentation == NULL || presentation->closed) return;
  presentation->closed = true;
  const char *error = presentation->host.close(presentation->host.ctx);
  free(presentation);
  if (error != NULL) qa_range_error("%s", error);
}
