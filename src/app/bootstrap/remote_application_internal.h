/* Port of src/app/bootstrap/remote-application.ts: the struct (every TS field,
 * TS order), the types remote-application.ts defines, and every member's
 * declaration. Status per member: docs/remote_application_layout.md; members
 * marked "not done" there are declared here and not yet defined.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_REMOTE_APPLICATION_INTERNAL_H
#define QA_APP_BOOTSTRAP_REMOTE_APPLICATION_INTERNAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/remote_application_class.h"
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/campaign_cinematic.h"
#include "qa/app/bootstrap/capture.h"
#include "qa/app/bootstrap/config_scripts.h"
#include "qa/app/bootstrap/configuration.h"
#include "qa/app/bootstrap/demo_recording_commands.h"
#include "qa/app/bootstrap/effects.h"
#include "qa/app/bootstrap/frame_clock.h"
#include "qa/app/bootstrap/image_settings.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/keys.h"
#include "qa/app/bootstrap/network/client_download_policy.h"
#include "qa/app/bootstrap/network/gtv_source.h"
#include "qa/app/bootstrap/network/q1_client.h"
#include "qa/app/bootstrap/network/q1_demo.h"
#include "qa/app/bootstrap/network/q2_demo.h"
#include "qa/app/bootstrap/network/q3_demo.h"
#include "qa/app/bootstrap/network/q2.h"
#include "qa/app/bootstrap/network/q3_client.h"
#include "qa/app/bootstrap/network/q3_client_content.h"
#include "qa/app/bootstrap/network/q3_client_downloads.h"
#include "qa/app/bootstrap/network/qw_client.h"
#include "qa/app/bootstrap/network/qw_downloads.h"
#include "qa/app/bootstrap/network/recorded_source.h"
#include "qa/app/bootstrap/network/remote.h"
#include "qa/app/bootstrap/network/remote_q1.h"
#include "qa/app/bootstrap/network/remote_q3.h"
#include "qa/app/bootstrap/network/remote_qw.h"
#include "qa/app/bootstrap/network/remote_unified.h"
#include "qa/app/bootstrap/network/socks_settings.h"
#include "qa/app/bootstrap/network/transport.h"
#include "qa/app/bootstrap/network/unified_client.h"
#include "qa/app/bootstrap/player_progress.h"
#include "qa/app/bootstrap/prepared_startup.h"
#include "qa/app/bootstrap/presentation.h"
#include "qa/app/bootstrap/q3_client.h"
#include "qa/app/bootstrap/remote_access.h"
#include "qa/app/bootstrap/remote_components.h"
#include "qa/app/bootstrap/remote_seat_identities.h"
#include "qa/app/bootstrap/remote_seat_pump.h"
#include "qa/app/bootstrap/remote_seat_source.h"
#include "qa/app/bootstrap/remote_seat_view.h"
#include "qa/app/bootstrap/remote_seats.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/app/bootstrap/video_restart.h"
#include "qa/app/bootstrap/view_settings.h"
#include "qa/audio/engine.h"
#include "qa/audio/music.h"
#include "qa/core/commands/documentation.h"
#include "qa/core/commands/index.h"
#include "qa/core/cvars/index.h"
#include "qa/foundation/event_ring.h"
#include "qa/network/q3/browser_view.h"
#include "qa/render/scene/resources.h"
#include "qa/settings/config.h"
#include "qa/text/atlas.h"
#include "qa/ui/common/assets.h"
#include "qa/ui/settings/index.h"
#include "qa/world/collision/q3/settings.h"
#include "qa/world/session/session.h"

/* ---- TS GC lifetimes -------------------------------------------------- */
/* Objects the TS keeps alive through closures (candidate registries,
 * transfers, created seat inputs, configuration records) are released when
 * the application is dropped. The list runs newest first. */
typedef struct qa_remote_arena_entry { void (*release)(void *object); void *object; } qa_remote_arena_entry;
typedef struct qa_remote_arena { qa_remote_arena_entry *entries; size_t count, capacity; } qa_remote_arena;
void qa_remote_arena_add(qa_remote_arena *arena, void (*release)(void *object), void *object);
void qa_remote_arena_release(qa_remote_arena *arena);

/* ---- module types (TS 137-214) ---------------------------------------- */
/* TS RemoteWorldFrontend. */
typedef struct qa_remote_world_frontend {
  qa_app_application_assets *assets;
  qa_text_font_selection font;
  qa_native_ui_art *art;
  qa_app_application_audio *audio;
  qa_app_application_effects *effects;
  qa_scene_queries scene;
} qa_remote_world_frontend;

/* TS RemoteSeatPeer. */
typedef struct qa_remote_seat_peer {
  qa_session_seat *seat;
  qa_app_remote_seat_source *source;
  qa_app_remote_seat_channel *channel;
  qa_app_remote_content_mounts *content;   /* NULL = TS null */
  char *map;                                /* NULL = TS null */
  qa_app_loaded_content *loaded_content;   /* NULL = TS null */
  qa_app_remote_seat_view *view;           /* NULL = TS null */
  double generation;
  bool closed;
  qa_app_remote_seat_pump *pump;
  char *disconnected;                       /* NULL = TS null */
  void (*cinematic_ended)(void *ctx); void *cinematic_ended_ctx; /* NULL = TS null */
  char *userinfo;                           /* NULL = TS null */
  qa_simulation_output sample_scratch;      /* C storage of the unified samplePresentation */
  bool cinematic_pending;                   /* C: cinematic_ended was set by a pump task */
} qa_remote_seat_peer;

/* TS RemoteCommand. The context is deep-copied (its script origin chain and
 * names are owned), so a queued command outlives the script that queued it. */
typedef struct qa_remote_command {
  char *name;
  char **args; size_t arg_count;
  bool has_seat; qa_seat_id seat;
  bool has_source; qa_command_context source;
} qa_remote_command;
void qa_remote_command_free(qa_remote_command *command);

/* TS MutableRemoteCommandOwner (RemoteCommandOwner is its read view). The
 * published owner keeps one identity across travel; its fields are repointed
 * in place. movement/fallback/input_settings NULL are TS undefined. */
typedef struct qa_remote_command_owner {
  qa_cvar_registry *movement, *fallback;
  qa_app_console_script_files *scripts;
  qa_cvar_registry *cvars;
  qa_command_buffer *commands;
  qa_command_cvar_routing routing;
  qa_mouse_settings *input_settings;
} qa_remote_command_owner;

/* TS RemoteOwnership. */
typedef struct qa_remote_ownership {
  bool borrowed;                    /* false = { kind: "owned" } */
  qa_app_client_bootstrap *client;  /* borrowed */
} qa_remote_ownership;

/* TS Map<SeatId, CvarRegistry> / Map<SeatId, ClientDownloadPermission|null>
 * entries, in insertion order. */
typedef struct qa_remote_seat_source_entry { qa_seat_id id; qa_cvar_registry *cvars; } qa_remote_seat_source_entry;
typedef struct qa_remote_seat_permission_entry {
  qa_seat_id id; qa_app_client_download_permission *permission; /* NULL = TS null */
} qa_remote_seat_permission_entry;

/* TS ReturnType<ApplicationImageSettings["prepareClientSettings"]>. */
typedef struct qa_remote_client_images {
  qa_app_image_settings *settings;
  qa_cvar_transfer *transfer; /* validatePublication / publish */
} qa_remote_client_images;

/* One staged registry: TS transfers Map<candidate, { owner, transfer }>. */
typedef struct qa_remote_staged_transfer { qa_cvar_registry *owner; qa_cvar_transfer *transfer; } qa_remote_staged_transfer;

/* TS RemoteConfiguration. The record and the closures' state are arena-owned
 * (TS GC); qa_remote_configuration_routing returns the TS `routing` member. */
typedef struct qa_remote_configuration {
  qa_app_prepared_startup *prepared;
  qa_app_prepared_profile *profile;
  qa_session_seat *seat;
  qa_session_seat **seats; size_t seat_count;
  qa_remote_seat_source_entry *seat_sources; size_t seat_source_count;
  qa_app_remote_seat_identities *identities;
  qa_config_store *settings;
  qa_app_client_socks_settings *socks;
  qa_remote_client_images images;
  qa_app_client_download_permission *download_permission; /* NULL = TS null */
  qa_remote_seat_permission_entry *download_permissions; size_t download_permission_count;
  qa_cvar_registry *source;
  qa_command_cvar_routing routing;
  /* closure state of prepareTransport / validateSource / publishSource */
  qa_remote_staged_transfer *transfers; size_t transfer_count;
  qa_cvar_transfer *staged_socks; /* NULL = TS null */
  qa_command_cvar_routing profile_routing; /* socks.route(profile.routing) */
  qa_cvar_registry **visible; size_t visible_capacity; /* routing.visible result storage */
  void *peer; /* a publishPeerConfiguration record: its validateSource/publishSource/prepareTransport (NULL otherwise) */
} qa_remote_configuration;
/* TS configuration.prepareTransport(transport). */
void qa_remote_configuration_prepare_transport(qa_remote_configuration *configuration, qa_socks_connector transport);
/* TS configuration.validateSource() / publishSource(). */
void qa_remote_configuration_validate_source(qa_remote_configuration *configuration);
void qa_remote_configuration_publish_source(qa_remote_configuration *configuration);
/* TS seatSources.get(id): NULL = undefined. */
qa_cvar_registry *qa_remote_configuration_seat_source(const qa_remote_configuration *configuration, qa_seat_id id);

/* TS prepareRemoteConfiguration(options, content, session, imageSettings,
 * ownership, host, cheatsAllowed, current?, selectedContent?). current and
 * selected NULL are TS undefined. options is borrowed for the call (the
 * profile copies it). Created owners are released through arena. */
typedef int (*qa_remote_cheats_allowed_fn)(void *ctx);
qa_remote_configuration *qa_remote_prepare_configuration(const qa_app_options *options,
    qa_app_configuration_content *content, qa_engine_session *session, qa_app_image_settings *image_settings,
    qa_remote_ownership ownership, const qa_application_host *host,
    qa_remote_cheats_allowed_fn cheats_allowed, void *cheats_ctx,
    qa_remote_configuration *current, qa_app_configuration_content *selected_content, qa_remote_arena *arena);

/* TS PeerDirectoryAdmission. */
typedef struct qa_remote_peer_admission {
  qa_app_remote_content_mounts *content;
  qa_app_options options;
  qa_app_assert_current_fn assert_current; void *assert_ctx;
  bool published; /* phase: false "requested", true "published" */
  bool has_suspension;
  bool resumed; bool cancelled; char *cancel_error;
} qa_remote_peer_admission;

/* TS PendingRemoteAdvance / RemoteAdvanceOutcome as pumped state: the
 * operation runs synchronously; a boundary is a flag it raises. */
typedef struct qa_remote_pending_advance {
  bool boundary;
  bool complete;
  bool failed; qa_caught_error error;
} qa_remote_pending_advance;

/* TS RemoteCinematic. */
typedef struct qa_remote_cinematic {
  double request;
  bool (*current)(void *ctx); void *current_ctx;
  qa_app_campaign_cinematic *movie;
  qa_scene_image_registry *images;
  qa_unified_audio *owned_audio; /* NULL = TS null */
  void (*ended)(void *ctx); void *ended_ctx;
} qa_remote_cinematic;

/* TS RemoteBrowser. */
typedef struct qa_remote_browser { bool owned; qa_app_startup_server_browser *browser; } qa_remote_browser;

/* TS LiveNetwork: the RemoteSeatNetwork union (remote_seat_source.h). */
typedef qa_app_remote_seat_network qa_remote_live_network;

/* TS RemoteSource (DemoLaunch/GtvLaunch/RemoteLaunch share its kinds). */
typedef enum qa_remote_source_kind { QA_REMOTE_SOURCE_LIVE, QA_REMOTE_SOURCE_RECORDED, QA_REMOTE_SOURCE_GTV } qa_remote_source_kind;
typedef struct qa_remote_source {
  qa_remote_source_kind kind;
  qa_remote_live_network network;           /* live */
  qa_app_application_transport *transport;  /* live */
  qa_app_recorded_source *playback;         /* recorded */
  qa_app_gtv_remote_source *watch;          /* gtv */
  char *label;                              /* gtv */
} qa_remote_source;
/* TS RemoteLaunch: the source plus what the constructor reads. */
typedef struct qa_remote_launch {
  qa_remote_source_kind kind;
  bool unified; qa_app_demo_family family; qa_net_address address; /* live */
  qa_app_application_transport *transport;                           /* live */
  const qa_app_demo_resource *resource; bool timedemo;               /* recorded */
  qa_remote_demo_complete_fn complete; void *complete_ctx;           /* recorded */
  qa_app_gtv_remote_source *watch; const char *label;                /* gtv */
  const bool *aborted;                                               /* gtv: TS signal (NULL = undefined) */
} qa_remote_launch;

/* The initial remote presentation (TS union of the five remote classes). */
typedef qa_app_remote_seat_presentation qa_remote_presentation;

/* TS clientInputs entry. */
typedef struct qa_remote_client_input {
  double generation; qa_app_application_q3_client *client; qa_seat_input_event event;
} qa_remote_client_input;

/* TS retiredFrontends EventRing entry. */
typedef struct qa_remote_retired_frontend {
  qa_remote_world_frontend *frontend; qa_app_loaded_content *content; /* content NULL = TS null */
} qa_remote_retired_frontend;

/* TS sourceHandlers / sourceDocumentation Map entries (insertion order). */
typedef struct qa_remote_source_handler {
  char *name;
  qa_command_handler_fn handler; void *handler_ctx;
  bool has_documentation; qa_command_documentation documentation;
} qa_remote_source_handler;

/* ---- the class --------------------------------------------------------- */
struct qa_remote_application {
  /* First member: the ctx every remote presentation accessor of remote_access.h
   * receives (the application is the presentations' ctx). */
  qa_app_remote_access access;
  /* constructor parameters (TS `private` parameter properties) */
  qa_app_options launch_options;                /* owned */
  qa_app_configuration_content *mounted_content;
  qa_engine_session *session;
  qa_app_native_renderer *renderer;
  qa_remote_application_host host;
  qa_app_image_settings *image_settings;
  const qa_identity_owner *identity;
  qa_remote_browser browser;
  qa_remote_ownership ownership;
  qa_remote_configuration *configuration;
  qa_app_keys *keys;
  /* fields, TS order (216-312) */
  qa_app_remote_component_view *initial_components;
  qa_remote_command_owner *client_command_owner;   /* NULL = TS null */
  qa_remote_command_owner client_command_owner_storage;
  qa_app_view_settings *view_settings;
  bool release_view_cvars;                         /* TS releaseViewCvars !== null */
  qa_app_client_socks_settings *socks_settings;
  qa_config_store *client_config;                  /* NULL = TS null */
  qa_config_store *input_config;
  qa_app_player_progress_store *player_progress;   /* NULL = TS null */
  qa_app_client_download_permission *download_permission; /* NULL = TS null */
  qa_remote_presentation initial_remote;
  qa_remote_seat_peer *primary_peer;               /* NULL = TS null */
  qa_remote_source initial_source;
  qa_remote_seat_peer *promoted_peer; qa_remote_source promoted_source; /* TS promotedSource */
  qa_app_demo_family family;
  qa_app_loaded_content *loaded_content;           /* NULL = TS null */
  qa_remote_world_frontend *frontend;              /* NULL = TS null */
  qa_remote_cinematic *cinematic;                  /* NULL = TS null */
  double cinematic_request;
  bool cinematic_presented;
  qa_event_ring *retired_frontends;                /* qa_remote_retired_frontend, capacity 1024 */
  qa_app_application_input *controls;              /* NULL = TS null */
  qa_app_capture *capture;                         /* NULL = TS null */
  qa_app_video_restart *owned_video_restart;       /* NULL = TS null */
  qa_app_world_seat_presentation *initial_presentation; /* NULL = TS null */
  qa_remote_command *commands; size_t command_count, command_capacity;
  double elapsed;
  qa_app_presentation_time presentation_time;
  double frames;
  bool stopping, closed, closing;
  bool close_started; bool close_failed; qa_caught_error close_error; /* TS closeResult */
  qa_app_client_demo_recording *owned_recording;   /* NULL = TS null */
  double world_load_generation;
  double primary_view_generation;
  bool stepping;
  qa_app_q3_client_content *q3_content;            /* NULL = TS null */
  void *q3_content_host;                           /* q3_content's host (remote_application_world.c) */
  char *qw_all_skins;
  qa_app_qw_download_receiver *qw_downloads;       /* NULL = TS null */
  qa_app_remote_content_mounts *remote_content;    /* NULL = TS null */
  double remote_content_generation;
  qa_remote_peer_admission *peer_admission;        /* NULL = TS null */
  qa_remote_pending_advance *pending_advance;      /* NULL = TS null */
  bool reset_frame_elapsed;
  qa_app_q3_client_downloads *q3_downloads;        /* NULL = TS null */
  bool q3_initial_view_pending;
  qa_remote_client_input *client_inputs; size_t client_input_count, client_input_capacity;
  qa_q3_browser_view *q3_browser;                  /* NULL = TS null */
  qa_app_startup_q3_browser_host *q3_browser_host; /* the StartupServerBrowser view the Q3BrowserView reads */
  qa_app_sim_presentation_event *source_events; size_t source_event_count;
  qa_app_unhandled_effect *unhandled_effects; size_t unhandled_effect_count;
  char **reported_effect_gaps; size_t reported_effect_gap_count, reported_effect_gap_capacity;
  qa_session_connection *source_connection;        /* NULL = TS null */
  bool publication_started;
  bool configuration_published;
  qa_remote_source_handler *source_handlers; size_t source_handler_count, source_handler_capacity;
  qa_seat_id initial_seat_id;
  bool has_ui_preferences; qa_ui_preference_values ui_preferences;
  qa_remote_seat_peer **remote_seats; size_t remote_seat_count, remote_seat_capacity;
  uint16_t *qports; size_t qport_count, qport_capacity; /* TS Set<number>, insertion order */
  bool has_initial_qport; uint16_t initial_qport;
  char *initial_userinfo;                          /* NULL = TS null */
  qa_music_controls *music_controls;
  qa_music_controls owned_music_controls;
  qa_app_key_profile *key_profile;                 /* NULL = TS null */
  /* The ClientSourceLifetime this application publishes as client.source.current
   * (TS `this`): ownsPublishedSource compares the published pointer with it. */
  qa_app_client_source_lifetime source_lifetime;
  /* C storage of publishRemote's filtered events */
  qa_simulation_event *published_events; size_t published_event_capacity;
  /* C storage of localPlayers / downloadProgress results (borrowed until the next call) */
  qa_app_local_player *local_player_rows; size_t local_player_capacity;
  qa_app_client_download_progress *progress_rows; size_t progress_capacity;
  /* TS GC lifetimes */
  qa_remote_arena arena;
  /* random source for allocateQport (TS crypto.getRandomValues); NULL = OS random */
  uint16_t (*random_u16)(void *ctx); void *random_ctx;
  /* C storage the constructor's closures return or hold (TS GC lifetimes) */
  qa_collision_map_settings collision;           /* Q3 bindCollisionSettings */
  char *q1_seat_name;                            /* Q1ClientNetwork seat.name */
  char *returned_text;                           /* userinfo/baseskin results, until the next call */
  qa_remote_launch launch_storage;               /* the RemoteLaunch the constructor read */
  bool key_profile_published;                    /* keys.publish took key_profile */
  /* owned by openSource for an owned application (NULL when borrowed) */
  qa_identity_owner *owned_identity;
  qa_app_keys *owned_keys;
  qa_app_configuration_content *owned_mounted;
  /* the per-download bindings of prepareQwDownloads / prepareQ3Downloads */
  struct qa_remote_qw_binding *qw_binding;
  struct qa_remote_q3_binding *q3_binding;
  qa_datagram_transport datagram;                /* the initial transport as a datagram transport */
  qa_config_store *browser_settings;             /* the owned browser's ConfigStore */
  qa_simulation_output sample_scratch;           /* C storage of the unified samplePresentation */
  qa_app_sim_presentation_access remote_view;    /* the remote as the presentation access (TS passes the remote) */
  qa_app_sim_presentation_access source_view;    /* { playerView: actor => sourceForActor(actor).playerView(actor) } */
  char lifetime_error[QA_CAUGHT_MESSAGE_CAPACITY]; /* the ClientSourceLifetime's error text */
  qa_app_prepared_video_presentation lifetime_video;
  qa_remote_world_frontend *closed_frontend;     /* the frontend closeOwned closed, freed with the application */
};

/* ---- members (TS order) ------------------------------------------------ */
/* core */
qa_remote_command_owner *qa_remote_application_client_commands(const qa_remote_application *app);
const qa_remote_source *qa_remote_application_source(qa_remote_application *app);
const qa_remote_live_network *qa_remote_application_network(qa_remote_application *app);
void qa_remote_application_send_command(qa_remote_application *app, const char *text);
/* TS get remote(): primaryPeer?.source.remote ?? initialRemote. */
const qa_remote_presentation *qa_remote_application_remote(const qa_remote_application *app);
qa_app_world_seat_presentation *qa_remote_application_presentation(const qa_remote_application *app);
void qa_remote_application_set_presentation(qa_remote_application *app, qa_app_world_seat_presentation *value);
bool qa_remote_application_primary_view_current(const qa_remote_application *app);
qa_seat_id qa_remote_application_seat_id(const qa_remote_application *app);
uint16_t qa_remote_application_allocate_qport(qa_remote_application *app);
qa_app_video_restart *qa_remote_application_video_restart(const qa_remote_application *app);
qa_content_mounted *qa_remote_application_mounts(const qa_remote_application *app);
void qa_remote_application_print(qa_remote_application *app, const char *text, const qa_command_context *source);
void qa_remote_application_view_fov_changed(void *ctx, double value);
/* open */
qa_remote_application *qa_remote_application_construct(qa_app_options launch_options,
    qa_app_configuration_content *mounted_content, qa_engine_session *session, qa_app_native_renderer *renderer,
    const qa_remote_application_host *host, qa_app_image_settings *image_settings, const qa_remote_launch *launch,
    const qa_identity_owner *identity, qa_remote_browser browser, qa_remote_ownership ownership,
    qa_remote_configuration *configuration, qa_app_keys *keys, qa_app_key_profile *key_profile);
qa_remote_source qa_remote_application_recorded_source(qa_remote_application *app, const qa_remote_launch *launch);
qa_remote_application *qa_remote_application_open_source(qa_remote_ownership ownership, const qa_app_options *options,
    const qa_remote_application_host *host, const qa_remote_launch *recording);
/* profile */
bool qa_remote_application_owns_published_source(qa_remote_application *app);
void qa_remote_application_publish_remote(qa_remote_application *app, const qa_simulation_output *output);
void qa_remote_application_disconnect_source(qa_remote_application *app);
void qa_remote_application_activate_source_commands(qa_remote_application *app);
void qa_remote_application_clear_source_presentation(qa_remote_application *app);
void qa_remote_application_adopt_unified_profile(qa_remote_application *app, qa_app_loaded_content *content,
    const char *mode, qa_app_assert_current_fn assert_current, void *assert_ctx);
void qa_remote_application_publish_peer_configuration(qa_remote_application *app, qa_remote_peer_admission *request);
bool qa_remote_application_advance_source(qa_remote_application *app, void (*operation)(void *ctx), void *ctx);
void qa_remote_application_present_loading_frame(qa_remote_application *app);
void qa_remote_application_publish_configuration(qa_remote_application *app, qa_remote_configuration *configuration);
void qa_remote_application_publish_connecting(qa_remote_application *app, qa_app_client_bootstrap *client,
    const bool *aborted);
/* world */
void qa_remote_application_save_source_settings(qa_remote_application *app);
void qa_remote_application_initialize_q3_browser(qa_remote_application *app);
bool qa_remote_application_browser_command(qa_remote_application *app, const char *name, const char *const *args,
    size_t arg_count, void (*print)(void *ctx, const char *text), void *print_ctx);
qa_remote_world_frontend *qa_remote_application_load_frontend(qa_remote_application *app, qa_app_loaded_content *content);
void qa_remote_application_release_frontend(qa_remote_application *app, qa_remote_world_frontend *frontend);
void qa_remote_application_retire_frontends(qa_remote_application *app);
void qa_remote_application_refresh_images(qa_remote_application *app);
qa_app_remote_content_mounts *qa_remote_application_select_remote_content(qa_remote_application *app,
    const qa_remote_content_selection *selection, qa_app_assert_current_fn assert_peer_current, void *assert_ctx);
qa_app_remote_content_mounts *qa_remote_application_refresh_remote_content(qa_remote_application *app,
    qa_app_assert_current_fn assert_current, void *assert_ctx);
void qa_remote_application_prepare_qw_downloads(qa_remote_application *app, const char *game_directory);
bool qa_remote_application_prepare_q3_downloads(qa_remote_application *app, qa_q3_client_connection *connection);
/* Releases one owner of a Q3ClientContent host (remote_application_world.c); NULL is ignored. */
void qa_remote_q3_content_host_release(void *host);
qa_app_loaded_content *qa_remote_application_load_q3_server_world(qa_remote_application *app,
    const qa_app_q1_remote_world_info *world, qa_q3_client_connection *connection);
qa_app_loaded_content *qa_remote_application_load_server_world(qa_remote_application *app,
    const qa_app_q1_remote_world_info *world, const char *const *images, size_t image_count,
    qa_app_loaded_content *prepared_content, bool refresh_content);
/* seat */
void qa_remote_application_bind_seat(qa_remote_application *app, qa_q3_client_connection *connection);
/* cinematic */
void qa_remote_application_start_cinematic(qa_remote_application *app, const char *name, bool loop, bool hold,
    bool silent, void (*ended)(void *ctx), void *ended_ctx);
void qa_remote_application_activate_cinematic(qa_remote_application *app, qa_remote_cinematic *movie);
void qa_remote_application_stop_cinematic(qa_remote_application *app);
void qa_remote_application_complete_cinematic(qa_remote_application *app, qa_remote_cinematic *movie);
void qa_remote_application_close_cinematic(qa_remote_application *app, qa_remote_cinematic *current);
/* commands */
void qa_remote_application_dispatch_commands(qa_remote_application *app);
qa_app_client_demo_recording *qa_remote_application_standalone_recording(qa_remote_application *app);
void qa_remote_application_execute_remote_command(qa_remote_application *app, const qa_remote_command *command);
bool qa_remote_application_underwater(qa_remote_application *app, qa_scene_camera camera, qa_actor_id actor,
    const qa_scene_queries *scene);
/* commands: TS primaryDownloadProgress (rows appended to the application's progress storage) */
size_t qa_remote_application_primary_download_progress(qa_remote_application *app, size_t at);
/* seats */
void qa_remote_application_drain_seat_publications(qa_remote_application *app);
/* TS sourceForActor(actor): raises "Remote input actor has no active native channel". */
const qa_remote_presentation *qa_remote_application_source_for_actor(qa_remote_application *app, qa_actor_id actor);
qa_rect qa_remote_application_seat_viewport(qa_remote_application *app, qa_seat_id id);
void qa_remote_application_publish_remote_layouts(qa_remote_application *app);
void qa_remote_application_join_remote_seat(qa_remote_application *app);
void qa_remote_application_drop_remote_seat(qa_remote_application *app, int64_t index);
void qa_remote_application_prepare_remote_seats(qa_remote_application *app, qa_remote_configuration *configuration);
bool qa_remote_application_peer_world_current(qa_remote_application *app, const qa_remote_seat_peer *peer);
void qa_remote_application_bind_remote_seat(qa_remote_application *app, qa_remote_seat_peer *peer,
    qa_q3_client_connection *connection);
void qa_remote_application_poll_remote_seats(qa_remote_application *app, double now);
void qa_remote_application_retire_remote_seat(qa_remote_application *app, qa_remote_seat_peer *peer, bool remove_input);
void qa_remote_application_retire_remote_views(qa_remote_application *app);
void qa_remote_application_prepare_remote_seat_frames(qa_remote_application *app, double now, double wall_elapsed);
/* step / close */
void qa_remote_application_close_owned(qa_remote_application *app);

/* ---- C helpers shared by the remote_application_*.c files ------------- */
/* TS sourceHandlers.set(name, invocation => queueCommand(name, invocation.args, null[, invocation.source]))
 * and sourceDocumentation.set(name, documentation) (documentation NULL leaves it unset). */
void qa_remote_application_set_source_handler(qa_remote_application *app, const char *name, bool with_source,
    const qa_command_documentation *documentation);
/* The live transport as the datagram transport the client networks take (stored in the application). */
qa_datagram_transport *qa_remote_application_datagram(qa_remote_application *app, qa_app_application_transport *transport);
/* TS this.keys.authorization as Pick<Q3ClientAuthorization, "request"> (ctx is the application). */
qa_app_q3_authorization qa_remote_application_q3_authorization(qa_remote_application *app);
/* TS Q2RemotePresentation.mvdPresentation. */
qa_app_q2_mvd_presentation qa_remote_q2_mvd_presentation(qa_app_q2_remote *remote);
/* TS profile.seats.find(seat => seat.id.equals(id))?.mouse: NULL is undefined. */
qa_mouse_settings *qa_remote_profile_seat_mouse(qa_app_prepared_profile *profile, qa_seat_id id);
/* TS prepared.seats.find(seat => seat.id.equals(id)): NULL is undefined. */
const qa_app_prepared_seat *qa_remote_prepared_seat(const qa_app_prepared_startup *prepared, qa_seat_id id);
/* TS options.networkTransport ?? { kind: "udp" }. */
qa_app_network_transport_selection qa_remote_options_transport_selection(const qa_app_options *options);
/* TS join(a, b, c) (malloc'd). */
char *qa_remote_join3(const char *a, const char *b, const char *c);
/* TS Pick<ApplicationTransport, "connectSocks"> over an application transport. */
qa_socks_connector qa_remote_application_socks_connector(qa_app_application_transport *transport);
/* TS presentation.q3Client: NULL is TS null. */
qa_app_application_q3_client *qa_remote_presentation_q3_client(const qa_app_world_seat_presentation *presentation);
bool qa_remote_seat_id_equal(qa_seat_id a, qa_seat_id b);
/* TS registry.set(name, value, force) (the result snapshot dropped). */
void qa_remote_cvar_set(qa_cvar_registry *registry, const char *name, const char *value, bool force);
/* The members of the five presentation classes the application reads, dispatched on family. */
void qa_remote_presentation_register_resource(const qa_remote_presentation *remote, qa_content_id content, const char *path,
    qa_resolved_resource_reference resource);
/* TS remote.samplePresentation(now); scratch holds the unified replica's sampled frame. */
const qa_simulation_output *qa_remote_presentation_sample(const qa_remote_presentation *remote, double now,
    qa_simulation_output *scratch);
void qa_remote_presentation_player_command(const qa_remote_presentation *remote, qa_actor_id actor, const char *name,
    const char *const *args, size_t count);
qa_app_sim_player_ui qa_remote_presentation_player_ui(const qa_remote_presentation *remote, qa_actor_id actor);
/* TS remote.admittedPlayer (Q3) or remote.player: false is TS null. */
bool qa_remote_presentation_admitted_player(const qa_remote_presentation *remote, bool q3_connection, qa_actor_id *out);
/* config.c pieces shared with the profile members. */
void qa_remote_release_transfer(void *object);
void qa_remote_release_profile(void *object);
void qa_remote_release_configuration_record(void *object);
void qa_remote_host_print(void *host, const char *text);
void qa_remote_next_frame(void *ctx);
bool qa_remote_network_is(const qa_app_options *options, const char *kind);
qa_command_dialect qa_remote_dialect_for_clock(qa_clock_profile_kind kind);
qa_app_console_script_files *qa_remote_profile_scripts(const qa_app_options *options, qa_app_configuration_content *content,
    const qa_config_store *settings, qa_remote_arena *arena);
qa_config_store *qa_remote_product_settings(qa_remote_arena *arena, const qa_catalog_discovered_product *product,
    const char *user_content_root);
/* The peer configuration record's members (profile.c). */
void qa_remote_peer_configuration_validate(void *peer);
void qa_remote_peer_configuration_publish(void *peer);
qa_cvar_registry *qa_remote_peer_configuration_transport_cvars(void *peer);
/* malloc'd copies of a configuration's arrays (released with the record). */
qa_session_seat **qa_remote_copy_seats(qa_session_seat *const *seats, size_t count);
qa_remote_seat_source_entry *qa_remote_copy_seat_sources(const qa_remote_seat_source_entry *entries, size_t count);
qa_remote_seat_permission_entry *qa_remote_copy_permissions(const qa_remote_seat_permission_entry *entries, size_t count);
/* TS cheatsAllowed: () => false. */
int qa_remote_cheats_never(void *ctx);
/* TS releaseCommands of a profile's program. */
qa_command_append_target qa_remote_release_commands(qa_app_prepared_profile *profile);
/* TS ApplicationInputCommandOwner view of the client command owner. */
qa_app_input_command_owner qa_remote_input_owner(const qa_remote_command_owner *owner);
/* TS MountedApplicationContent and ApplicationConfigurationContent over a loaded unified content's
 * retained main mounts (adoptUnifiedProfile; one object serves as both): close releases the lease once.
 * The application owns it (dropped at destroy without releasing). */
qa_app_configuration_content *qa_remote_unified_configuration_content(qa_remote_application *app,
    qa_app_loaded_content *content, qa_app_main_mount_lease lease);
/* TS this.renderer.execute({ owner, sequence: this.frames, commands }). */
void qa_remote_renderer_execute(qa_remote_application *app, const qa_render_command *commands, size_t count);
/* TS startupReader(scripts). */
void qa_remote_application_startup_reader(qa_remote_application *app, qa_app_console_script_files *scripts,
    qa_app_prepared_scoped_read_fn *out_read, void **out_ctx);
/* cinematic.c: TS systemCinematicHost(owner) and componentCinematics(audio). */
typedef struct qa_remote_cinematic_owner qa_remote_cinematic_owner;
qa_remote_cinematic_owner *qa_remote_cinematic_owner_create(qa_remote_application *app, qa_unified_audio *audio,
    qa_content_mounted *mounts, qa_seat_id seat, qa_cvar_registry *cvars, void *ctx, bool (*current)(void *ctx),
    void (*complete)(void *ctx), void (*publish)(void *ctx, qa_remote_cinematic *movie),
    void (*unpublish)(void *ctx, qa_remote_cinematic *movie));
const qa_app_system_cinematic_host *qa_remote_application_system_cinematic_host(qa_remote_application *app,
    const qa_remote_cinematic_owner *owner);
struct qa_active_mod_presentation;
struct qa_app_viewing_seat;
struct qa_app_component_cinematic_scope;
typedef struct qa_remote_component_cinematics {
  void *ctx;
  const qa_app_system_cinematic_host *(*host)(void *ctx, const struct qa_active_mod_presentation *source,
      const struct qa_app_viewing_seat *presentation, const struct qa_app_component_cinematic_scope *scope);
} qa_remote_component_cinematics;
qa_remote_component_cinematics qa_remote_application_component_cinematics(qa_remote_application *app,
    qa_app_application_audio *audio);
/* input_platform.c: ApplicationAudio.bindHaptics(input) (declared in application_internal.h too). */
qa_app_audio_haptics qa_app_application_input_audio_haptics(qa_app_application_input *input);
/* content.c: loadQ3Character over a loaded content's appearance content (declared in application_internal.h too). */
const qa_q3_character_assets *qa_app_load_q3_character_for_content(qa_app_loaded_content *content, qa_content_id appearance,
    const char *model);
/* renderer.c: the QVM display the component presentations draw through (declared in application_internal.h too). */
qa_app_qvm_display_renderer qa_app_native_renderer_qvm_display(qa_app_native_renderer *renderer);
/* seat.c: TS `new RemoteComponentView(remote, { assets, audio, input, queries: remote.scene, renderer,
 * systemCinematics: this.componentCinematics(audio), print, nextFrame, clock })` for the unified replica; the
 * view is owned by the application (closed when replaced, freed at destroy). */
qa_app_remote_component_view *qa_remote_application_create_component_view(qa_remote_application *app,
    qa_app_unified_remote *remote, qa_app_application_assets *assets, qa_app_application_audio *audio,
    qa_app_application_input *input, double (*frame_number)(void *ctx), void *frame_ctx);
/* seat.c: the presentation-access views of the remote. */
const qa_app_sim_presentation_access *qa_remote_application_remote_access(qa_remote_application *app);
const qa_app_sim_presentation_access *qa_remote_application_source_access(qa_remote_application *app);
/* core.c: a deep copy of a command context (owned origin chain; qa_remote_command_free releases it). */
qa_command_context qa_remote_command_context_copy(const qa_command_context *source);
/* step.c */
void qa_remote_application_source_session_actions(qa_remote_application *app, const qa_app_audio_seat_events *batches,
    size_t batch_count);
uint8_t *qa_remote_application_read_pixels(qa_remote_application *app, size_t *out_length);
void qa_remote_application_capture_next_frame(qa_remote_application *app, qa_app_frame_capture_request request);
/* close.c: fills source_lifetime (TS `this` as ClientSourceLifetime). */
void qa_remote_application_init_lifetime(qa_remote_application *app);
/* commands.c: TS prepareRecording(context) as a ClientRecordingFeed; returns error text or NULL. */
const char *qa_remote_application_prepare_recording_feed(qa_remote_application *app, qa_command_context source,
    qa_app_client_recording_feed *out);
/* Frees a constructed application's storage (TS GC). */
void qa_remote_application_free_storage(qa_remote_application *app);
/* TS RecordedRemoteSource's per-family inputs (recorded-source.ts constructor). */
typedef struct qa_remote_recorded_inputs {
  void *reader;                          /* the family demo reader */
  bool q3_reader;                        /* reader is a qa_q3_demo_reader */
  qa_app_netquake_demo_input *q1;
  qa_app_quakeworld_demo_input *qw;
  qa_app_q2_demo_playback *q2;
  qa_app_q3_demo_playback *q3;
  qa_app_remote_seat_family family;
  const qa_remote_presentation *remote;
} qa_remote_recorded_inputs;
qa_app_recorded_playback qa_remote_application_recorded_playback(qa_remote_application *app,
    const qa_app_recorded_demo_resource *resource, qa_remote_recorded_inputs *inputs);
void qa_remote_recorded_inputs_release(void *inputs);
/* world: the download owners (TS this.qwDownloads?.close(); this.qwDownloads = null, and Q3's). */
void qa_remote_application_close_qw_downloads(qa_remote_application *app);
void qa_remote_application_close_q3_downloads(qa_remote_application *app);
/* unified.c: TS loadUnifiedContent over the mounted catalog. */
qa_app_loaded_content *qa_remote_load_unified_content(qa_remote_application *app, const qa_app_unified_control *offer);
/* TS saveAudioSettings(store, audio). */
void qa_remote_save_audio_settings(const qa_config_store *store, qa_app_application_audio *audio);
/* world: TS loadUnifiedContent({ ...this.options, mode: offer.mode }, offer.composition, this.mountedContent.catalog). */
qa_app_loaded_content *qa_remote_application_load_unified_content(qa_remote_application *app,
    const qa_app_unified_control *offer);
/* TS loadQ2ServerWorld(state). */
qa_app_loaded_content *qa_remote_application_load_q2_server_world(qa_remote_application *app, const qa_app_q2_game_state *state);
/* TS prepareVideoRestart(): false is TS null. */
bool qa_remote_application_prepare_video_restart(qa_remote_application *app, qa_app_prepared_video_presentation *out);
/* TS publishConnecting(client, signal?): aborted NULL is TS undefined. */

/* TS requireQwAddress: raises "QuakeWorld requires UDP" for IPX. */
qa_net_address qa_remote_require_qw_address(qa_net_address address);
/* TS `{ ...options }`: a deep copy (*out owns its strings). */
void qa_remote_options_copy(const qa_app_options *options, qa_app_options *out);
#endif
