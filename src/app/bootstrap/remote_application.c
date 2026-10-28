/* Port of src/app/bootstrap/remote-application.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/remote_application.h"
#include <stdlib.h>
#include "qa/app/bootstrap/startup_timing.h"
#include "qa/foundation/support.h"
struct qa_app_remote_application {
  qa_app_remote_application_host host;
  qa_app_options options;
  bool closed, closing;
  double frames;
};
static void phase(const char *error) { if (error != NULL) qa_range_error("%s", error); }
qa_app_remote_application *qa_app_remote_application_open(qa_app_remote_application_host host, qa_app_options options) {
  qa_app_remote_application *app = calloc(1, sizeof *app);
  if (!app) return NULL;
  app->host = host;
  app->options = options;
  phase(host.open_services(host.ctx));
  qa_app_startup_stamp("remote-services");
  phase(host.connect(host.ctx));
  qa_app_startup_stamp("remote-connect");
  phase(host.build_seats(host.ctx));
  qa_app_startup_stamp("remote-seats");
  phase(host.open_local_play(host.ctx));
  qa_app_startup_stamp("remote-ready");
  return app;
}
void qa_app_remote_application_destroy(qa_app_remote_application *app) {
  if (!app) return;
  qa_app_remote_application_close(app);
  qa_app_options_destroy(&app->options);
  free(app);
}
const char *qa_app_remote_application_backend(qa_app_remote_application *app) {
  return app->host.backend(app->host.ctx);
}
const void *qa_app_remote_application_captures(qa_app_remote_application *app) { (void)app; return NULL; }
bool qa_app_remote_application_open_gui(qa_app_remote_application *app) {
  if (app->closed) qa_range_error("Remote application is closed");
  return true;
}
void qa_app_remote_application_pump(qa_app_remote_application *app) {
  if (app->closed) qa_range_error("Remote application is closed");
  phase(app->host.pump_gameplay(app->host.ctx));
  app->frames++;
}
void qa_app_remote_application_modal(qa_app_remote_application *app) {
  if (app->closed) qa_range_error("Remote application is closed");
  phase(app->host.pump_modal(app->host.ctx));
}
void qa_app_remote_application_advance(qa_app_remote_application *app, double elapsed) {
  if (app->closed) qa_range_error("Remote application is closed");
  phase(app->host.advance_frame(app->host.ctx, elapsed));
  app->frames++;
}
void qa_app_remote_application_command(qa_app_remote_application *app, const char *name, const char *const *args, size_t count, const qa_command_context *source) {
  app->host.command(app->host.ctx, name, args, count, source);
}
bool qa_app_remote_application_handle(qa_app_remote_application *app, const qa_sdl_event *event) {
  if (app->closed) return false;
  return app->host.handle_event(app->host.ctx, event);
}
void qa_app_remote_application_capture(qa_app_remote_application *app, const void *request) {
  app->host.capture(app->host.ctx, request);
}
int qa_app_remote_application_report(qa_app_remote_application *app) {
  return app->host.report(app->host.ctx) ? 0 : 1;
}
double qa_app_remote_application_frame_count(qa_app_remote_application *app) {
  return app->host.frame_count ? app->host.frame_count(app->host.ctx) : app->frames;
}
qa_render_frame qa_app_remote_application_prepare_frame(qa_app_remote_application *app, qa_render_frame frame) {
  (void)app;
  return frame;
}
void qa_app_remote_application_reindex(qa_app_remote_application *app) {
  if (app->closed) qa_range_error("Remote application is closed");
  /* Seat layout reindexing runs inside build_seats on the host. */
  phase(app->host.build_seats(app->host.ctx));
}
void qa_app_remote_application_close(qa_app_remote_application *app) {
  if (app->closed || app->closing) return;
  app->closing = true;
  const char *error = app->host.close_services(app->host.ctx);
  app->closed = true;
  if (error != NULL) qa_range_error("%s", error);
}
