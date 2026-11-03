/* Port of src/app/bootstrap/startup.ts, the StartupApplication class (one C
 * type, qa_startup_application), built alongside the temporary concrete-host
 * shell in startup.h (main.c's path until the connection step).
 *
 * Files: startup_application.c (constructor, StartupAction, print,
 * routeCommand, frontendCommand, connect, lobbyJoinOptions),
 * startup_application_sources.c (bindDemoCommands),
 * startup_application_frontend.c (bindFrontendConsole, activateFrontend,
 * returnToFrontend, failActiveSource, publishFrontendRouting, the frontend
 * movie), startup_application_library.c (the library menu services),
 * startup_application_launch.c (launch, connect*, publishPendingSource,
 * publishDemoIntent, dispatchApplicationRequest, step, run, close, preference
 * persistence) and startup_application_graphics.c (open, openGraphics).
 *
 * TS async members are synchronous (docs/PORTING.md); a TS throw reaches the
 * caller through qa_raise. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_APPLICATION_H
#define QA_APP_BOOTSTRAP_STARTUP_APPLICATION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/application_class.h"
#include "qa/app/bootstrap/campaign_cinematic.h"
#include "qa/app/bootstrap/demo_commands.h"
#include "qa/app/bootstrap/frontend_preferences.h"
#include "qa/app/bootstrap/gtv_commands.h"
#include "qa/app/bootstrap/input_devices.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/prepared_startup.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/app/bootstrap/server_browser.h"
#include "qa/app/bootstrap/startup_selection.h"
#include "qa/contracts/common.h"
#include "qa/contracts/ui.h"
#include "qa/input/router.h"
#include "qa/network/services/online.h"

/* TS StartupAction. Strings are owned (qa_app_startup_action_clear). */
typedef enum qa_app_startup_action_kind {
  QA_APP_STARTUP_ACTION_GTV, QA_APP_STARTUP_ACTION_CONNECT, QA_APP_STARTUP_ACTION_INITIAL,
  QA_APP_STARTUP_ACTION_LOBBY_HOST, QA_APP_STARTUP_ACTION_FRONTEND, QA_APP_STARTUP_ACTION_PLAY,
  QA_APP_STARTUP_ACTION_PRESET, QA_APP_STARTUP_ACTION_LOAD
} qa_app_startup_action_kind;
typedef struct qa_app_startup_action {
  qa_app_startup_action_kind kind;
  /* gtv: GtvConnectRequest; label NULL = TS undefined */
  char *gtv_address, *gtv_username, *gtv_password, *gtv_label;
  /* connect: BrowserConnection (remote owned) */
  qa_app_browser_connection connection;
  /* initial; q3_map_launch backs options.q3_map_launch (owned) */
  qa_app_options options;
  void *q3_map_launch;
  /* lobby-host: borrowed StartupLaunch */
  const qa_app_startup_launch *selection;
  /* preset: arena_map NULL = absent */
  char *preset_id; double skill; char *arena_map;
  /* load: source_product NULL = absent */
  char *path; char *source_product;
} qa_app_startup_action;
void qa_app_startup_action_clear(qa_app_startup_action *action);

/* TS DemoRecordingIntent (demo-recording-commands.ts). name NULL = undefined;
 * source is a qa_command_context_copy owned by the intent. */
typedef struct qa_app_startup_recording_intent {
  const char *kind; /* "record"|"rerecord"|"mvdrecord"|"serverrecord"|"stop"|"serverstop"|"mvdstop" */
  char *name;
  qa_command_context source;
} qa_app_startup_recording_intent;
/* TS ScreenCinematicRequest. */
typedef struct qa_app_startup_movie_request { char *name; bool loop, hold, silent; } qa_app_startup_movie_request;

/* TS ApplicationHost & { readonly localLobbies?: LocalLobbyService }. base.print
 * receives base.ctx; every pointer is borrowed for the application's life. */
typedef struct qa_startup_application_host {
  qa_application_host base;
  qa_local_lobby_service *local_lobbies; /* NULL is TS undefined */
} qa_startup_application_host;

typedef struct qa_startup_application qa_startup_application;

/* TS static open(options, host, saveDirectory = userDataDirectory("saves", ...),
 * entry = "menu"). save_directory NULL and entry NULL select the TS defaults.
 * A failure closes the partly opened application and raises. */
qa_startup_application *qa_startup_application_open(const qa_app_options *options,
    qa_startup_application_host host, const char *save_directory, const char *entry);
/* TS private constructor(model, host, saveDirectory, entry), exposed for the
 * members that run before openGraphics. The model is borrowed. */
qa_startup_application *qa_startup_application_create(qa_app_startup_selection_model *model,
    qa_startup_application_host host, const char *save_directory, const char *entry);
/* Frees a closed application (TS GC); closes it first when still open. */
void qa_startup_application_destroy(qa_startup_application *app);

/* TS public members. */
qa_app_startup_selection_model *qa_startup_application_model(const qa_startup_application *app);
qa_app_frontend_preferences *qa_startup_application_preferences(const qa_startup_application *app);
qa_application *qa_startup_application_active_game(const qa_startup_application *app);
/* TS get inputSeat: false is TS null. */
bool qa_startup_application_input_seat(const qa_startup_application *app, qa_seat_id *out);
bool qa_startup_application_input(qa_startup_application *app, const qa_seat_input_event *event);
bool qa_startup_application_quit_requested(const qa_startup_application *app);
void qa_startup_application_request_quit(qa_startup_application *app);
/* TS readPixels(): malloc'd RGBA rows. */
uint8_t *qa_startup_application_read_pixels(qa_startup_application *app, size_t *out_length);
/* TS captureNextFrame(): Promise<Uint8Array>. resolve receives the next presented
 * frame (its pixels are owned by the receiver); with no visible menu the request is
 * rejected with "Startup menu is not visible". */
void qa_startup_application_capture_next_frame(qa_startup_application *app, qa_app_frame_capture_request request);
void qa_startup_application_step(qa_startup_application *app);
void qa_startup_application_run(qa_startup_application *app);
/* TS close(). Errors collected during shutdown raise one AggregateError text. */
void qa_startup_application_close(qa_startup_application *app);

/* Members reached by the menu, the prepared startup and the tests. */
void qa_startup_application_print(qa_startup_application *app, const char *text);
/* TS routeCommand(name, args, source, prepared = this.client?.prepared):
 * prepared NULL with use_client_prepared selects the default. */
bool qa_startup_application_route_command(qa_startup_application *app, const char *name,
    const char *const *args, size_t arg_count, qa_command_context source, qa_app_prepared_startup *prepared,
    bool use_client_prepared);
void qa_startup_application_frontend_command(qa_startup_application *app, const char *name,
    const char *const *args, size_t arg_count, qa_command_context source);
/* TS connect(connection): the options passed to connectOptions. Owned out. */
void qa_startup_application_connection_options(qa_startup_application *app,
    const qa_app_browser_connection *connection, qa_app_options *out);
/* TS lobbyJoinOptions(lobby). recipe is lobby.composition.composition.recipe
 * (the C session composition carries canonical text only). Owned out. */
void qa_startup_application_lobby_join_options(qa_startup_application *app, const qa_lobby *lobby,
    const qa_executable_recipe *recipe, qa_app_options *out);

/* Read-only views of the pending state for the tests. */
const qa_app_startup_action *qa_startup_application_pending(const qa_startup_application *app);
const qa_app_startup_recording_intent *qa_startup_application_pending_recording(const qa_startup_application *app);
const qa_app_startup_movie_request *qa_startup_application_pending_movie(const qa_startup_application *app);
const char *qa_startup_application_status(const qa_startup_application *app);
void qa_startup_application_clear_pending(qa_startup_application *app);
#endif
