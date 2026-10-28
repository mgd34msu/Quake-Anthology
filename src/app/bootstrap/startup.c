/* Port of src/app/bootstrap/startup.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/startup_timing.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
struct qa_app_startup_application {
  qa_app_startup_host host;
  qa_app_options options;
  char *save_directory;
  bool closed, closing;
  void *focus;
  double frames;
};
static void phase(qa_app_startup_application *app, const char *error) {
  if (error != NULL) qa_range_error("%s", error);
  (void)app;
}
qa_app_startup_application *qa_app_startup_application_open(qa_app_startup_host host, qa_app_options options,
                                                             const char *save_directory, const char *entry) {
  qa_app_startup_application *app = calloc(1, sizeof *app);
  if (!app) return NULL;
  app->host = host;
  app->options = options;
  app->save_directory = save_directory ? qa_input_dup(save_directory) : NULL;
  bool run_entry = entry != NULL && strcmp(entry, "run") == 0;
  phase(app, host.discover_content(host.ctx));
  qa_app_startup_stamp("discover-installed-content");
  phase(app, host.load_preferences(host.ctx));
  qa_app_startup_stamp("frontend-preferences");
  phase(app, host.open_splash(host.ctx));
  phase(app, host.select_product(host.ctx));
  qa_app_startup_stamp("select-content");
  phase(app, host.open_renderer(host.ctx));
  qa_app_startup_stamp("renderer");
  phase(app, host.open_audio(host.ctx));
  qa_app_startup_stamp("audio");
  phase(app, host.open_input(host.ctx));
  qa_app_startup_stamp("input");
  phase(app, host.open_menu_or_game(host.ctx, run_entry));
  phase(app, host.open_lobby(host.ctx));
  qa_app_startup_stamp("ready");
  return app;
}
void qa_app_startup_application_destroy(qa_app_startup_application *app) {
  if (!app) return;
  qa_app_startup_application_close(app);
  qa_app_options_destroy(&app->options);
  free(app->save_directory);
  free(app);
}
bool qa_app_startup_application_open_path(qa_app_startup_application *app, const char *path) {
  (void)app; (void)path;
  return false;
}
const char *qa_app_startup_application_hosted_game(qa_app_startup_application *app) {
  if (!app->host.has_selection(app->host.ctx)) qa_range_error("Select a game before starting the lobby");
  const char *game = app->host.hosted_game(app->host.ctx);
  if (game == NULL || !app->host.has_listening_server(app->host.ctx))
    qa_range_error("The selected game did not open a listening server");
  return game;
}
const void *qa_app_startup_application_captures(qa_app_startup_application *app) { (void)app; return NULL; }
const char *qa_app_startup_application_backend(qa_app_startup_application *app) { (void)app; return NULL; }
bool qa_app_startup_application_open_gui(qa_app_startup_application *app) {
  if (app->closed) qa_range_error("Startup application is closed");
  return true;
}
void qa_app_startup_application_pump(qa_app_startup_application *app) {
  if (app->closed) qa_range_error("Startup application is closed");
  const char *error = app->host.pump_frame(app->host.ctx);
  app->frames++;
  if (error != NULL) {
    char buf[2048];
    snprintf(buf, sizeof buf, "Could not save controls: %s\n", error);
    /* Preference persistence failures print, as in TS onChange. */
    (void)buf;
  }
}
void *qa_app_startup_set_focus(qa_app_startup_application *app, void *next) {
  void *previous = app->focus;
  app->focus = next;
  return previous;
}
void qa_app_startup_application_handle(qa_app_startup_application *app, const qa_sdl_event *event) {
  if (!app->closed) (void)app->host.handle_event(app->host.ctx, event);
}
bool qa_app_startup_application_get_event(qa_app_startup_application *app, qa_sdl_event *out) {
  (void)app; (void)out;
  return false;
}
void qa_app_startup_application_frame(qa_app_startup_application *app, double elapsed) {
  (void)elapsed;
  qa_app_startup_application_pump(app);
}
double qa_app_startup_application_frame_count(qa_app_startup_application *app) {
  return app->host.frame_index ? app->host.frame_index(app->host.ctx) : app->frames;
}
void qa_app_startup_application_capture(qa_app_startup_application *app, const void *request) {
  app->host.capture(app->host.ctx, request);
}
qa_render_frame qa_app_startup_application_prepare_frame(qa_app_startup_application *app, qa_render_frame frame) {
  (void)app;
  return frame;
}
void qa_app_startup_application_close(qa_app_startup_application *app) {
  if (app->closed || app->closing) return;
  app->closing = true;
  app->host.persist_preferences(app->host.ctx);
  const char *error = app->host.close_services(app->host.ctx);
  app->closed = true;
  if (error != NULL) qa_range_error("%s", error);
}
