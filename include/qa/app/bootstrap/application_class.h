/* Port of src/app/bootstrap/application.ts: the Application class (one C type,
 * qa_application). Built alongside the temporary concrete-host layer in
 * application.h (qa_app_application, main.c's path until the connection step);
 * the two share nothing. The application constructs the real SharedSimulation
 * (shared_simulation.h).
 *
 * The member layout (TS member, C function, C file, status) is
 * docs/application_layout.md. TS async members are synchronous; a TS throw
 * reaches the caller through qa_raise. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_APPLICATION_CLASS_H
#define QA_APP_BOOTSTRAP_APPLICATION_CLASS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/frontend_preferences.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/simulation/shared_simulation.h"
#include "qa/contracts/session.h"
#include "qa/llm/settings.h"
#include "qa/console/llm.h"
#include "qa/network/services/online.h"
#include "qa/network/services/rankings.h"

typedef struct qa_application qa_application;
typedef struct qa_engine_session qa_engine_session;
/* TS ApplicationMatchRankings (match-results.ts; no C port yet). */
typedef struct qa_app_match_rankings qa_app_match_rankings;

/* TS ApplicationHost["lobby"]. ctx is passed to both members. */
typedef struct qa_application_lobby {
  void *ctx;
  void (*complete)(void *ctx);
  void (*return_to_lobby)(void *ctx);
  const qa_online_account *account; /* NULL = TS undefined */
} qa_application_lobby;
/* TS ApplicationHost["rankings"]. */
typedef struct qa_application_rankings_host {
  const qa_ranking_provider *provider;
  const char *game_key;
} qa_application_rankings_host;
/* TS ApplicationHost["loading"]. next_frame NULL = TS undefined. */
typedef struct qa_application_loading {
  void *ctx;
  bool defer_window_visibility;
  void (*next_frame)(void *ctx);
  void (*stage)(void *ctx, const char *message);
} qa_application_loading;
/* TS ApplicationHost. Every pointer is borrowed for the application's life;
 * NULL is TS undefined. print receives ctx. */
typedef struct qa_application_host {
  void *ctx;
  const qa_application_lobby *lobby;
  qa_app_match_rankings *local_rankings;
  const qa_application_rankings_host *rankings;
  /* TS llm: LlmSettingsUi & LlmCommandRequester (both or neither). */
  qa_llm_settings_service *llm_settings;
  const qa_llm_command_requester *llm_requester;
  const char *save_directory;
  bool has_external_save_directories;
  const char *const *external_save_directories; size_t external_save_directory_count;
  const qa_application_loading *loading;
  void (*print)(void *ctx, const char *text);
} qa_application_host;

/* TS Application.open(options, host, recipe?, preferences?, initialSave?) and
 * openApplication. NULL recipe / preferences / initial_save are TS undefined.
 * options is copied; its strings are borrowed for the application's life. */
qa_application *qa_application_open(qa_app_options options, qa_application_host host,
    const qa_executable_recipe *recipe, const qa_app_frontend_preference_overrides *preferences,
    const qa_save_image *initial_save);
qa_application *qa_application_open_application(qa_app_options options, qa_application_host host,
    const qa_executable_recipe *recipe, const qa_app_frontend_preference_overrides *preferences);
/* Frees a closed application (TS GC). */
void qa_application_destroy(qa_application *app);

/* TS step(elapsedMilliseconds). The output borrows the simulation's storage
 * until the next step. */
qa_simulation_output qa_application_step(qa_application *app, double elapsed_milliseconds);
/* TS run(): runFrameLoop over step. */
void qa_application_run(qa_application *app);
/* TS close(). A second close rethrows the first close's error. */
void qa_application_close(qa_application *app);

/* TS public getters. */
const qa_app_options *qa_application_options(const qa_application *app);
qa_app_loaded_content *qa_application_content(const qa_application *app);
qa_app_shared_simulation *qa_application_simulation(const qa_application *app);
qa_engine_session *qa_application_session(const qa_application *app);
double qa_application_frame_count(const qa_application *app);
double qa_application_time_milliseconds(const qa_application *app);
double qa_application_world_operation_count(const qa_application *app);
bool qa_application_world_failed(const qa_application *app);
bool qa_application_finished(const qa_application *app);
void qa_application_request_quit(qa_application *app);
/* TS queueCommand(name, args, seat): seat NULL is TS null. */
void qa_application_queue_command(qa_application *app, const char *name,
    const char *const *arguments, size_t argument_count, const qa_seat_id *seat);
bool qa_application_take_ranking_menu_request(qa_application *app, qa_seat_id seat);
/* TS executeApplicationRequest(request): dispatches the request (copied) through commands(). */
struct qa_app_configuration_command_request;
void qa_application_execute_application_request(qa_application *app, const struct qa_app_configuration_command_request *request);
/* TS takePendingClientCommands(): the queued requests and the closure that dispatches
 * them (run consumes the record; discard drops it unrun). */
typedef struct qa_application_pending_client_commands qa_application_pending_client_commands;
qa_application_pending_client_commands *qa_application_take_pending_client_commands(qa_application *app);
void qa_application_pending_client_commands_run(qa_application_pending_client_commands *pending);
void qa_application_pending_client_commands_discard(qa_application_pending_client_commands *pending);

/* ---- Members StartupApplication (startup_application*.c) calls. The same
 * declarations as src/app/bootstrap/application_internal.h, made visible to
 * the frontend; networkWire has no definition yet (docs/application_layout.md). */
#include "qa/app/bootstrap/client_bootstrap.h"
#include "qa/app/bootstrap/demo_recording.h"
#include "qa/contracts/ui.h"
#include "qa/network/common/endpoint.h"
#include "qa/platform/sdl.h"
qa_application *qa_application_open_borrowed(qa_app_client_bootstrap *client, qa_app_options options, qa_application_host host,
    const qa_executable_recipe *recipe, const qa_app_frontend_preference_overrides *preferences, const qa_save_image *initial_save);
qa_app_frontend_preference_overrides qa_application_frontend_settings(qa_application *app);
bool qa_application_client_commands_blocked(qa_application *app);
void qa_application_pump_client_input(qa_application *app);
bool qa_application_advance_client_startup(qa_application *app);
void qa_application_flush_client_commands(qa_application *app);
bool qa_application_network_address(const qa_application *app, qa_net_address *out);
/* TS get networkWire: false is TS null. */
bool qa_application_network_wire(const qa_application *app, qa_wire_selection *out);
qa_sdl_window *qa_application_window(qa_application *app);
bool qa_application_input(qa_application *app, const qa_seat_input_event *event);
typedef struct qa_application_recording_feed qa_application_recording_feed;
const char *qa_application_recording_feed_root(const qa_application_recording_feed *feed);
qa_app_demo_recording_seed qa_application_recording_feed_seed(qa_application_recording_feed *feed);
void *qa_application_recording_feed_attach(qa_application_recording_feed *feed, qa_app_demo_recording_sink sink);
void qa_application_recording_feed_detach(qa_application_recording_feed *feed, void *token);
qa_application_recording_feed *qa_application_prepare_mvd_recording(qa_application *app, qa_command_context context);
qa_application_recording_feed *qa_application_prepare_server_recording(qa_application *app, qa_command_context context);
#endif
