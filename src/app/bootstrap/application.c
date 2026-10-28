/* Port of src/app/bootstrap/application.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/application.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct { char *name; } queued_command;
struct qa_app_application {
  qa_app_options options;
  qa_app_application_host host;
  bool has_preferences;
  qa_app_frontend_preference_overrides preferences;
  double frames, elapsed;
  bool stopping, closed, closing;
  queued_command *queued; size_t queued_count, queued_cap;
  qa_seat_id *ranking_requests; size_t ranking_count, ranking_cap;
};
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static void phase(const char *error) { if (error) qa_range_error("%s", error); }
qa_app_application *qa_app_application_open(qa_app_options options, qa_app_application_host host,
                                            const qa_executable_recipe *recipe, bool has_recipe,
                                            bool has_preferences, qa_app_frontend_preference_overrides preferences) {
  (void)recipe; (void)has_recipe;
  qa_app_application *application = calloc(1, sizeof *application);
  if (!application) return NULL;
  application->options = options;
  application->host = host;
  application->has_preferences = has_preferences;
  application->preferences = preferences;
  if (host.open) phase(host.open(host.ctx, application));
  return application;
}
qa_app_application *qa_app_open_application(qa_app_options options, qa_app_application_host host,
                                            const qa_executable_recipe *recipe, bool has_recipe,
                                            bool has_preferences, qa_app_frontend_preference_overrides preferences) {
  return qa_app_application_open(options, host, recipe, has_recipe, has_preferences, preferences);
}
void qa_app_application_destroy(qa_app_application *application) {
  if (!application) return;
  qa_app_application_close(application);
  for (size_t i = 0; i < application->queued_count; i++) free(application->queued[i].name);
  free(application->queued);
  free(application->ranking_requests);
  qa_app_options_destroy(&application->options);
  free(application);
}
const qa_app_options *qa_app_application_options(const qa_app_application *a) { return &a->options; }
double qa_app_application_frame_count(const qa_app_application *a) { return a->frames; }
double qa_app_application_time_milliseconds(const qa_app_application *a) { return a->elapsed; }
bool qa_app_application_finished(const qa_app_application *a) {
  return a->stopping || a->closed || (a->options.has_frame_limit && a->frames >= a->options.frame_limit);
}
bool qa_app_application_client_commands_blocked(qa_app_application *a) {
  return (a->host.capture_blocks_transition && a->host.capture_blocks_transition(a->host.ctx)) ||
      (a->host.video_restart_pending && a->host.video_restart_pending(a->host.ctx));
}
void qa_app_application_request_quit(qa_app_application *a) { a->stopping = true; }
void qa_app_application_queue_command(qa_app_application *a, const char *name,
                                      const char *const *arguments, size_t argument_count,
                                      bool has_seat, qa_seat_id seat, bool has_source, qa_command_context source) {
  (void)arguments; (void)argument_count; (void)has_seat; (void)seat; (void)has_source; (void)source;
  if (a->queued_count == a->queued_cap) {
    a->queued_cap = a->queued_cap ? a->queued_cap * 2 : 16;
    a->queued = realloc(a->queued, a->queued_cap * sizeof *a->queued);
  }
  a->queued[a->queued_count++] = (queued_command){dup(name)};
}
size_t qa_app_application_queued_command_count(const qa_app_application *a) { return a->queued_count; }
bool qa_app_application_input(qa_app_application *a, const qa_seat_input_event *event) {
  return a->host.input ? a->host.input(a->host.ctx, event) : false;
}
void qa_app_application_pump_client_input(qa_app_application *a) {
  if (a->host.pump_client_input) a->host.pump_client_input(a->host.ctx);
}
const char *qa_app_application_advance_client_startup(qa_app_application *a, bool *advanced) {
  return a->host.advance_client_startup ? a->host.advance_client_startup(a->host.ctx, advanced)
                                         : (*advanced = false, (const char *)NULL);
}
const char *qa_app_application_flush_client_commands(qa_app_application *a) {
  return a->host.flush_client_commands ? a->host.flush_client_commands(a->host.ctx) : NULL;
}
const char *qa_app_application_step(qa_app_application *a, double elapsed_milliseconds, const void **out_output) {
  const char *error = a->host.step ? a->host.step(a->host.ctx, a, elapsed_milliseconds, out_output) : NULL;
  if (!error) {
    a->frames++;
    a->elapsed += elapsed_milliseconds;
  }
  return error;
}
const char *qa_app_application_run(qa_app_application *a, double frame_milliseconds) {
  /* TS run(): step until finished; the pumped C loop owns pacing. */
  while (!qa_app_application_finished(a)) {
    const void *output = NULL;
    const char *error = qa_app_application_step(a, frame_milliseconds, &output);
    if (error) return error;
  }
  return NULL;
}
const char *qa_app_application_change_level(qa_app_application *a, const char *map, const char *spawn_point) {
  return a->host.change_level(a->host.ctx, map, spawn_point ? spawn_point : "");
}
const char *qa_app_application_save_game(qa_app_application *a, const char *path, const char *format) {
  return a->host.save_game(a->host.ctx, path, format ? format : "shared");
}
const char *qa_app_application_load_game(qa_app_application *a, const char *path, const char *source_product) {
  return a->host.load_game(a->host.ctx, path, source_product);
}
const char *qa_app_application_execute_request(qa_app_application *a, qa_app_configuration_command_request request) {
  return a->host.execute_request(a->host.ctx, request);
}
const char *qa_app_application_return_to_lobby(qa_app_application *a) {
  return a->host.return_to_lobby ? a->host.return_to_lobby(a->host.ctx) : NULL;
}
const char *qa_app_application_read_client_resource(qa_app_application *a, const char *path, uint8_t **out_bytes, size_t *out_count, bool *found) {
  return a->host.read_client_resource(a->host.ctx, path, out_bytes, out_count, found);
}
const char *qa_app_application_read_pixels(qa_app_application *a, uint8_t **out_bytes, size_t *out_count) {
  return a->host.read_pixels(a->host.ctx, out_bytes, out_count);
}
const char *qa_app_application_capture_next_frame(qa_app_application *a, uint8_t **out_bytes, size_t *out_count) {
  return a->host.read_pixels(a->host.ctx, out_bytes, out_count);
}
const char *qa_app_application_close(qa_app_application *a) {
  if (!a || a->closed || a->closing) return NULL;
  a->closing = true;
  const char *error = a->host.close ? a->host.close(a->host.ctx, a) : NULL;
  a->closed = true;
  return error;
}
void qa_app_application_request_ranking_menu(qa_app_application *a, qa_seat_id seat) {
  for (size_t i = 0; i < a->ranking_count; i++) if (qa_same_seat(a->ranking_requests[i], seat)) return;
  if (a->ranking_count == a->ranking_cap) {
    a->ranking_cap = a->ranking_cap ? a->ranking_cap * 2 : 8;
    a->ranking_requests = realloc(a->ranking_requests, a->ranking_cap * sizeof *a->ranking_requests);
  }
  a->ranking_requests[a->ranking_count++] = seat;
}
bool qa_app_application_take_ranking_menu_request(qa_app_application *a, qa_seat_id seat) {
  for (size_t i = 0; i < a->ranking_count; i++)
    if (qa_same_seat(a->ranking_requests[i], seat)) {
      a->ranking_requests[i] = a->ranking_requests[--a->ranking_count];
      return true;
    }
  return false;
}
void qa_app_application_release_settings(qa_app_application *a) { (void)a; /* tools/view cvars are host-owned */ }
const char *qa_app_application_retire(qa_app_application *a) { return qa_app_application_close(a); }
