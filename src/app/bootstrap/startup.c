/* Port of src/app/bootstrap/startup.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/startup_timing.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/world/session/session.h"
struct qa_app_startup_application {
  qa_app_startup_host host;
  qa_app_options options;
  char *save_directory;
  bool closed, closing, stopping;
  void *focus;
  double frames;
  /* TS field initializers: identity "startup-client" + a local
   * EngineSession owned for the application's whole lifetime. */
  qa_identity_owner identity;
  bool has_identity;
  qa_engine_session *session;
};
/* Run one open() phase; the first error wins (TS open catch then
 * closes the partially opened application and rethrows). */
static void phase(const char **error, const char *result) {
  if (*error == NULL && result != NULL) *error = result;
}
qa_app_startup_application *qa_app_startup_application_open(qa_app_startup_host host, qa_app_options options,
                                                             const char *save_directory, const char *entry) {
  qa_app_startup_application *app = calloc(1, sizeof *app);
  if (!app) return NULL;
  app->host = host;
  app->options = options;
  app->save_directory = save_directory ? qa_input_dup(save_directory) : NULL;
  if (!qa_identity_owner_create(&app->identity, "startup-client")) qa_range_error("Out of memory");
  app->has_identity = true;
  qa_session_mode mode = {QA_SESSION_MODE_LOCAL};
  app->session = qa_engine_session_create(&app->identity, mode);
  if (!app->session) qa_range_error("Out of memory");
  bool run_entry = entry != NULL && strcmp(entry, "run") == 0;
  const char *error = NULL;
  /* TS open(): createStartupSelection, server browser, then
   * openGraphics (preferences, splash, product, renderer, audio,
   * input, menu or initial game, lobby). */
  if (host.discover_content) phase(&error, host.discover_content(host.ctx));
  qa_app_startup_stamp("discover-installed-content");
  if (host.load_preferences) phase(&error, host.load_preferences(host.ctx));
  qa_app_startup_stamp("frontend-preferences");
  if (host.open_splash) phase(&error, host.open_splash(host.ctx));
  if (host.select_product) phase(&error, host.select_product(host.ctx));
  qa_app_startup_stamp("select-content");
  if (host.open_renderer) phase(&error, host.open_renderer(host.ctx));
  qa_app_startup_stamp("renderer");
  if (host.open_audio) phase(&error, host.open_audio(host.ctx));
  qa_app_startup_stamp("audio");
  if (host.open_input) phase(&error, host.open_input(host.ctx));
  qa_app_startup_stamp("input");
  if (host.open_menu_or_game) phase(&error, host.open_menu_or_game(host.ctx, run_entry));
  if (host.open_lobby) phase(&error, host.open_lobby(host.ctx));
  qa_app_startup_stamp("ready");
  if (error != NULL) {
    /* TS: catch { await application.close(); throw error; } */
    qa_app_startup_application_close(app);
    qa_range_error("%s", error);
  }
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
  if (app->closed || !app->host.open_path) return false;
  return app->host.open_path(app->host.ctx, path);
}
const char *qa_app_startup_application_hosted_game(qa_app_startup_application *app) {
  if (!app->host.has_selection || !app->host.has_selection(app->host.ctx))
    qa_range_error("Select a game before starting the lobby");
  const char *game = app->host.hosted_game ? app->host.hosted_game(app->host.ctx) : NULL;
  if (game == NULL || !app->host.has_listening_server || !app->host.has_listening_server(app->host.ctx))
    qa_range_error("The selected game did not open a listening server");
  return game;
}
const void *qa_app_startup_application_captures(qa_app_startup_application *app) {
  return app->host.captures ? app->host.captures(app->host.ctx) : NULL;
}
const char *qa_app_startup_application_backend(qa_app_startup_application *app) {
  return app->host.backend ? app->host.backend(app->host.ctx) : NULL;
}
bool qa_app_startup_application_open_gui(qa_app_startup_application *app) {
  if (app->closed) qa_range_error("Startup application is closed");
  return true;
}
void qa_app_startup_application_pump(qa_app_startup_application *app) {
  /* TS step(): a closed or stopping startup returns silently. */
  if (app->closed || app->stopping) return;
  const char *error = app->host.pump_frame ? app->host.pump_frame(app->host.ctx) : NULL;
  app->frames++;
  if (error != NULL && app->host.print) {
    /* Preference persistence failures print, as in TS onChange. */
    char buf[2048];
    (void)snprintf(buf, sizeof buf, "Could not save controls: %s\n", error);
    app->host.print(app->host.ctx, buf);
  }
}
void *qa_app_startup_set_focus(qa_app_startup_application *app, void *next) {
  void *previous = app->focus;
  app->focus = next;
  return previous;
}
void qa_app_startup_application_handle(qa_app_startup_application *app, const qa_sdl_event *event) {
  if (app->closed || event == NULL) return;
  /* TS router unhandled quit requests quit before host handling. */
  if (event->kind == QA_SDL_EVENT_QUIT) app->stopping = true;
  if (app->host.handle_event) (void)app->host.handle_event(app->host.ctx, event);
}
bool qa_app_startup_application_get_event(qa_app_startup_application *app, qa_sdl_event *out) {
  if (app->closed || !app->host.poll_event) return false;
  return app->host.poll_event(app->host.ctx, out);
}
void qa_app_startup_application_frame(qa_app_startup_application *app, double elapsed) {
  (void)elapsed;
  qa_app_startup_application_pump(app);
}
double qa_app_startup_application_frame_count(qa_app_startup_application *app) {
  return app->host.frame_index ? app->host.frame_index(app->host.ctx) : app->frames;
}
void qa_app_startup_application_capture(qa_app_startup_application *app, const void *request) {
  if (app->host.capture) app->host.capture(app->host.ctx, request);
}
qa_render_frame qa_app_startup_application_prepare_frame(qa_app_startup_application *app, qa_render_frame frame) {
  (void)app;
  return frame;
}
void qa_app_startup_application_request_quit(qa_app_startup_application *app) {
  app->stopping = true;
}
bool qa_app_startup_application_stopping(const qa_app_startup_application *app) {
  return app->stopping;
}
bool qa_app_startup_application_finished(const qa_app_startup_application *app) {
  /* TS run() loop condition: stopping, closed, or frameLimit hit. */
  return app->stopping || app->closed ||
      (app->options.has_frame_limit && app->frames >= app->options.frame_limit);
}
void qa_app_startup_application_run(qa_app_startup_application *app) {
  /* TS run(): step until finished; the pumped C loop owns pacing. */
  double counted = qa_app_startup_application_frame_count(app);
  while (!app->stopping && !app->closed &&
         !(app->options.has_frame_limit && counted >= app->options.frame_limit)) {
    qa_app_startup_application_pump(app);
    counted = qa_app_startup_application_frame_count(app);
  }
}
qa_engine_session *qa_app_startup_application_session(qa_app_startup_application *app) {
  return app->session;
}
void qa_app_startup_application_close(qa_app_startup_application *app) {
  if (app->closed || app->closing) return;
  app->closing = true;
  app->closed = true;
  app->stopping = true;
  /* TS close(): persist controls first, then the frontend session,
   * then the graphical services. */
  if (app->host.persist_preferences) app->host.persist_preferences(app->host.ctx);
  if (app->session) {
    qa_engine_session_close(app->session);
    qa_engine_session_destroy(app->session);
    app->session = NULL;
  }
  if (app->has_identity) {
    qa_identity_owner_destroy(&app->identity);
    app->has_identity = false;
  }
  const char *error = app->host.close_services ? app->host.close_services(app->host.ctx) : NULL;
  if (error != NULL) qa_range_error("%s", error);
}
