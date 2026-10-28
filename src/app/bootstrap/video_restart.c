/* Port of src/app/bootstrap/video-restart.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/video_restart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
struct qa_app_video_restart {
  qa_app_native_renderer *renderer;
  qa_app_video_restart_host host;
  bool has_request;
  char kind[8];
  bool render_worker;
  bool has_source; qa_command_context source;
  bool running, closed;
  qa_command_buffer *commands;
  double completed;
};
qa_app_video_restart *qa_app_video_restart_create(qa_app_native_renderer *renderer, qa_app_video_restart_host host) {
  qa_app_video_restart *v = calloc(1, sizeof *v);
  if (!v) return NULL;
  v->renderer = renderer; v->host = host;
  return v;
}
void qa_app_video_restart_destroy(qa_app_video_restart *v) {
  if (!v) return;
  qa_app_video_restart_close(v);
  free(v);
}
bool qa_app_video_restart_pending(const qa_app_video_restart *v) { return v->has_request || v->running; }
double qa_app_video_restart_generation(const qa_app_video_restart *v) { return v->completed; }
void qa_app_video_restart_request(qa_app_video_restart *v, const char *kind, const qa_command_context *source) {
  if (v->closed) qa_range_error("Video restart owner has retired");
  if (kind == NULL) {
    qa_sdl_window *window = qa_app_native_renderer_window(v->renderer);
    kind = window && qa_sdl_window_backend(window) == QA_SDL_BACKEND_CPU ? "cpu" : "gl";
  }
  snprintf(v->kind, sizeof v->kind, "%s", kind);
  bool worker = false;
  v->render_worker = (v->host.render_worker && v->host.render_worker(v->host.ctx, &worker)) ? worker
                         : qa_app_native_renderer_render_worker(v->renderer);
  v->has_source = source != NULL;
  if (source) v->source = *source;
  v->has_request = true;
}
static void vid_restart_handler(void *ctx, qa_command_invocation *command) {
  qa_app_video_restart *v = ctx;
  const qa_command_origin *origin = &command->source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  if (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT) {
    v->host.print(v->host.ctx, "vid_restart is a local client command.\n", &command->source);
    return;
  }
  /* Landed argv excludes the command name (TS command.args). */
  const char *kind = command->argc > 0 ? command->argv[0] : NULL;
  if (command->argc > 1 || (kind != NULL && strcmp(kind, "cpu") != 0 && strcmp(kind, "gl") != 0)) {
    v->host.print(v->host.ctx, "vid_restart [cpu|gl]\n", &command->source);
    return;
  }
  qa_app_video_restart_request(v, kind, &command->source);
}
void qa_app_video_restart_register(qa_app_video_restart *v, qa_command_buffer *commands) {
  if (v->commands != NULL) qa_range_error("Video restart command already registered");
  static const char *const examples[] = {"vid_restart", "vid_restart cpu", "vid_restart gl"};
  qa_command_documentation doc = {"Restart the client renderer while retaining the current game.", "vid_restart [cpu|gl]", examples, 3, NULL, 0};
  if (!qa_command_buffer_register_engine(commands, "vid_restart", vid_restart_handler, v, &doc))
    qa_range_error("Video restart command belongs to another owner");
  v->commands = commands;
}
bool qa_app_video_restart_drain(qa_app_video_restart *v) {
  if (v->running) qa_range_error("Video restart is already preparing");
  qa_app_capture *capture = v->host.capture ? v->host.capture(v->host.ctx) : NULL;
  if (v->closed || !v->has_request || (capture && qa_app_capture_pending_readback(capture))) return false;
  char kind[8]; snprintf(kind, sizeof kind, "%s", v->kind);
  bool has_source = v->has_source; qa_command_context source = v->source;
  bool render_worker = v->render_worker;
  v->has_request = false;
  v->running = true;
  qa_app_prepared_renderer_restart *surface = NULL;
  qa_app_prepared_video_presentation presentation; memset(&presentation, 0, sizeof presentation);
  bool has_presentation = false;
  qa_sdl_window *previous_window = qa_app_native_renderer_window(v->renderer);
  bool input_published = false;
  const char *error = NULL;
  if (capture) qa_app_capture_drain(capture);
  if (error == NULL) {
    surface = qa_app_native_renderer_prepare_restart(v->renderer, kind, true, render_worker);
    if (surface == NULL) error = "Out of memory";
  }
  if (error == NULL) {
    has_presentation = v->host.prepare(v->host.ctx, &presentation);
    if (v->closed) error = "Video restart was cancelled during preparation";
  }
  if (error == NULL) {
    if (has_presentation && presentation.validate_publication) presentation.validate_publication(presentation.ctx);
    v->host.publish_window(v->host.ctx, qa_app_prepared_renderer_restart_window(surface));
    input_published = true;
    qa_app_prepared_renderer_restart_publish(surface);
    if (has_presentation && presentation.restart) error = presentation.restart(presentation.ctx);
    if (error == NULL && v->closed) error = "Video restart was cancelled after publication";
    if (error == NULL) error = v->host.published(v->host.ctx, kind);
    if (error == NULL) {
      qa_app_prepared_renderer_restart_retire(surface);
      char buf[64];
      snprintf(buf, sizeof buf, "Renderer restarted (%s).\n", kind);
      v->host.print(v->host.ctx, buf, has_source ? &source : NULL);
      v->completed++; v->running = false; v->host.settled(v->host.ctx);
      return true;
    }
  }
  bool published = surface != NULL && qa_app_prepared_renderer_restart_published(surface);
  if (!published) {
    if (input_published) v->host.publish_window(v->host.ctx, previous_window);
    if (surface) qa_app_prepared_renderer_restart_discard(surface);
  } else {
    qa_app_prepared_renderer_restart_retire(surface);
  }
  v->completed++; v->running = false;
  if (published) {
    v->host.settled(v->host.ctx);
    v->host.failed(v->host.ctx, "Video restart publication failed");
    return false;
  }
  char buf[1024];
  snprintf(buf, sizeof buf, "Video restart rejected: %s\n", error ? error : "");
  v->host.print(v->host.ctx, buf, has_source ? &source : NULL);
  v->host.settled(v->host.ctx);
  return false;
}
void qa_app_video_restart_close(qa_app_video_restart *v) {
  if (v->closed) return;
  v->closed = true;
  v->has_request = false;
  if (v->commands) {
    qa_command_buffer_unregister(v->commands, "vid_restart", vid_restart_handler, v, true);
    v->commands = NULL;
  }
}
typedef struct guest_prep {
  qa_app_video_guest_input input;
  qa_app_native_renderer *renderer;
  qa_app_video_guest_seat *seats; size_t seat_count;
  void (*assert_current)(void *ctx); void *assert_ctx;
} guest_prep;
static void guest_validate(void *ctx) {
  guest_prep *g = ctx;
  g->assert_current(g->assert_ctx);
}
static const char *guest_restart(void *ctx) {
  guest_prep *g = ctx;
  bool failed = false;
  for (size_t i = 0; i < g->seat_count; i++) {
    if (g->seats[i].client.shutdown(g->seats[i].client.ctx) != NULL) failed = true;
    if (g->seats[i].client.close(g->seats[i].client.ctx) != NULL) failed = true;
  }
  if (failed) return "Previous guest video shutdown failed";
  for (size_t i = 0; i < g->seat_count; i++) {
    void *registration = g->input.client_command_registration(g->input.ctx, g->seats[i].client.seat);
    /* viewport closure in TS reads seat.viewport(renderer.window) lazily;
     * the host reopen consumes it through the seat mirror. */
    double x = 0, y = 0, w = 0, h = 0;
    if (g->seats[i].viewport) g->seats[i].viewport(g->seats[i].ctx, qa_app_native_renderer_window(g->renderer), &x, &y, &w, &h);
    void *replacement = NULL;
    const char *error = g->seats[i].client.reopen(g->seats[i].client.ctx, g->renderer, registration, &replacement);
    if (error != NULL) {
      bool cleanup_failed = false;
      if (replacement != NULL && g->seats[i].client.close(replacement) != NULL) cleanup_failed = true;
      if (g->input.registration_close(g->input.ctx, registration) != NULL) cleanup_failed = true;
      return cleanup_failed ? "Guest video reopen and cleanup failed" : error;
    }
    g->seats[i].publish(g->seats[i].ctx, replacement);
  }
  return NULL;
}
bool qa_app_prepare_video_guests(qa_app_video_guest_input input, qa_app_native_renderer *renderer,
                                 const qa_app_video_guest_seat *seats, size_t seat_count,
                                 void (*assert_current)(void *ctx), void *assert_ctx,
                                 qa_app_prepared_video_presentation *out) {
  if (seat_count == 0) return false;
  guest_prep *g = calloc(1, sizeof *g);
  if (!g) qa_range_error("Out of memory");
  g->input = input; g->renderer = renderer;
  g->seats = calloc(seat_count, sizeof *g->seats);
  if (!g->seats) qa_range_error("Out of memory");
  memcpy(g->seats, seats, seat_count * sizeof *g->seats);
  g->seat_count = seat_count;
  g->assert_current = assert_current; g->assert_ctx = assert_ctx;
  out->ctx = g;
  out->validate_publication = guest_validate;
  out->restart = guest_restart;
  return true;
}
