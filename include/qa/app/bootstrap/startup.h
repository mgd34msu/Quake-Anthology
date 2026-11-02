/* Port of src/app/bootstrap/startup.ts. The full composition graph
 * (content, session, input routing, lobby, host services) is wired by
 * the host through this file's phases; open/run lifecycle, hosted-game
 * guard, command routing surface and close ordering are kept here.
 * main.ts composition stays with the caller. Async -> synchronous;
 * host failures are error strings. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_H
#define QA_APP_BOOTSTRAP_STARTUP_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/options.h"
#include "qa/contracts/common.h"
#include "qa/contracts/render.h"
#include "qa/input/seat.h"
#include "qa/platform/sdl.h"
/* TS ApplicationHost + localLobbies service mirror (composition root). */
typedef struct qa_app_startup_host {
  void *ctx;
  /* Phase callbacks mirroring the TS open() sequence. */
  const char *(*discover_content)(void *ctx);
  const char *(*load_preferences)(void *ctx);
  const char *(*open_splash)(void *ctx);
  const char *(*select_product)(void *ctx);
  const char *(*open_renderer)(void *ctx);
  const char *(*open_audio)(void *ctx);
  const char *(*open_input)(void *ctx);
  const char *(*open_menu_or_game)(void *ctx, bool run_entry);
  const char *(*open_lobby)(void *ctx);
  const char *(*pump_frame)(void *ctx);   /* NULL = continue */
  const char *(*selection_error)(void *ctx); /* TS staged lobby errors */
  bool (*has_selection)(void *ctx);
  bool (*has_listening_server)(void *ctx);
  const char *(*hosted_game)(void *ctx);  /* NULL when listening */
  void (*persist_preferences)(void *ctx);
  void (*print)(void *ctx, const char *text);
  /* ApplicationHost surface (main-loop.ts mirror). */
  void (*command)(void *ctx, const char *name, const char *const *args, size_t count, const qa_command_context *source);
  bool (*handle_event)(void *ctx, const qa_sdl_event *event);
  void (*capture)(void *ctx, const void *request);
  int (*frame_count)(void *ctx);
  const char *(*close_services)(void *ctx);
  double (*frame_index)(void *ctx);
  /* Platform services (all optional): system URL/path opener,
   * pending SDL event source, capture bundle and renderer backend
   * tokens for the matching StartupApplication accessors. */
  bool (*open_path)(void *ctx, const char *path);
  bool (*poll_event)(void *ctx, qa_sdl_event *out);
  const void *(*captures)(void *ctx);
  const char *(*backend)(void *ctx);
} qa_app_startup_host;
typedef struct qa_app_startup_application qa_app_startup_application;
typedef struct qa_engine_session qa_engine_session;
/* TS StartupApplication.open(options, host, saveDirectory?, entry?). */
qa_app_startup_application *qa_app_startup_application_open(qa_app_startup_host host, qa_app_options options,
                                                             const char *save_directory, const char *entry);
void qa_app_startup_application_destroy(qa_app_startup_application *app);
/* TS open(url or path) system-URL opener is platform-owned: host ctx. */
bool qa_app_startup_application_open_path(qa_app_startup_application *app, const char *path);
const char *qa_app_startup_application_hosted_game(qa_app_startup_application *app);
const void *qa_app_startup_application_captures(qa_app_startup_application *app);
const char *qa_app_startup_application_backend(qa_app_startup_application *app);
bool qa_app_startup_application_open_gui(qa_app_startup_application *app);
void qa_app_startup_application_pump(qa_app_startup_application *app);
void *qa_app_startup_set_focus(qa_app_startup_application *app, void *next);
void qa_app_startup_application_handle(qa_app_startup_application *app, const qa_sdl_event *event);
bool qa_app_startup_application_get_event(qa_app_startup_application *app, qa_sdl_event *out);
void qa_app_startup_application_frame(qa_app_startup_application *app, double elapsed);
double qa_app_startup_application_frame_count(qa_app_startup_application *app);
void qa_app_startup_application_capture(qa_app_startup_application *app, const void *request);
qa_render_frame qa_app_startup_application_prepare_frame(qa_app_startup_application *app, qa_render_frame frame);
void qa_app_startup_application_close(qa_app_startup_application *app);
/* TS requestQuit / run loop condition / owned frontend session. */
void qa_app_startup_application_request_quit(qa_app_startup_application *app);
bool qa_app_startup_application_stopping(const qa_app_startup_application *app);
bool qa_app_startup_application_finished(const qa_app_startup_application *app);
void qa_app_startup_application_run(qa_app_startup_application *app);
qa_engine_session *qa_app_startup_application_session(qa_app_startup_application *app);
#endif
