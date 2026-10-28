/* Port of src/app/bootstrap/remote-components.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/remote_components.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_remote_component_view {
  qa_app_remote_component_host host;
  const void **commands; size_t count, cap;
  bool closed;
};
qa_app_remote_component_view *qa_app_remote_component_view_create(qa_app_remote_component_host host) {
  qa_app_remote_component_view *v = calloc(1, sizeof *v);
  if (!v) return NULL;
  v->host = host;
  return v;
}
void qa_app_remote_component_view_destroy(qa_app_remote_component_view *v) {
  if (!v) return;
  qa_app_remote_component_view_close(v);
  free(v->commands); free(v);
}
void qa_app_remote_component_view_queue_command(qa_app_remote_component_view *v, const void *request) {
  if (v->closed) qa_range_error("Remote component view is retired");
  if (v->count == v->cap) { v->cap = v->cap ? v->cap * 2 : 8; v->commands = realloc(v->commands, v->cap * sizeof *v->commands); if (!v->commands) qa_range_error("Out of memory"); }
  v->commands[v->count++] = request;
}
bool qa_app_remote_component_view_pending_commands(qa_app_remote_component_view *v) {
  return v->count != 0 || v->host.clients_pending_commands(v->host.ctx);
}
void qa_app_remote_component_view_prepare_media(qa_app_remote_component_view *v, const void *const *events, size_t event_count, bool music) {
  if (v->closed) qa_range_error("Remote component view is retired");
  const void **commands = v->commands; size_t count = v->count;
  v->commands = NULL; v->count = 0; v->cap = 0;
  for (size_t i = 0; i < count; i++) v->host.clients_dispatch_command(v->host.ctx, commands[i]);
  free(commands);
  if (music) {
    v->host.prepare_audio(v->host.ctx, events, event_count);
  } else {
    const void **filtered = calloc(event_count ? event_count : 1, sizeof *filtered);
    if (!filtered) qa_range_error("Out of memory");
    size_t n = 0;
    for (size_t i = 0; i < event_count; i++) if (v->host.event_is_presentation_owner(v->host.ctx, events[i])) filtered[n++] = events[i];
    v->host.prepare_audio(v->host.ctx, filtered, n);
    free(filtered);
  }
  v->host.prepare_shaders(v->host.ctx);
}
void qa_app_remote_component_view_prepare(qa_app_remote_component_view *v, const void *presentation, const void *const *events, size_t event_count, double frame) {
  if (v->closed) qa_range_error("Remote component view is retired");
  v->host.clients_prepare(v->host.ctx, presentation, events, event_count, frame);
}
void qa_app_remote_component_view_close(qa_app_remote_component_view *v) {
  if (v->closed) return;
  v->closed = true;
  free(v->commands); v->commands = NULL; v->count = 0; v->cap = 0;
  v->host.clients_close(v->host.ctx);
}
