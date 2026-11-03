/* Port of src/app/bootstrap/remote-application.ts: the RemoteApplication class
 * (one C type, qa_remote_application), the client side for joining Q1/QW, Q2,
 * Q3 and unified servers. Built beside the old host shell in
 * remote_application.h (qa_app_remote_application, main.c and startup.c's path
 * until the connection step); the two share nothing.
 *
 * The member layout (TS member, C function, C file, status) is
 * docs/remote_application_layout.md. Every public TS member is declared here so
 * StartupApplication can be written against it; a member whose status there is
 * "not done" has no definition yet. TS async members are synchronous; a TS throw
 * reaches the caller through qa_raise("Error", ...). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_REMOTE_APPLICATION_CLASS_H
#define QA_APP_BOOTSTRAP_REMOTE_APPLICATION_CLASS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/application_class.h"
#include "qa/app/bootstrap/client_bootstrap.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/demo_playback.h"
#include "qa/app/bootstrap/effects.h"
#include "qa/app/bootstrap/network/client_download_policy.h"
#include "qa/app/bootstrap/network/gtv_source.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/server_browser.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/app/bootstrap/view_settings.h"
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/session.h"
#include "qa/contracts/ui.h"
#include "qa/network/common/endpoint.h"
#include "qa/platform/sdl.h"

typedef struct qa_remote_application qa_remote_application;
struct qa_app_configuration_command_request;

/* TS RemoteApplicationHost = ApplicationHost & { serverBrowser? } (and
 * RemoteDemoApplicationHost, whose serverBrowser is required). server_browser
 * NULL is TS undefined; it is borrowed for the application's life. */
typedef struct qa_remote_application_host {
  qa_application_host base;
  qa_app_startup_server_browser *server_browser;
} qa_remote_application_host;

/* TS DemoCompletion ("ended" | "truncated" | ...) callback of openDemoBorrowed. */
typedef void (*qa_remote_demo_complete_fn)(void *ctx, const char *reason);

/* TS static open(options, host) / openBorrowed(client, options, host) /
 * openDemoBorrowed(client, options, host, playback, complete) /
 * openGtvBorrowed(client, options, host, request). options is copied. */
qa_remote_application *qa_remote_application_open(const qa_app_options *options,
    const qa_remote_application_host *host);
qa_remote_application *qa_remote_application_open_borrowed(qa_app_client_bootstrap *client,
    const qa_app_options *options, const qa_remote_application_host *host);
qa_remote_application *qa_remote_application_open_demo_borrowed(qa_app_client_bootstrap *client,
    const qa_app_options *options, const qa_remote_application_host *host,
    const qa_app_demo_resource *resource, bool timedemo, qa_remote_demo_complete_fn complete, void *complete_ctx);
qa_remote_application *qa_remote_application_open_gtv_borrowed(qa_app_client_bootstrap *client,
    const qa_app_options *options, const qa_remote_application_host *host, qa_app_gtv_source_options request);
/* Frees a closed application (TS GC). */
void qa_remote_application_destroy(qa_remote_application *app);

/* TS static recordedOptions(options, resource): the offline options a demo
 * resource is played with. *out is owned (qa_app_options_destroy). */
void qa_remote_application_recorded_options(const qa_app_options *options,
    const qa_app_demo_resource *resource, qa_app_options *out);

/* TS public getters. */
qa_app_startup_server_browser *qa_remote_application_server_browser(const qa_remote_application *app);
qa_app_view_settings *qa_remote_application_view_settings(const qa_remote_application *app);
bool qa_remote_application_presented_cinematic(const qa_remote_application *app);
bool qa_remote_application_has_cinematic(const qa_remote_application *app);
const qa_app_options *qa_remote_application_options(const qa_remote_application *app);
bool qa_remote_application_finished(const qa_remote_application *app);
bool qa_remote_application_client_commands_blocked(const qa_remote_application *app);
/* TS captureMap: NULL is TS null. */
const char *qa_remote_application_capture_map(const qa_remote_application *app);
/* TS content: raises "Remote server has not supplied a world" before the first world. */
qa_app_loaded_content *qa_remote_application_content(const qa_remote_application *app);
double qa_remote_application_frame_count(const qa_remote_application *app);
double qa_remote_application_time_milliseconds(const qa_remote_application *app);
qa_sdl_window *qa_remote_application_window(const qa_remote_application *app);
/* TS networkAddress: raises "Recorded source has no network address" for recordings.
 * The address is a copy (qa_net_address_destroy). */
qa_net_address qa_remote_application_network_address(qa_remote_application *app);
/* TS localPlayers: controls?.locals.map(local => local.player) ?? []. Rows owned by
 * the application until the next call. */
struct qa_app_local_player;
const struct qa_app_local_player *qa_remote_application_local_players(qa_remote_application *app, size_t *out_count);
/* TS downloadProgress: the primary rows, then each open peer's. Rows owned by the
 * application until the next call. */
const qa_app_client_download_progress *qa_remote_application_download_progress(qa_remote_application *app,
    size_t *out_count);
/* TS networkPhase. The recorded and GTV phases are their TS strings. */
const char *qa_remote_application_network_phase(const qa_remote_application *app);
const qa_app_sim_presentation_event *qa_remote_application_presentation_events(const qa_remote_application *app,
    size_t *out_count);
const qa_app_unhandled_effect *qa_remote_application_unhandled_presentation_effects(const qa_remote_application *app,
    size_t *out_count);
qa_engine_session *qa_remote_application_session(const qa_remote_application *app);

/* TS public methods. */
void qa_remote_application_pump_client_input(qa_remote_application *app);
bool qa_remote_application_advance_client_startup(qa_remote_application *app);
void qa_remote_application_flush_client_commands(qa_remote_application *app);
void qa_remote_application_prepare_retirement(qa_remote_application *app);
void qa_remote_application_release_settings(qa_remote_application *app);
void qa_remote_application_retire(qa_remote_application *app);
/* TS readClientResource: false is TS undefined; *out_bytes is malloc'd. */
bool qa_remote_application_read_client_resource(qa_remote_application *app, const char *path,
    uint8_t **out_bytes, size_t *out_length);
bool qa_remote_application_input(qa_remote_application *app, const qa_seat_input_event *event);
/* TS queueCommand(name, args, seat, source = clientCommands?.commands.executionContext).
 * seat NULL is TS null; source NULL is the TS default argument. */
void qa_remote_application_queue_command(qa_remote_application *app, const char *name,
    const char *const *args, size_t arg_count, const qa_seat_id *seat, const qa_command_context *source);
void qa_remote_application_cancel_downloads(qa_remote_application *app, const qa_seat_id *seat);
void qa_remote_application_retry_downloads(qa_remote_application *app, const qa_seat_id *seat);
void qa_remote_application_execute_application_request(qa_remote_application *app,
    const struct qa_app_configuration_command_request *request);
/* TS step(elapsedMilliseconds): false is the TS null result; *out borrows
 * the remote's storage until the next step. */
bool qa_remote_application_step(qa_remote_application *app, double elapsed_milliseconds,
    qa_simulation_output *out);
void qa_remote_application_request_quit(qa_remote_application *app);
void qa_remote_application_run(qa_remote_application *app);
/* TS close(). A second close rethrows the first close's error. */
void qa_remote_application_close(qa_remote_application *app);

/* TS attachConnectingRecording(sink): returns the detach token; the TS detach
 * closure is qa_remote_application_detach_connecting_recording(app, token). */
#include "qa/app/bootstrap/network/qw_client.h"
void *qa_remote_application_attach_connecting_recording(qa_remote_application *app, qa_app_qw_recording_sink sink);
void qa_remote_application_detach_connecting_recording(qa_remote_application *app, void *token);
#endif
