/* Port of src/app/bootstrap/client-bootstrap.ts. This module is the
 * client composition interface set; concrete owners are assembled by
 * startup/application. localClientAccount's WeakMap becomes an
 * explicit registry here. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CLIENT_BOOTSTRAP_H
#define QA_APP_BOOTSTRAP_CLIENT_BOOTSTRAP_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/common.h"
#include "qa/contracts/content.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/render.h"
/* TS Account (network/services identity subset read here). */
typedef struct qa_app_local_account { char *id, *name; } qa_app_local_account;
/* TS ClientSourcePublicationError message. */
extern const char qa_app_client_source_publication_error[];
/* TS localClientAccount: stable per identity owner; id bytes come
 * from the host random source (16 bytes -> hex). */
typedef struct qa_app_local_accounts qa_app_local_accounts;
qa_app_local_accounts *qa_app_local_accounts_create(void (*random_bytes)(void *ctx, unsigned char out[16]), void *random_ctx);
void qa_app_local_accounts_destroy(qa_app_local_accounts *accounts);
/* Borrowed account for the owner (created on first use). */
const qa_app_local_account *qa_app_local_client_account(qa_app_local_accounts *accounts, const qa_identity_owner *identity);
/* TS ClientBootstrap / ClientSourceLifetime surfaces as fn+ctx rows
 * (implemented by Application in application.h). */
typedef struct qa_app_client_recording_feed {
  void *ctx;
  const char *root;
  const char *(*seed)(void *ctx, const void **out_seed);
  const void *(*attach)(void *ctx, const void *sink);
  void (*detach)(void *ctx, const void *token);
} qa_app_client_recording_feed;
typedef struct qa_app_client_source_lifetime {
  void *ctx;
  const char *(*prepare_recording)(void *ctx, qa_command_context source, qa_app_client_recording_feed *out);
  const char *(*prepare_video_restart)(void *ctx, const void **out_prepared);
  const char *capture_map; /* NULL mirrors TS null */
  const char *(*prepare_retirement)(void *ctx);
  void (*release_settings)(void *ctx);
  const char *(*retire)(void *ctx);
} qa_app_client_source_lifetime;
/* OS-random registry (TS node:crypto randomBytes(16)); NULL on OOM. */
qa_app_local_accounts *qa_app_local_accounts_create_os_random(void);
/* ClientSourceLifetime forwarding (TS `source?.method()` sites): a
 * NULL lifetime or NULL fn is a no-op returning NULL (no error), so
 * shrunk lifetime rows stay callable. Borrowed error text. */
const char *qa_app_client_source_prepare_recording(const qa_app_client_source_lifetime *lifetime,
    qa_command_context source, qa_app_client_recording_feed *out);
const char *qa_app_client_source_prepare_video_restart(const qa_app_client_source_lifetime *lifetime,
    const void **out_prepared);
const char *qa_app_client_source_prepare_retirement(const qa_app_client_source_lifetime *lifetime);
void qa_app_client_source_release_settings(const qa_app_client_source_lifetime *lifetime);
const char *qa_app_client_source_retire(const qa_app_client_source_lifetime *lifetime);
/* TS capture map fallback: lifetime->capture_map ?? "menu". */
const char *qa_app_client_source_capture_map(const qa_app_client_source_lifetime *lifetime);
/* --- TS ClientBootstrap members that ApplicationInput reads and writes
 * (input.ts prepareForClient / retireLocalSeats / publishClientSeats /
 * publishClientPlatform). The application owns the struct; the other
 * TS members join it as their readers are ported. --- */
struct qa_app_application_input;
struct qa_input_router;
struct qa_app_controller_settings;
struct qa_app_prepared_startup;
struct qa_sdl_controllers;
struct qa_app_input_devices;
struct qa_seat_console;
struct qa_session_client;
struct qa_session_seat;
/* TS ClientInputPublication | null. */
typedef enum qa_app_client_input_publication_kind {
  QA_APP_CLIENT_INPUT_NONE, /* TS null */
  QA_APP_CLIENT_INPUT_MENU,
  QA_APP_CLIENT_INPUT_WORLD
} qa_app_client_input_publication_kind;
typedef struct qa_app_client_input_publication {
  qa_app_client_input_publication_kind kind;
  /* menu */
  struct qa_input_router *router;
  struct qa_app_controller_settings *controller_settings;
  void *ctx;
  void (*retire_commands)(void *ctx);
  /* world */
  struct qa_app_application_input *input;
} qa_app_client_input_publication;
/* TS ClientBootstrapSeat. prepared names the PreparedSeat by id (the C
 * prepared seat rows are reallocated by publishSeats). */
typedef struct qa_app_client_bootstrap_seat {
  struct qa_session_client *client;
  struct qa_session_seat *seat;
  qa_seat_id prepared;
} qa_app_client_bootstrap_seat;
/* TS consoles Map<SessionSeat, SeatConsole> entry. The console is held
 * for the client (qa_app_input_release_holder(client) drops the holds). */
typedef struct qa_app_client_console_entry {
  struct qa_session_seat *seat;
  struct qa_seat_console *console;
} qa_app_client_console_entry;
struct qa_app_configuration_command_request;
struct qa_app_application_local_lobby;
struct qa_config_store;
struct qa_render_command;
struct qa_active_caption;
struct qa_command_buffer;
/* TS activateFrontend's configuration { releaseCommands: Pick<CommandBuffer, "append">; publish() }. */
struct qa_command_append_target;
typedef struct qa_app_client_frontend_configuration {
  struct qa_command_buffer *release_commands;
  /* TS releaseCommands when it is a prepared program's release target (NULL: release_commands). */
  const struct qa_command_append_target *release_target;
  void *ctx;
  void (*publish)(void *ctx);
} qa_app_client_frontend_configuration;
typedef struct qa_app_client_bootstrap {
  const void *window;                       /* renderer.window identity (input window ctx) */
  struct qa_app_prepared_startup *prepared;
  qa_app_client_input_publication platform; /* platform.current */
  struct qa_sdl_controllers *controllers;
  struct qa_app_input_devices *input_devices;
  qa_app_client_bootstrap_seat *locals; size_t local_count, local_capacity; /* malloc'd */
  qa_app_client_console_entry *consoles; size_t console_count, console_capacity; /* malloc'd */
  /* TS configuration.current { scripts, options } (RemoteApplication reads and
   * repoints it). Borrowed; options NULL before the first publication. */
  struct {
    struct qa_app_console_script_files *scripts;
    const struct qa_app_options *options;
  } configuration_current;
  /* The TS members Application reads (openBorrowed, publishBorrowed, closeOwned,
   * changeNetworkWorld). Every pointer is borrowed from the client owner. */
  struct qa_app_keys *keys;
  struct qa_app_client_demo_recording *recording;
  /* TS stopRecording(source): returns error text or NULL. */
  void *stop_recording_ctx;
  const char *(*stop_recording)(void *ctx, const qa_app_client_source_lifetime *source);
  struct qa_app_video_restart *video_restart;
  struct qa_music_controls *music_controls;
  struct qa_app_capture *capture;
  qa_identity_owner *identity;
  struct qa_engine_session *session;
  struct qa_app_image_settings *image_settings;
  /* TS source.current / sourceProfile.current: NULL is TS null. */
  const qa_app_client_source_lifetime *source_current;
  bool has_source_profile; qa_provider_reference source_profile;
  /* TS hasPendingSource, routeCommand(name, args, source) and
   * dispatchApplicationRequest(request), the members Application's command
   * dispatch reads (ctx is bootstrap_ctx). */
  void *bootstrap_ctx;
  bool (*has_pending_source)(void *ctx);
  bool (*route_command)(void *ctx, const char *name, const char *const *args, size_t count, const qa_command_context *source);
  void (*dispatch_application_request)(void *ctx, const struct qa_app_configuration_command_request *request);
  /* TS renderer (the client's NativeRenderer; borrowed) and output.current (the published UnifiedAudio,
   * written by qa_app_client_bootstrap_publish_output). */
  struct qa_app_native_renderer *renderer;
  struct qa_unified_audio *output_current;
  /* TS localLobby? (NULL is TS undefined), settings (the client ConfigStore). */
  struct qa_app_application_local_lobby *local_lobby;
  struct qa_config_store *settings;
  /* TS captionCommands(captions, viewport, timeMilliseconds): the rows are owned by
   * the client and borrowed until the next call (ctx is bootstrap_ctx). */
  const struct qa_render_command *(*caption_commands)(void *ctx, const struct qa_active_caption *captions,
      size_t caption_count, qa_rect viewport, double time_milliseconds, size_t *out_count);
  /* TS activateFrontend(configuration?): configuration NULL is TS undefined (ctx is bootstrap_ctx). */
  void (*activate_frontend)(void *ctx, const struct qa_app_client_frontend_configuration *configuration);
} qa_app_client_bootstrap;
/* TS client.output.current = engine. */
void qa_app_client_bootstrap_publish_output(qa_app_client_bootstrap *client, struct qa_unified_audio *engine);
#endif
