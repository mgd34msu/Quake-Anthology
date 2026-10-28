/* Port of src/app/bootstrap/application.ts. The full composition
 * graph (world/session/input/audio/effects/lobby/rankings/save)
 * is wired by the host through the phase and service callbacks
 * below, as in landed startup.c; lifecycle state (finished/
 * frameCount/stopping/closed), ranking-menu request bookkeeping
 * and command queue guards are kept here. Async flows become
 * synchronous or pumped through step(). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_APPLICATION_H
#define QA_APP_BOOTSTRAP_APPLICATION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/configuration.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/frontend_preferences.h"
#include "qa/app/bootstrap/options.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/ui.h"
typedef struct qa_app_application qa_app_application;
/* TS ApplicationHost (services the graphical host provides). */
typedef struct qa_app_application_host {
  void *ctx;
  void (*print)(void *ctx, const char *text);
  const char *save_directory; /* NULL = absent */
  const char *const *external_save_directories; size_t external_save_directory_count;
  bool has_loading; bool defer_window_visibility;
  void (*loading_stage)(void *ctx, const char *message);
  /* Open/step/close composition phases; error text or NULL. */
  const char *(*open)(void *ctx, qa_app_application *application);
  const char *(*step)(void *ctx, qa_app_application *application, double elapsed_milliseconds, const void **out_output);
  const char *(*close)(void *ctx, qa_app_application *application);
  const char *(*change_level)(void *ctx, const char *map, const char *spawn_point);
  const char *(*save_game)(void *ctx, const char *path, const char *format);
  const char *(*load_game)(void *ctx, const char *path, const char *source_product);
  const char *(*execute_request)(void *ctx, qa_app_configuration_command_request request);
  const char *(*return_to_lobby)(void *ctx);
  const char *(*read_client_resource)(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_count, bool *found);
  bool (*input)(void *ctx, const qa_seat_input_event *event);
  void (*pump_client_input)(void *ctx);
  const char *(*advance_client_startup)(void *ctx, bool *advanced);
  const char *(*flush_client_commands)(void *ctx);
  bool (*capture_blocks_transition)(void *ctx);
  bool (*video_restart_pending)(void *ctx);
  const char *(*read_pixels)(void *ctx, uint8_t **out_bytes, size_t *out_count);
} qa_app_application_host;
/* TS Application.open / openApplication. */
qa_app_application *qa_app_application_open(qa_app_options options, qa_app_application_host host,
                                            const qa_executable_recipe *recipe, bool has_recipe,
                                            bool has_preferences, qa_app_frontend_preference_overrides preferences);
qa_app_application *qa_app_open_application(qa_app_options options, qa_app_application_host host,
                                            const qa_executable_recipe *recipe, bool has_recipe,
                                            bool has_preferences, qa_app_frontend_preference_overrides preferences);
void qa_app_application_destroy(qa_app_application *application);
const qa_app_options *qa_app_application_options(const qa_app_application *application);
double qa_app_application_frame_count(const qa_app_application *application);
double qa_app_application_time_milliseconds(const qa_app_application *application);
bool qa_app_application_finished(const qa_app_application *application);
bool qa_app_application_client_commands_blocked(qa_app_application *application);
void qa_app_application_request_quit(qa_app_application *application);
void qa_app_application_queue_command(qa_app_application *application, const char *name,
                                      const char *const *arguments, size_t argument_count,
                                      bool has_seat, qa_seat_id seat, bool has_source, qa_command_context source);
size_t qa_app_application_queued_command_count(const qa_app_application *application);
bool qa_app_application_input(qa_app_application *application, const qa_seat_input_event *event);
void qa_app_application_pump_client_input(qa_app_application *application);
const char *qa_app_application_advance_client_startup(qa_app_application *application, bool *advanced);
const char *qa_app_application_flush_client_commands(qa_app_application *application);
/* TS step/run: step returns the host simulation output token. */
const char *qa_app_application_step(qa_app_application *application, double elapsed_milliseconds, const void **out_output);
const char *qa_app_application_run(qa_app_application *application, double frame_milliseconds);
const char *qa_app_application_change_level(qa_app_application *application, const char *map, const char *spawn_point);
const char *qa_app_application_save_game(qa_app_application *application, const char *path, const char *format);
const char *qa_app_application_load_game(qa_app_application *application, const char *path, const char *source_product);
const char *qa_app_application_execute_request(qa_app_application *application, qa_app_configuration_command_request request);
const char *qa_app_application_return_to_lobby(qa_app_application *application);
const char *qa_app_application_read_client_resource(qa_app_application *application, const char *path, uint8_t **out_bytes, size_t *out_count, bool *found);
const char *qa_app_application_read_pixels(qa_app_application *application, uint8_t **out_bytes, size_t *out_count);
const char *qa_app_application_capture_next_frame(qa_app_application *application, uint8_t **out_bytes, size_t *out_count);
const char *qa_app_application_close(qa_app_application *application);
/* Ranking menu requests (TS rankingMenuRequests set). */
void qa_app_application_request_ranking_menu(qa_app_application *application, qa_seat_id seat);
bool qa_app_application_take_ranking_menu_request(qa_app_application *application, qa_seat_id seat);
void qa_app_application_release_settings(qa_app_application *application);
const char *qa_app_application_retire(qa_app_application *application);
#endif
