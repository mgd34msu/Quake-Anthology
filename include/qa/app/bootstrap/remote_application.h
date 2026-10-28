/* Port of src/app/bootstrap/remote-application.ts. The full remote
 * composition graph (network/simulation/presentation slices, local
 * play, capture, video restart) is wired by the host through this
 * file's lifecycle; open/run ordering, modal/gameplay views, seat
 * reindexing and close phases are kept here. Async -> synchronous /
 * pumped (remote-seat-pump). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_REMOTE_APPLICATION_H
#define QA_APP_BOOTSTRAP_REMOTE_APPLICATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/options.h"
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/render.h"
#include "qa/platform/sdl.h"
/* TS RemoteApplicationServices (hosted composition mirror). */
typedef struct qa_app_remote_application_host {
  void *ctx;
  const char *(*open_services)(void *ctx);
  const char *(*connect)(void *ctx);
  const char *(*build_seats)(void *ctx);
  const char *(*open_local_play)(void *ctx);
  const char *(*advance_frame)(void *ctx, double elapsed);
  const char *(*pump_modal)(void *ctx);
  const char *(*pump_gameplay)(void *ctx);
  const char *(*close_services)(void *ctx);
  void (*command)(void *ctx, const char *name, const char *const *args, size_t count, const qa_command_context *source);
  bool (*handle_event)(void *ctx, const qa_sdl_event *event);
  void (*capture)(void *ctx, const void *request);
  bool (*report)(void *ctx);
  double (*frame_count)(void *ctx);
  const char *(*backend)(void *ctx);
  void (*print)(void *ctx, const char *text);
} qa_app_remote_application_host;
typedef struct qa_app_remote_application qa_app_remote_application;
/* TS #open(options subset, host): options are borrowed by value. */
qa_app_remote_application *qa_app_remote_application_open(qa_app_remote_application_host host, qa_app_options options);
void qa_app_remote_application_destroy(qa_app_remote_application *app);
const char *qa_app_remote_application_backend(qa_app_remote_application *app);
const void *qa_app_remote_application_captures(qa_app_remote_application *app);
bool qa_app_remote_application_open_gui(qa_app_remote_application *app);
void qa_app_remote_application_pump(qa_app_remote_application *app);
void qa_app_remote_application_modal(qa_app_remote_application *app);
void qa_app_remote_application_advance(qa_app_remote_application *app, double elapsed);
void qa_app_remote_application_command(qa_app_remote_application *app, const char *name, const char *const *args, size_t count, const qa_command_context *source);
bool qa_app_remote_application_handle(qa_app_remote_application *app, const qa_sdl_event *event);
void qa_app_remote_application_capture(qa_app_remote_application *app, const void *request);
int qa_app_remote_application_report(qa_app_remote_application *app);
double qa_app_remote_application_frame_count(qa_app_remote_application *app);
qa_render_frame qa_app_remote_application_prepare_frame(qa_app_remote_application *app, qa_render_frame frame);
void qa_app_remote_application_reindex(qa_app_remote_application *app);
void qa_app_remote_application_close(qa_app_remote_application *app);
#endif
