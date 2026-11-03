/* Port of src/app/bootstrap/application.ts: the Application struct (every TS
 * field, TS order), the types application.ts declares, and every member's
 * declaration. Status per member: docs/application_layout.md. A member
 * declared here and marked "not done" there has no definition yet.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_APPLICATION_INTERNAL_H
#define QA_APP_BOOTSTRAP_APPLICATION_INTERNAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>
#include "qa/app/bootstrap/application_class.h"
#include "qa/app/bootstrap/application_tools.h"
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/base_arena_catalog.h"
#include "qa/app/bootstrap/base_arena_postgame.h"
#include "qa/app/bootstrap/base_arena_progression.h"
#include "qa/app/bootstrap/base_arena_menu.h"
#include "qa/app/bootstrap/cvar_archives.h"
#include "qa/app/bootstrap/team_arena_results.h"
#include "qa/app/bootstrap/campaign_cinematic.h"
#include "qa/app/bootstrap/campaign_unit.h"
#include "qa/app/bootstrap/capture.h"
#include "qa/app/bootstrap/precache.h"
#include "qa/app/bootstrap/client_bootstrap.h"
#include "qa/app/bootstrap/component_drawings.h"
#include "qa/app/bootstrap/config_scripts.h"
#include "qa/app/bootstrap/configuration.h"
#include "qa/app/bootstrap/demo_commands.h"
#include "qa/app/bootstrap/demo_local_recording.h"
#include "qa/app/bootstrap/demo_recording_commands.h"
#include "qa/app/bootstrap/effects.h"
#include "qa/app/bootstrap/image_settings.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/keys.h"
#include "qa/app/bootstrap/mod_presentations.h"
#include "qa/app/bootstrap/native_q2_client.h"
#include "qa/app/bootstrap/network/q1.h"
#include "qa/app/bootstrap/network/q2.h"
#include "qa/app/bootstrap/network/q3.h"
#include "qa/app/bootstrap/network/qw_server.h"
#include "qa/app/bootstrap/network/unified_server.h"
#include "qa/app/bootstrap/player_progress.h"
#include "qa/app/bootstrap/prepared_startup.h"
#include "qa/app/bootstrap/presentation.h"
#include "qa/app/bootstrap/q2_console.h"
#include "qa/app/bootstrap/q2_travel.h"
#include "qa/app/bootstrap/q2_match_reports.h"
#include "qa/app/bootstrap/q3_client.h"
#include "qa/app/bootstrap/q3_client/client_state.h"
#include "qa/app/bootstrap/q3_map_command.h"
#include "qa/app/bootstrap/q3_rankings.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/app/bootstrap/rerelease_presentation.h"
#include "qa/app/bootstrap/save_requests.h"
#include "qa/app/bootstrap/selected_q3_presentation.h"
#include "qa/app/bootstrap/server_administration.h"
#include "qa/app/bootstrap/server_browser.h"
#include "qa/app/bootstrap/simulation/bots.h"
#include "qa/app/bootstrap/simulation/bot_assets.h"
#include "qa/app/bootstrap/simulation/prediction/presentation.h"
#include "qa/app/bootstrap/simulation/q3/guest_runtime.h"
#include "qa/app/bootstrap/simulation/network_q3.h"
#include "qa/app/bootstrap/team_arena_skirmish.h"
#include "qa/app/bootstrap/ui.h"
#include "qa/ui/saves/menu.h"
#include "qa/app/bootstrap/startup_saves.h"
#include "qa/app/bootstrap/team_arena_demo.h"
#include "qa/app/bootstrap/team_arena_overrides.h"
#include "qa/app/bootstrap/team_arena_scores.h"
#include "qa/app/bootstrap/video_restart.h"
#include "qa/app/bootstrap/view_settings.h"
#include "qa/audio/music.h"
#include "qa/console/dedicated.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
#include "qa/foundation/event_ring.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/browser_view.h"
#include "qa/platform/files/writable.h"
#include "qa/persistence/save_policy.h"
#include "qa/render/debug_graph.h"
#include "qa/text/draw2d.h"
#include "qa/settings/config.h"
#include "qa/ui/common/assets.h"
#include "qa/ui/hud/q2_native.h"
#include "qa/world/gameplay/transitions.h"
#include "qa/world/session/session.h"
#include "simulation/shared_simulation_internal.h"
#include "qa/app/bootstrap/original_save.h"
#include "qa/app/bootstrap/q3_client/cinematics.h"
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/text/media_captions.h"
#include "qa/app/bootstrap/audio_settings.h"
#include "qa/app/bootstrap/component_media.h"

/* TS DEDICATED_FAULT_LIMIT, DEDICATED_FAULT_WINDOW_MILLISECONDS. */
#define QA_APPLICATION_DEDICATED_FAULT_LIMIT 3
#define QA_APPLICATION_DEDICATED_FAULT_WINDOW_MILLISECONDS 60000.0
/* TS EventRing capacities of requestedCommands / pendingCommands and roundPresentationEvents. */
#define QA_APPLICATION_REQUEST_CAPACITY 16384
#define QA_APPLICATION_ROUND_EVENT_CAPACITY 32768

/* TS () => void closures the class stores. fn NULL is the TS no-op. */
typedef struct qa_application_release { void (*fn)(void *ctx); void *ctx; } qa_application_release;

/* TS NativeServerHost (kind + host). */
typedef enum qa_application_network_kind {
  QA_APPLICATION_NETWORK_Q1, QA_APPLICATION_NETWORK_QW, QA_APPLICATION_NETWORK_Q2,
  QA_APPLICATION_NETWORK_Q3, QA_APPLICATION_NETWORK_UNIFIED
} qa_application_network_kind;
typedef struct qa_application_native_server_host {
  qa_application_network_kind kind; /* never UNIFIED */
  qa_app_q1_server_host q1;
  qa_app_qw_server_host qw;
  qa_app_q2_server_host q2;
  qa_app_sim_q3_server_authority q3;
} qa_application_native_server_host;
/* TS NativeServer ({address} & kind + server). */
typedef struct qa_application_native_server {
  qa_net_address address;
  qa_application_network_kind kind;
  qa_app_q1_server_network *q1;
  qa_app_qw_server_network *qw;
  qa_app_q2_server_network *q2;
  qa_app_q3_server_network *q3;
  qa_app_unified_server_network *unified;
} qa_application_native_server;

/* TS SavedApplicationClients. resolve_bot_client returns NULL for TS null. */
typedef struct qa_application_saved_clients {
  const qa_client_id *clients; size_t client_count;
  qa_session_client *const *added; size_t added_count;
  qa_session_client *const *removed; size_t removed_count;
  qa_session_client *(*resolve_bot_client)(void *ctx, qa_client_id saved);
  void *ctx;
} qa_application_saved_clients;

/* TS PreparedSourceCommands. activate returns the release closure. */
typedef struct qa_application_prepared_source_commands {
  qa_command_buffer *commands;                 /* NULL = TS null */
  qa_prepared_program *program;                /* NULL = TS null */
  qa_app_q2_console *q2_console;               /* NULL = TS null */
  const qa_command_buffer_options *options;    /* NULL = TS null */
  qa_application_release (*activate)(void *ctx, qa_command_buffer *commands);
  void (*destroy)(void *ctx); /* C ownership: frees the preparation, its buffer or program and its console */
  void *ctx;
} qa_application_prepared_source_commands;

/* TS ApplicationCommandRequest. Every string is owned by the record
 * (qa_application_command_request_free). component is set for the
 * ComponentClientCommandRequest arm (target "component-client"). */
typedef enum qa_application_request_target {
  QA_APPLICATION_TARGET_APPLICATION, QA_APPLICATION_TARGET_CLIENT, QA_APPLICATION_TARGET_SOURCE,
  QA_APPLICATION_TARGET_COMPONENT_CLIENT
} qa_application_request_target;
typedef struct qa_application_command_request {
  qa_application_request_target target;
  char *name;
  char **arguments; size_t argument_count;
  bool has_seat; qa_seat_id seat;
  bool has_source; qa_command_context source;
  const qa_app_component_client_command_target *consumer; /* component-client */
  const char *mode;                                       /* component-client: static "console" | "reliable" */
} qa_application_command_request;
void qa_application_command_request_free(qa_application_command_request *request);

/* TS Q3SeatClient. */
typedef struct qa_application_q3_seat_client {
  bool qvm; /* kind "qvm" (state) or "native" (prediction) */
  qa_app_application_q3_client *client;
  qa_actor_id actor;                              /* client.options.local.player.actor */
  qa_app_simulation_prediction_host *prediction; /* native */
  qa_app_local_q3_client_state *state;           /* qvm */
} qa_application_q3_seat_client;

/* TS NativeQ2SeatClient. descriptor.frame reads client's HUD frame. */
typedef struct qa_application_native_q2_seat_client {
  char *userinfo;
  double source_slot;                             /* client.sourceSlot */
  qa_cvar_registry *cvars;
  qa_app_native_q2_client *client;
  qa_app_application_effects *effects;
  /* descriptor: { frame: () => NativeQ2HudFrame, ownsEffects: true } (filled by prepareNativeQ2Seat). */
  qa_native_q2_hud_frame (*descriptor_frame)(void *ctx); void *descriptor_ctx;
} qa_application_native_q2_seat_client;

/* TS WorldSeatPresentation as GraphicalApplication.presentations holds it: the
 * presentation and the public members the application reads (local, ui,
 * q3Client; presentation.c does not expose them). q3_client NULL is TS null. */
typedef struct qa_application_seat_presentation {
  qa_app_world_seat_presentation *presentation;
  qa_app_local_input *local;
  qa_app_seat_ui *ui;
  qa_application_q3_seat_client *q3_client;
} qa_application_seat_presentation;

/* TS GraphicalApplication. q3 / native_q2 are Map<SeatId, ...> in insertion order. */
typedef struct qa_application_graphical {
  qa_app_application_mod_presentations *mod_presentations;
  qa_app_selected_q3_presentations *selected_q3_presentations;
  qa_app_native_renderer *renderer;
  qa_app_application_assets *assets;
  qa_app_application_input *input;
  qa_app_application_audio *audio;
  qa_app_application_effects *effects;
  qa_native_ui_art *art;
  qa_application_seat_presentation *presentations; size_t presentation_count;
  qa_seat_id *q3_seats; qa_application_q3_seat_client *q3; size_t q3_count;
  qa_seat_id *native_q2_seats; qa_application_native_q2_seat_client *native_q2; size_t native_q2_count;
  qa_app_rerelease_presentation *rerelease;
} qa_application_graphical;

/* TS LocalQ3GuestSeat, LocalQ3GuestBrowser, LocalQ3GuestWorld. */
typedef struct qa_application_local_q3_guest_seat {
  qa_app_local_q3_client_state *state;
  qa_cvar_registry *cvars;
  char *userinfo;
  qa_app_application_q3_client *client; /* NULL = TS null */
} qa_application_local_q3_guest_seat;
typedef struct qa_application_local_q3_guest_browser {
  qa_app_startup_server_browser *host;
  qa_q3_browser_view *view;
  qa_app_startup_q3_browser_host *adapter; /* C-only: the Q3BrowserHost view of `host` that `view` reads */
} qa_application_local_q3_guest_browser;
typedef struct qa_application_local_q3_guest_world {
  qa_actor_id world_sound;
  qa_app_sim_q3_server_authority authority;
  qa_seat_id *seat_ids; qa_application_local_q3_guest_seat *seats; size_t seat_count, seat_capacity;
  qa_event_ring pending_commands; /* qa_application_command_request records */
} qa_application_local_q3_guest_world;

/* TS rankings state. task is the TS pending Promise (true while a frame runs). */
typedef struct qa_application_rankings {
  qa_app_shared_simulation *simulation;
  qa_app_q3_rankings *owner;
  bool enabled, pending, task;
  int64_t *client_slots; qa_client_id *clients; size_t client_count, client_capacity; /* Map<number, ClientId> */
  void *host_ctx; /* C-only: the Q3RankingHost record, freed with the state */
} qa_application_rankings;

/* TS campaign movie record (Map<SharedSimulation, movie> value). request is
 * the TS identity token (compared by value). */
typedef struct qa_application_movie {
  qa_app_shared_simulation *simulation;
  qa_app_campaign_cinematic *playback;
  uint64_t request;
  bool component_owned;
  bool (*current)(void *ctx);
  void (*complete)(void *ctx);
  void *ctx;
} qa_application_movie;

/* TS baseArenaProgress WeakMap value. */
typedef struct qa_application_base_arena_progress {
  qa_app_shared_simulation *simulation;
  qa_app_base_arena_catalog *catalog;
  qa_app_base_arena_progression *progression;
  bool has_result; qa_app_arena_postgame_presentation result;
  qa_app_arena_postgame result_game; /* C-only: owns the podium result borrows; result.result.awards owned */
  double result_at;
  bool announced, movie_played;
} qa_application_base_arena_progress;

/* TS saveOperation. run performs the queued save/load; settle receives NULL on
 * success or the caught error (TS resolve / reject). */
typedef struct qa_application_save_operation {
  void (*run)(qa_application *app, void *ctx);
  void (*settle)(void *ctx, const qa_caught_error *error);
  void *ctx;
} qa_application_save_operation;

/* TS gameLog. */
typedef struct qa_application_game_log {
  char *name;
  bool sync;
  qa_writable_binary_file *file;
} qa_application_game_log;

/* TS operatorOutput box: write NULL is TS null. */
typedef struct qa_application_operator_output {
  void (*write)(void *ctx, const char *text);
  void *ctx;
} qa_application_operator_output;

/* TS ownership: borrowed_client NULL is { kind: "owned" }. */
typedef struct qa_application_ownership {
  qa_app_client_bootstrap *borrowed_client;
} qa_application_ownership;

/* TS networkPlayerIdentities value. */
typedef struct qa_application_player_identity { double seat; char *social_id; } qa_application_player_identity;

/* TS world operation kind. */
typedef enum qa_application_world_operation {
  QA_APPLICATION_IDLE, QA_APPLICATION_SAVING, QA_APPLICATION_LOADING, QA_APPLICATION_TRAVEL
} qa_application_world_operation;

/* TS Simulation view of the SharedSimulation the session attaches. The last
 * checkpoint is owned here until the next checkpoint or close. */
typedef struct qa_application_simulation_view {
  qa_app_shared_simulation *simulation;
  bool has_save; qa_app_shared_sim_save save;
} qa_application_simulation_view;

/* openDedicatedConsole's queueCommand handler context (one per command name). */
typedef struct qa_application_dedicated_queue { qa_application *app; const char *name; } qa_application_dedicated_queue;

struct qa_application {
  qa_application_release release_view_cvars;          /* releaseViewCvars (fn NULL = TS null) */
  qa_app_view_settings *view_settings;
  qa_application_graphical *graphical;                /* NULL = TS null */
  qa_app_capture *capture;
  bool q3_menu_pause;
  qa_app_shared_simulation *lobby_completion;
  qa_application_rankings *rankings;
  qa_seat_id *ranking_menu_requests; size_t ranking_menu_request_count, ranking_menu_request_capacity; /* Set<SeatId> */
  int64_t *ranking_status_slots; qa_ranking_player_state *ranking_statuses; size_t ranking_status_count, ranking_status_capacity;
  qa_app_application_tools *tools;
  qa_source_debug_graph *debug_graph;                 /* owned */
  qa_app_video_restart *owned_video_restart;
  qa_app_frontend_preference_overrides frontend_overrides;
  qa_app_image_settings *image_settings;
  bool has_frontend_baseline; qa_app_frontend_preference_values frontend_baseline;
  qa_app_sim_bot_service *bots;                       /* NULL = TS null */
  qa_seat_id *client_cvar_seats; qa_cvar_registry **client_cvars; size_t client_cvar_count, client_cvar_capacity;
  qa_cvar_registry **published_userinfo_cvars; char **published_userinfo; size_t published_userinfo_count, published_userinfo_capacity;
  qa_app_team_arena_overrides *team_arena_overrides;
  bool archive_persistence;
  qa_application_game_log *game_log;
  qa_dedicated_console *dedicated_console;
  qa_command_buffer *dedicated_commands;
  qa_event_ring requested_commands;                   /* qa_application_command_request records */
  qa_event_cursor requested_cursor;
  qa_app_prepared_startup *prepared_startup;
  qa_app_keys *keys;
  qa_app_shared_simulation **key_profile_simulations; qa_app_key_profile **key_profiles; size_t key_profile_count, key_profile_capacity;
  qa_seat_input_event *client_inputs; size_t client_input_count, client_input_capacity;
  qa_seat_input_event *deferred_input; size_t deferred_input_count, deferred_input_capacity;
  bool stopping;
  bool closed;
  bool stepping;
  qa_application_world_operation world_operation;
  double world_operations;
  bool world_operation_completion;                    /* TS Promise | null: a world operation is running */
  bool step_completion;                               /* TS Promise | null: a step is running */
  bool closing;                                       /* TS closing !== null */
  bool has_closing_error; qa_caught_error closing_error;
  qa_app_client_demo_recording *owned_recording;
  qa_app_client_demo_commands *owned_demos_service; qa_application_release owned_demos_release; bool has_owned_demos;
  double elapsed;
  double frames;
  double presentation_frames;
  bool has_presentation_milliseconds; double presentation_milliseconds;
  qa_app_sim_presentation_event *source_events; size_t source_event_count; /* owned copies */
  qa_app_unhandled_effect *unhandled_effects; size_t unhandled_effect_count;
  qa_config_store *server_profile_store_value;
  qa_config_store input_config;
  char **reported_effect_gaps; size_t reported_effect_gap_count, reported_effect_gap_capacity;
  qa_application_native_server *network;
  qa_application_native_server_host *recording_host;
  qa_app_local_demo_recording *local_recording;
  bool local_recording_active;
  bool has_recording_seat; qa_seat_id recording_seat;
  qa_shared_transition_coordinator *transitions;
  bool has_pending_transition; qa_transition_decision pending_transition; int64_t pending_transition_id;
  qa_app_shared_simulation **movie_request_simulations; uint64_t *movie_requests; size_t movie_request_count, movie_request_capacity;
  qa_application_movie *movies; size_t movie_count, movie_capacity;
  uint64_t next_movie_request;                        /* TS `{}` identity tokens */
  char *pending_map;
  bool has_pending_q3_map; qa_app_q3_map_launch pending_q3_map;
  bool has_pending_native_travel; qa_app_q2_travel_target pending_native_travel;
  qa_application_base_arena_progress *base_arena_progress; size_t base_arena_progress_count, base_arena_progress_capacity;
  qa_app_player_progress_store *player_progress;
  qa_app_shared_simulation **team_arena_demo_simulations; qa_app_team_arena_demo **team_arena_demos; size_t team_arena_demo_count, team_arena_demo_capacity;
  const char *pending_team_arena;                     /* "next" | "retry" | NULL */
  bool has_pending_team_arena_postgame; qa_app_team_arena_postgame pending_team_arena_postgame;
  qa_command_buffer *source_commands;
  qa_app_server_operator_state *operator_state;
  qa_application_operator_output *operator_output;    /* shared with the host print wrapper */
  bool (*operator_send)(void *ctx, const qa_net_address *to, const uint8_t *bytes, size_t count); void *operator_send_ctx;
  bool has_source_command_binding; const qa_command_buffer_options *source_command_binding;
  qa_application_release release_source_commands;
  qa_application_release release_mod_input_commands;
  qa_command_buffer *mod_input_commands;
  qa_app_q2_console *q2_console;                      /* borrowed from published_source_commands */
  bool has_pending_restart; double pending_restart;
  double last_restart_frame;
  int local_snapshot_server_bit;                      /* 0 | 4 */
  bool fatal_world_failure;
  double dedicated_frame_faults[QA_APPLICATION_DEDICATED_FAULT_LIMIT + 1];
  double dedicated_frame_fault_count;
  qa_event_ring round_presentation_events;            /* qa_app_sim_presentation_event records */
  qa_event_cursor round_cursor;
  bool has_pending_save; char *pending_save_path; char *pending_save_source_product;
  qa_app_startup_saves *saved_games;
  const void *saved_games_catalog;                    /* identity of the catalog the list belongs to */
  qa_application_local_q3_guest_world *local_guest;
  qa_application_local_q3_guest_browser *guest_browser;
  bool has_save_operation; qa_application_save_operation save_operation;
  bool has_last_output; qa_app_shared_simulation *last_output_simulation; qa_simulation_output last_output;
  qa_app_campaign_unit *campaign_unit;
  double native_world_count;
  qa_app_console_script_files *configuration_scripts;
  qa_app_prepared_profile *profile_configuration;
  bool source_published;
  bool source_seats_published;
  qa_session_client **source_connection_clients; qa_session_connection **source_connections; size_t source_connection_count, source_connection_capacity;
  qa_session_client **source_clients_added; size_t source_clients_added_count, source_clients_added_capacity;
  qa_session_client **source_clients_removed; size_t source_clients_removed_count, source_clients_removed_capacity;
  qa_session_seat **source_seats_added; size_t source_seats_added_count, source_seats_added_capacity;
  qa_session_seat **source_seats_removed; size_t source_seats_removed_count, source_seats_removed_capacity;
  qa_music_controls *music_controls; bool owns_music_controls;
  /* constructor parameter properties */
  qa_app_options launch_options;
  qa_app_loaded_content *loaded_content;
  qa_engine_session *session;
  qa_app_shared_simulation *world_simulation;
  qa_application_host host;                           /* print wrapped by operatorHost */
  qa_application_host original_host;                  /* the caller's host (operatorHost's originalPrint) */
  qa_identity_owner *identity;
  qa_client_id *local_seat_clients; qa_session_seat **local_seats; size_t local_seat_count, local_seat_capacity; /* Map<ClientId, SessionSeat> */
  qa_client_id *network_player_clients; qa_application_player_identity *network_player_identities;
  size_t network_player_identity_count, network_player_identity_capacity;
  qa_application_ownership ownership;
  /* C-only storage */
  qa_application_simulation_view simulation_view;    /* the qa_simulation the session holds */
  bool owns_identity;
  char *save_directory_text;                          /* saveDirectory getter result */
  char **external_save_roots; size_t external_save_root_count; /* externalSaveDirectories getter result */
  qa_application_prepared_source_commands *published_source_commands; /* owns sourceCommandBinding.options and q2Console */
  void **ranking_account_records; size_t ranking_account_record_count, ranking_account_record_capacity; /* rankingAccountActions results */
  void **guest_output_records; size_t guest_output_record_count, guest_output_record_capacity;       /* guestOutput closures */
  qa_application_dedicated_queue dedicated_queues[7];                                               /* openDedicatedConsole handlers */
  void *service_records;                              /* application_services.c storage (qa_application_services_free) */
  void **bot_records; size_t bot_record_count, bot_record_capacity; /* application_bots.c: one per createBots result */
  void **guest_bot_records; size_t guest_bot_record_count, guest_bot_record_capacity; /* application_bots.c: one per prepareGuestBots */
  struct { void *record; void (*release)(void *record); } *seat_records; size_t seat_record_count, seat_record_capacity; /* application_seats.c service records */
  struct qa_application_open_record *open_record;    /* application_open.c: what openSource created (qa_application_open_free) */
  void *open_state;                                   /* application_open.c: openSource's closure state */
  /* C-only: the application as a ClientSourceLifetime (client.source.current = this; application_open_graphical.c).
   * ctx is NULL until publishBorrowed publishes it; the members return borrowed error text (source_lifetime_error). */
  qa_app_client_source_lifetime source_lifetime;
  char source_lifetime_error[sizeof ((qa_caught_error *)0)->message];
};

/* ---- constructor and storage (application_core.c) ---- */
qa_application *qa_application_construct(qa_app_options launch_options, qa_app_loaded_content *loaded_content,
    qa_engine_session *session, qa_app_shared_simulation *world_simulation, qa_application_host host,
    qa_identity_owner *identity, const qa_client_id *local_seat_clients, qa_session_seat *const *local_seats,
    size_t local_seat_count, qa_config_store input_config, qa_application_ownership ownership);
qa_simulation qa_application_simulation_contract(qa_application_simulation_view *view);
double qa_application_performance_now(void);
void qa_application_cvar_set(qa_cvar_registry *cvars, const char *name, const char *value, bool force);
char *qa_application_cvar_string(qa_cvar_registry *cvars, const char *name);
/* node:path join/normalize of already joined text; malloc'd. */
char *qa_application_path_normalize(const char *path);
void qa_application_print(qa_application *app, const char *text);
void qa_application_printf(qa_application *app, const char *format, ...);
qa_cvar_registry *qa_application_client_cvars_get(qa_application *app, qa_seat_id seat);
qa_session_seat *qa_application_local_seat(qa_application *app, qa_client_id client);
/* TS localPlayers getter: rows of graphical.input.locals (count 0 without graphics). */
size_t qa_application_local_player_count(qa_application *app);
qa_app_local_player qa_application_local_player(qa_application *app, size_t index);

/* ---- application_core.c: 408-886 ---- */
const qa_ui_ranking_account_actions *qa_application_ranking_account_actions(qa_application *app, qa_seat_id seat);
void qa_application_publish_ranking_state(qa_application *app, qa_app_shared_simulation *simulation);
void qa_application_disconnect_rankings(qa_application *app, qa_app_shared_simulation *simulation, qa_client_id client);
void qa_application_close_rankings(qa_application *app, qa_app_shared_simulation *simulation);
void qa_application_queue_ranking_frame(qa_application *app);
void qa_application_ranking_frame(qa_application *app);
size_t qa_application_override_seats(qa_application *app, const qa_seat_id *seats, qa_cvar_registry *const *cvars,
    size_t count, qa_app_override_registry *out, size_t capacity);
qa_config_store *qa_application_server_profile_store(qa_application *app);
qa_application_movie *qa_application_campaign_movie(qa_application *app);
void qa_application_set_campaign_movie(qa_application *app, const qa_application_movie *movie);
const char *qa_application_save_directory(qa_application *app);
const char *const *qa_application_external_save_directories(qa_application *app, size_t *out_count);
const char *qa_application_save_unavailable(qa_application *app, qa_save_purpose purpose);
bool qa_application_level_recovery_available(qa_application *app, qa_app_shared_simulation *simulation, const qa_app_options *options);
void qa_application_autosave_level(qa_application *app);
qa_app_application_effects *qa_application_create_world_effects(qa_application *app, qa_app_application_assets *assets,
    qa_app_shared_simulation *simulation, double seed);
void qa_application_preload_world_effects(qa_application *app, qa_app_application_effects *effects);
/* TS createSeatUi(local, art, input, windowMutator(renderer), simulation, font, audio, typography, rerelease, content,
 * options, qvm, clientCvars?): the window mutator is the shared renderer's; client_cvar_seats NULL is TS undefined. */
qa_app_seat_ui *qa_application_create_seat_ui(qa_application *app, qa_app_local_input *local, qa_native_ui_art *art,
    qa_app_application_input *input, qa_app_native_renderer *renderer, qa_app_shared_simulation *simulation, qa_text_font_selection font,
    qa_app_application_audio *audio, const qa_app_menu_typography *typography, qa_app_rerelease_presentation *rerelease,
    qa_app_loaded_content *content, const qa_app_options *options, bool qvm, const qa_seat_id *client_cvar_seats,
    qa_cvar_registry *const *client_cvars, size_t client_cvar_count);
/* TS createWorldSeatPresentation(local, assets, renderer, simulation, seats, font, characters, ui, effects, sourceClient,
 * rerelease, nativeSeat, content = this.content): NULL pointers are TS null; content NULL is the default. */
qa_app_world_seat_presentation *qa_application_create_world_seat_presentation(qa_application *app, qa_app_local_input *local,
    qa_app_application_assets *assets, qa_app_native_renderer *renderer, qa_app_shared_simulation *simulation, double seats,
    qa_text_font_selection font, const qa_q3_character_assets *characters, qa_app_seat_ui *ui, qa_app_application_effects *effects,
    qa_application_q3_seat_client *source_client, qa_app_rerelease_presentation *rerelease,
    qa_application_native_q2_seat_client *native_seat, qa_app_loaded_content *content);
qa_ui_saved_game_menu_service qa_application_save_menu(qa_application *app, qa_app_shared_simulation *simulation, const qa_app_options *options);
/* TS windowMutator(renderer) (module-level): operation => renderer.mutateWindow(operation); ctx is the
 * qa_app_native_renderer (the ApplicationSeatUi mutate_window shape). */
void qa_application_window_mutator(void *renderer, const qa_ui_window_mutation *mutation);
/* TS uiSourceOptions(simulation, rerelease, seat, clients = this.clientCvars): fills lobby, rankings, gameplay, baseArena,
 * nativeHudLocalizer and localize of *out; client_seats NULL is the default map. Storage lives until destroy. */
void qa_application_ui_source_options(qa_application *app, qa_app_shared_simulation *simulation, qa_app_rerelease_presentation *rerelease,
    qa_seat_id seat, const qa_seat_id *client_seats, qa_cvar_registry *const *client_cvars, size_t client_count,
    qa_app_seat_ui_options *out);
qa_config_store qa_application_source_config(const qa_app_options *options, qa_app_loaded_content *content);
qa_config_store qa_application_client_config(const qa_app_options *options, qa_app_loaded_content *content);
void qa_application_enable_startup_persistence(qa_application *app);
void qa_application_save_source_archive(qa_application *app, const qa_app_options *options, qa_app_loaded_content *content,
    qa_app_shared_simulation *simulation, qa_app_team_arena_overrides *overrides);
void qa_application_save_client_archives(qa_application *app, const qa_app_options *options, qa_app_loaded_content *content,
    const qa_seat_id *seats, qa_cvar_registry *const *cvars, size_t count, qa_app_team_arena_overrides *overrides);
qa_cvar_registry *qa_application_source_cvars(qa_application *app, qa_app_shared_simulation *simulation);
qa_command_context qa_application_native_command_context(qa_app_loaded_content *content, qa_session_id session);
/* TS guestOutput(localGuest = this.localGuest): default_local_guest true is TS undefined. */
qa_app_q3_guest_output qa_application_guest_output(qa_application *app, qa_application_local_q3_guest_world *local_guest,
    bool default_local_guest);
/* TS (text: string) => void closures the statics return; record is the heap
 * storage the caller frees when the closure is dropped. */
typedef struct qa_application_printer { void (*print)(void *ctx, const char *text); void *ctx; void *record; } qa_application_printer;
/* TS { readonly application: Application | null } owner box. */
typedef struct qa_application_owner_ref { qa_application *application; } qa_application_owner_ref;
/* TS static operatorHost(host, operatorOutput); *out_record is the wrapper storage. */
qa_application_host qa_application_operator_host_wrap(qa_application_host host, qa_application_operator_output *output,
    void **out_record);
qa_application_printer qa_application_host_print(const qa_application_host *host);
qa_application_printer qa_application_frontend_print(const qa_application_host *host, qa_application_owner_ref *owner);
qa_app_client_demo_commands *qa_application_startup_demo_service(bool dedicated, qa_app_prepared_startup *prepared,
    const qa_application_host *host, qa_application_owner_ref *owner, void **out_record);
qa_application *qa_application_open_borrowed(qa_app_client_bootstrap *client, qa_app_options options, qa_application_host host,
    const qa_executable_recipe *recipe, const qa_app_frontend_preference_overrides *preferences, const qa_save_image *initial_save);
void qa_application_forward_startup_commands(qa_app_prepared_startup *prepared, qa_application *app);

/* ---- application_open.c: 888-1233 ---- */
/* C-only: frees what openSource created and its closure state (destroy, after close). */
void qa_application_open_free(qa_application *app);
/* C-only: a deep copy of launch options (the content loader takes over the options it is given). */
qa_app_options qa_application_options_copy(const qa_app_options *options);
qa_application *qa_application_open_source(qa_application_ownership ownership, qa_app_options options, qa_application_host host,
    const qa_executable_recipe *recipe, const qa_app_frontend_preference_overrides *preferences, const qa_save_image *initial_save);
void qa_application_prepare_guest_bots(qa_application *app, qa_app_loaded_content *content, qa_app_shared_simulation *simulation);
/* TS createBots(content, simulation, setup = {}): setup NULL is TS {}.
 * commands: has_commands false is the TS default (this.sourceCommands). */
typedef struct qa_application_create_bots_setup {
  const qa_app_sim_bot_client *clients; size_t client_count;
  bool restart;
  const qa_app_sim_decoded_bots_checkpoint *restore;
  qa_session_client *(*resolve_client)(void *ctx, double slot, double generation); void *resolve_ctx;
  bool requested;
  bool has_commands; qa_command_buffer *commands;
} qa_application_create_bots_setup;
qa_app_sim_bot_service *qa_application_create_bots(qa_application *app, qa_app_loaded_content *content,
    qa_app_shared_simulation *simulation, const qa_application_create_bots_setup *setup);

/* ---- application_core.c: 1235-1300 accessors ---- */
qa_app_frontend_preference_overrides qa_application_frontend_settings(qa_application *app);
bool qa_application_frontend_values(qa_application *app, qa_app_frontend_preference_values *out);
size_t qa_application_bot_clients(qa_application *app, const qa_app_sim_bot_client **out);
bool qa_application_client_commands_blocked(qa_application *app);
qa_app_video_restart *qa_application_video_restart(qa_application *app);
void qa_application_pump_client_input(qa_application *app);
bool qa_application_advance_client_startup(qa_application *app);
void qa_application_flush_client_commands(qa_application *app);
const qa_app_sim_presentation_event *qa_application_presentation_events(const qa_application *app, size_t *out_count);
const qa_app_unhandled_effect *qa_application_unhandled_presentation_effects(const qa_application *app, size_t *out_count);
bool qa_application_network_address(const qa_application *app, qa_net_address *out);
void qa_application_return_to_lobby(qa_application *app);
/* TS queueCommand(name, args, seat, source?): seat / source NULL are TS null / undefined. */
void qa_application_queue_command_source(qa_application *app, const char *name, const char *const *arguments,
    size_t argument_count, const qa_seat_id *seat, const qa_command_context *source);
qa_sdl_window *qa_application_window(qa_application *app);
/* TS get networkClients: malloc'd rows (caller frees). */
qa_app_network_player *qa_application_network_clients(qa_application *app, size_t *out_count);
bool qa_application_input(qa_application *app, const qa_seat_input_event *event);
void qa_application_resume_input(qa_application *app);
void qa_application_open_dedicated_console(qa_application *app);

/* ---- application_commands.c: 1302-1750 ---- */
/* content NULL is TS this.content. */
qa_command_dialect qa_application_source_dialect(qa_application *app, qa_app_loaded_content *content);
qa_command_dialect qa_application_content_dialect(qa_app_loaded_content *content);
/* TS ApplicationCommandRequest consumers (action / candidateAction): the
 * callee takes over the request's storage. */
/* TS inputActions(simulation?, content?, q2Console?, localGuest?, clientCvars?, sharedCvars?, candidateAction?).
 * Zeroed options (or NULL) select every TS default. The returned commands
 * (actions->commands) and their ctx live until qa_application_input_actions_destroy. */
typedef struct qa_application_input_actions_options {
  qa_app_shared_simulation *simulation;             /* NULL = this.simulation */
  qa_app_loaded_content *content;                   /* NULL = this.content */
  bool has_q2_console; qa_app_q2_console *q2_console;
  bool has_local_guest; qa_application_local_q3_guest_world *local_guest;
  bool explicit_client_cvars;                       /* false = this.clientCvars */
  const qa_seat_id *client_cvar_seats; qa_cvar_registry *const *client_cvars; size_t client_cvar_count;
  bool has_shared_cvars; qa_cvar_registry *shared_cvars; /* false = this.imageSettings?.cvars */
  void (*candidate_action)(void *ctx, const qa_application_command_request *request); /* NULL = undefined */
  void *candidate_ctx;
} qa_application_input_actions_options;
typedef struct qa_application_input_actions {
  qa_app_input_commands commands;
  qa_app_input_console_routes console;
  qa_application *app;
  qa_app_shared_simulation *simulation;
  qa_app_loaded_content *content;
  qa_app_q2_console *q2_console;
  qa_application_local_q3_guest_world *local_guest;
  bool explicit_client_cvars;
  const qa_seat_id *client_cvar_seats; qa_cvar_registry *const *client_cvars; size_t client_cvar_count;
  void (*candidate_action)(void *ctx, const qa_application_command_request *request);
  void *candidate_ctx;
  /* C-only: the configuration openGraphical's startupReader closure captured (() => configuration.read). */
  qa_app_prepared_profile *startup_configuration;
  /* C-only scratch for the borrowed callback results. */
  qa_weapon_binding_item *binding_items; size_t binding_item_capacity;
  const char **shared_names; size_t shared_name_capacity;
} qa_application_input_actions;
qa_application_input_actions *qa_application_input_actions_create(qa_application *app, const qa_application_input_actions_options *options);
void qa_application_input_actions_destroy(qa_application_input_actions *actions);
/* C-only: releases what the actions own but not the record (a kept application record is freed by its keeper). */
void qa_application_input_actions_release(void *record);
void qa_application_dispatch_client_inputs(qa_application *app);
/* TS bindServerSettingCommand: returns the unregister closure. */
qa_application_release qa_application_bind_server_setting_command(qa_application *app, qa_command_buffer *commands,
    qa_app_shared_simulation *simulation);
qa_debug_graph_settings qa_application_debug_graph_settings(qa_application *app);
/* TS createDebugGraphOverlay(assets): the returned closure (draw, view, components). Owned by the caller (free). */
typedef struct qa_application_graph_overlay {
  void (*draw)(void *ctx, const qa_draw2d *draw, qa_rect view, qa_app_component_drawings *components);
  void *ctx;
  qa_application *app;
  qa_app_application_assets *assets;
  qa_app_provider_scene_assets provider;
} qa_application_graph_overlay;
qa_application_graph_overlay *qa_application_create_debug_graph_overlay(qa_application *app, qa_app_application_assets *assets);
void qa_application_refresh_application_tools(qa_application *app);
void qa_application_bind_source_commands(qa_application *app, bool restoring);
void qa_application_mod_command(qa_application *app, qa_command_invocation *command, qa_app_shared_simulation *simulation);
qa_command_buffer *qa_application_create_source_command_buffer(qa_application *app, const qa_command_buffer_options *options);
void qa_application_publish_source_commands(qa_application *app, qa_application_prepared_source_commands *prepared);
/* TS prepareSourceCommands(simulation, content, restoring, action). The result
 * is owned by the caller until destroy (prepared->destroy) or publication
 * (publishSourceCommands takes it over). */
qa_application_prepared_source_commands *qa_application_prepare_source_commands(qa_application *app,
    qa_app_shared_simulation *simulation, qa_app_loaded_content *content, bool restoring,
    void (*action)(void *ctx, const qa_application_command_request *request), void *action_ctx);
/* TS kickTargets(args): malloc'd actor array in *out. */
size_t qa_application_kick_targets(qa_application *app, const char *const *args, size_t count, qa_actor_id **out);
void qa_application_kick_clients(qa_application *app, const char *const *args, size_t count);
void qa_application_request_restart(qa_application *app, const char *const *args, size_t count);
void qa_application_append_q1_commands(qa_application *app, const qa_app_sim_presentation_event *const *events, size_t count);
void qa_application_write_game_log(qa_application *app, qa_app_shared_simulation *simulation, const char *text);
void qa_application_close_game_log(qa_application *app);

/* ---- application_services.c: 1751-2308 ---- */
/* TS sourceActions(events = this.sourceEvents): callers pass app->source_events for the default. */
void qa_application_source_actions(qa_application *app, const qa_app_sim_presentation_event *events, size_t event_count);
/* TS ClientRecordingFeed as prepareRecording / prepareServerRecording / prepareMvdRecording
 * return it: root, seed() and attach(sink) (the returned token detaches). The feed and its
 * seed storage live until qa_application_services_free. */
typedef struct qa_application_recording_feed qa_application_recording_feed;
const char *qa_application_recording_feed_root(const qa_application_recording_feed *feed);
qa_app_demo_recording_seed qa_application_recording_feed_seed(qa_application_recording_feed *feed);
void *qa_application_recording_feed_attach(qa_application_recording_feed *feed, qa_app_demo_recording_sink sink);
void qa_application_recording_feed_detach(qa_application_recording_feed *feed, void *token);
qa_app_client_demo_recording *qa_application_standalone_recording(qa_application *app);
qa_application_recording_feed *qa_application_prepare_mvd_recording(qa_application *app, qa_command_context context);
qa_application_recording_feed *qa_application_prepare_server_recording(qa_application *app, qa_command_context context);
qa_application_recording_feed *qa_application_prepare_recording(qa_application *app, qa_command_context context);
qa_app_local_recording_source qa_application_local_recording_source(qa_application *app);
/* TS operatorHost(): ctx is the application. */
qa_app_server_operator_host qa_application_operator_host(qa_application *app);
/* TS executeAdministration(text, output, limited = false). */
void qa_application_execute_administration(qa_application *app, const char *text,
    void (*output)(void *ctx, const char *text), void *output_ctx, bool limited);
/* TS networkHost(simulation = this.simulation, content = this.content, serverCount = this.nativeWorldCount):
 * NULL / NULL / negative select the defaults. The host and its closures live until qa_application_services_free. */
qa_application_native_server_host qa_application_network_host(qa_application *app, qa_app_shared_simulation *simulation,
    qa_app_loaded_content *content, double server_count);
void qa_application_validate_network_host(qa_application *app, const qa_application_native_server_host *next);
void qa_application_change_network_world(qa_application *app, const qa_application_native_server_host *next);
void qa_application_open_network(qa_application *app);
void qa_application_open_graphical(qa_application *app, const qa_save_image *initial_save);
const char *qa_application_capture_map(qa_application *app);
void qa_application_prepare_retirement(qa_application *app);
/* TS readClientResource(path): malloc'd bytes; false is TS undefined. */
bool qa_application_read_client_resource(qa_application *app, const char *path, uint8_t **out_bytes, size_t *out_length);
void qa_application_release_settings(qa_application *app);
void qa_application_adopt_image_settings(qa_application *app, qa_app_image_settings *settings);
void qa_application_retire(qa_application *app);
/* C-only: releases the hosts, transports, feeds and closures the services created (TS GC). */
void qa_application_services_free(qa_application *app);

/* ---- application_seats.c: 2310-2846 ---- */
/* TS publishBorrowed(client, images): images is ApplicationImageSettings.prepareClientSettings()'s result. */
void qa_application_publish_borrowed(qa_application *app, qa_app_client_bootstrap *client, const qa_app_image_client_settings *images);
/* simulation NULL is TS this.simulation. */
bool qa_application_is_team_arena_skirmish(qa_application *app, qa_app_shared_simulation *simulation);
void qa_application_prepare_base_arena_progress(qa_application *app, qa_app_shared_simulation *simulation,
    qa_app_loaded_content *content, bool restoring);
/* TS static startupScriptReader(content, options, scripts). */
bool qa_application_startup_script_reader(qa_app_loaded_content *content, const qa_app_options *options,
    qa_app_console_script_files *scripts, qa_app_prepared_scoped_read_fn *out_read, void **out_read_ctx);
/* TS queueLocalGuestCommand: takes over the request's storage. */
void qa_application_queue_local_guest_command(qa_application *app, qa_application_local_q3_guest_world *world,
    qa_application_command_request *command);
void qa_application_start_team_arena_skirmish(qa_application *app, const qa_app_team_arena_skirmish *skirmish);
void qa_application_publish_local_guest_snapshots(qa_application *app);
void qa_application_publish_keys(qa_application *app);
void qa_application_request_restored_scores(qa_application *app);
void qa_application_replace_world(qa_application *app, const char *map, const qa_app_simulation_travel *carry);
void qa_application_complete_base_arena(qa_application *app, const char *const *args, size_t arg_count);
/* TS baseArenaMenu(simulation, seat): false is TS undefined; the service lives until destroy. */
bool qa_application_base_arena_menu(qa_application *app, qa_app_shared_simulation *simulation, qa_seat_id seat,
    qa_app_base_arena_menu_service *out);
/* TS teamArenaResults(simulation, seat): false is TS undefined; the service lives until destroy. */
bool qa_application_team_arena_results(qa_application *app, qa_app_shared_simulation *simulation, qa_session_seat *seat,
    qa_app_team_arena_result_service *out);
/* TS prepareKeys(content, simulation): the profile stays in keyProfiles. */
qa_app_key_profile *qa_application_prepare_keys(qa_application *app, qa_app_loaded_content *content, qa_app_shared_simulation *simulation);
/* TS loadClientArchive(seat, content, options = this.options): options NULL is the default. Malloc'd entries
 * (qa_app_cvar_archive_entries_free). */
qa_cvar_archive_entry *qa_application_load_client_archive(qa_application *app, qa_session_seat *seat, qa_app_loaded_content *content,
    const qa_app_options *options, size_t *out_count);
/* C-only: frees a reader qa_application_startup_script_reader returned. */
void qa_application_startup_script_reader_free(void *read_ctx);
/* ---- application_bots.c: createBots and the content views of the bot modules ---- */
/* The LoadedApplicationContent members loadApplicationBotAssets / createApplicationBotNavigation read (ctx is the content). */
qa_app_sim_bot_asset_content qa_application_bot_asset_content(qa_app_loaded_content *content);
qa_app_sim_navigation_content qa_application_navigation_content(qa_app_loaded_content *content);
/* C-only: frees the record of a dropped (closed) bot service; qa_application_bots_free frees them all. */
void qa_application_bots_release(qa_application *app, const qa_app_sim_bot_service *service);
void qa_application_bots_free(qa_application *app);
/* C-only: frees the P3 storage (base arena records); called by qa_application_destroy. */
void qa_application_seats_free(qa_application *app);
/* TS recordPlayerProgress(source). */
void qa_application_record_player_progress(qa_application *app, const qa_app_sim_presentation_event *source);

/* ---- application_transition.c: WorldTransition and the world replacement transaction ---- */
/* TS replaceWorld(map, carry, initialSourceMilliseconds = 0, save?, skirmish?, published?, nativeTravel?, q3Map?,
 * guestRestart = false, q2NextServer = ""). Zeroed options (or NULL) are the TS defaults; NULL pointers are TS
 * undefined and q2_next_server NULL is "". qa_application_replace_world(app, map, carry) passes the defaults. */
typedef struct qa_application_replace_world_options {
  double initial_source_milliseconds;
  const qa_save_image *save;
  const qa_app_team_arena_skirmish *skirmish;
  void (*published)(void *ctx); void *published_ctx;
  const qa_app_q2_travel_target *native_travel;
  const qa_app_q3_map_launch *q3_map;
  bool guest_restart;
  const char *q2_next_server;
} qa_application_replace_world_options;
void qa_application_replace_world_with(qa_application *app, const char *map, const qa_app_simulation_travel *carry,
    const qa_application_replace_world_options *options);
/* TS WorldReplacementRequest (the arguments of prepareAndReplaceWorld). next_frame NULL is the TS default
 * (Bun.sleep(0): a no-op pump here); q2_next_server NULL is ""; restored_options NULL is TS undefined. */
typedef struct qa_application_world_replacement_request {
  const char *map;
  const qa_app_simulation_travel *carry;
  double initial_source_milliseconds;
  const qa_save_image *save;
  const qa_app_team_arena_skirmish *skirmish;
  const qa_app_q2_travel_target *native_travel;
  const qa_app_q3_map_launch *q3_map;
  void (*next_frame)(void *ctx); void *next_frame_ctx;
  bool guest_restart;
  const char *q2_next_server;
  const qa_app_options *restored_options;
} qa_application_world_replacement_request;
void qa_application_prepare_and_replace_world(qa_application *app, const qa_application_world_replacement_request *request);
/* TS restoreComponentClients(image, graphical, simulation). */
void qa_application_restore_component_clients(qa_application *app, const qa_save_image *image, qa_application_graphical *graphical,
    qa_app_shared_simulation *simulation);
/* TS changeLevel(map, spawnPoint = ""): spawn_point NULL is "". */
void qa_application_change_level(qa_application *app, const char *map, const char *spawn_point);
/* TS hostPrinter(): (text) => this.host.print(text). */
qa_application_printer qa_application_host_printer(qa_application *app);

/* TS AudioPreferences as createWorldAudio spreads them: has_values false is the TS {} of an absent settings document. */
typedef struct qa_application_audio_settings { bool has_values; qa_app_audio_preferences values; } qa_application_audio_settings;
qa_app_application_audio *qa_application_create_world_audio(qa_application *app, qa_app_loaded_content *content,
    const qa_app_options *options, const qa_application_audio_settings *settings, bool defer_output, qa_app_shared_simulation *simulation);
/* TS () => number clock closures (stagedClock). */
typedef double (*qa_application_staged_clock_fn)(void *ctx);
typedef struct qa_application_clock { void *ctx; qa_application_staged_clock_fn now; } qa_application_clock;
struct qa_application_world_replacement;
qa_application_clock qa_application_staged_clock(qa_application *app, struct qa_application_world_replacement *staged,
    double destination_source_milliseconds);
/* TS createPresentationServices(...): fills out->selected_q3_presentations and out->mod_presentations. */
void qa_application_create_presentation_services(qa_application *app, qa_app_application_assets *assets, qa_app_application_audio *audio,
    qa_app_application_input *input, qa_app_shared_simulation *current, qa_app_loaded_content *content, qa_app_native_renderer *shared_renderer,
    qa_application_clock mod_clock, qa_application_graphical *out);
/* TS createWorldAssets(content, { owner, images }, this.hostPrinter(), this.imageSettings). */
qa_app_application_assets *qa_application_create_world_assets(qa_application *app, qa_app_loaded_content *content,
    qa_renderer_resource_owner owner, qa_scene_image_registry *root_images);
/* The userinfo a restored local seat's registry starts from: simulation.q3Source()?.host.engine.getUserinfo(slot) ??
 * q2Native()?.clients.find(slot + 1)?.userinfo ?? the player's sourcePlayerUserinfo ?? null. Malloc'd; NULL is TS null. */
char *qa_application_restored_userinfo(qa_app_shared_simulation *simulation, qa_session_seat *seat);
qa_app_capture *qa_application_create_application_capture(qa_application *app, qa_app_application_input *input, qa_app_native_renderer *renderer);
qa_app_video_restart *qa_application_create_video_restart(qa_application *app, qa_app_native_renderer *renderer);

/* ---- P1/P2/P3 members the P4/P5 members call (not done; see docs/application_layout.md) ---- */
/* P1 static guestOptions(content, options, host, guestCommands, graph, nativeCommand): fills the guest members of *out. */
void qa_application_guest_options(qa_app_loaded_content *content, const qa_app_options *options, const qa_application_host *host,
    qa_command_buffer *(*guest_commands)(void *ctx), void *guest_ctx, qa_source_debug_graph *graph,
    void (*native_command)(void *ctx, const char *text), void *native_ctx, qa_app_simulation_options *out);
/* P2 prepareNativeQ2Seat(local, assets, simulation, cvars): false is TS null. */
bool qa_application_prepare_native_q2_seat(qa_application *app, qa_app_local_input *local, qa_app_application_assets *assets,
    qa_app_shared_simulation *simulation, qa_cvar_registry *cvars, qa_application_native_q2_seat_client *out);
/* P2 prepareVideoRestart(): false is TS null. */
bool qa_application_prepare_video_restart(qa_application *app, qa_app_prepared_video_presentation *out);
/* P3 prepareSavedClients(slots, botClients). */
qa_application_saved_clients *qa_application_prepare_saved_clients(qa_application *app, const double *slots, size_t slot_count,
    const qa_app_sim_bot_transport_connection *bot_clients, size_t bot_count);
/* P3 createClientCvars(simulation, content, options, seat, userinfo, previous?, archive = [], selectedIdentity = false):
 * userinfo / previous NULL are TS null / undefined. */
qa_cvar_registry *qa_application_create_client_cvars(qa_application *app, qa_app_shared_simulation *simulation,
    qa_app_loaded_content *content, const qa_app_options *options, qa_session_seat *seat, const char *userinfo,
    qa_cvar_registry *previous, const qa_cvar_archive_entry *archive, size_t archive_count, bool selected_identity);
/* P3 prepareLocalGuestSeat(world, simulation, seat, cvars, userinfo, initial): initial is the authority gameState result. */
void qa_application_prepare_local_guest_seat(qa_application *app, qa_application_local_q3_guest_world *world,
    qa_app_shared_simulation *simulation, qa_session_seat *seat, qa_cvar_registry *cvars, const char *userinfo, const void *initial);
/* P3 prepareRestoredGuestSeat(world, simulation, seat, content, options, previous?, archive = []). */
qa_actor_id qa_application_prepare_restored_guest_seat(qa_application *app, qa_application_local_q3_guest_world *world,
    qa_app_shared_simulation *simulation, qa_session_seat *seat, qa_app_loaded_content *content, const qa_app_options *options,
    qa_cvar_registry *previous, const qa_cvar_archive_entry *archive, size_t archive_count);
/* TS openGuestBrowser(): StartupServerBrowser.open(inputConfig) plus its Q3BrowserView; raises what open raises
 * (the host is closed first when the view cannot be built). NULL only on OOM. */
qa_application_local_q3_guest_browser *qa_application_open_guest_browser(qa_application *app);
/* C-only: frees a closed (or never published) guest browser record, its view, adapter and host (TS GC). */
void qa_application_release_guest_browser(qa_application_local_q3_guest_browser *browser);
/* P3 createQ3SeatClient(local, assets, audio, input, renderer, simulation, settings?, localGuest, browser, cvars): false is
 * TS null; settings NULL is TS undefined. */
bool qa_application_create_q3_seat_client(qa_application *app, qa_app_local_input *local, qa_app_application_assets *assets,
    qa_app_application_audio *audio, qa_app_application_input *input, qa_app_native_renderer *renderer, qa_app_shared_simulation *simulation,
    const qa_cvar_snapshot *settings, size_t setting_count, qa_application_local_q3_guest_world *local_guest,
    qa_application_local_q3_guest_browser *browser, qa_cvar_registry *cvars, qa_application_q3_seat_client *out);
/* P3 restoreSourceInputAngles(input, simulation, seat?): seat NULL is TS undefined. */
void qa_application_restore_source_input_angles(qa_application *app, qa_app_application_input *input, qa_app_shared_simulation *simulation,
    const qa_seat_id *seat);
/* P3 publishLocalGuestSnapshots(local, simulation) with explicit arguments. */
void qa_application_publish_local_guest_snapshots_with(qa_application *app, qa_application_local_q3_guest_world *local,
    qa_app_shared_simulation *simulation);

/* ---- application_travel.c: save, load and travel ---- */
/* The ApplicationQ3Client local-round closures over a Q3 source and a seat's prediction host (restartSourceRound and
 * createQ3SeatClient): linkBounds, sourceActor, predictionCommand and serverSettings. ctx is the binding (the C client
 * shares one ctx among its option members, so a caller's record may embed the binding as its first member). */
typedef struct qa_application_q3_round_binding {
  qa_app_q3_source_runtime *source;
  qa_app_presentation_prediction_adapter *adapter;
  qa_cvar_snapshot *settings; size_t setting_count; /* the last serverSettings() result */
} qa_application_q3_round_binding;
bool qa_application_round_link_bounds(void *ctx, double number, qa_bounds *out);
bool qa_application_round_source_actor(void *ctx, double number, qa_actor_id *out);
qa_q3_base_user_command qa_application_round_prediction_command(void *ctx, qa_actor_command command, double time);
qa_cvar_snapshot *qa_application_round_server_settings(void *ctx, size_t *out_count);
/* Frees the binding's serverSettings storage (not the binding). */
void qa_application_round_binding_release(void *record);
/* TS saveGame(path, format = "shared"): format NULL is the default. */
void qa_application_save_game(qa_application *app, const char *path, qa_app_save_format format);
/* TS loadGame(path, sourceProduct?) / restoreSavedGame: source_product NULL is TS undefined. */
void qa_application_load_game(qa_application *app, const char *path, const char *source_product);
void qa_application_restore_saved_game(qa_application *app, const char *path, const char *source_product);
bool qa_application_restart_source_round(qa_application *app);
bool qa_application_capture_blocks_transition(qa_application *app);
void qa_application_apply_transition(qa_application *app);
void qa_application_travel_or_restart(qa_application *app, const char *map, const qa_app_simulation_travel *carry);
void qa_application_report_campaign_travel_error(qa_application *app, const qa_caught_error *error);
/* TS advanceQ2Travel(target, carry): carry NULL is TS null. */
void qa_application_advance_q2_travel(qa_application *app, const qa_app_q2_travel_target *target, const qa_app_simulation_travel *carry);
void qa_application_activate_movie(qa_application *app, qa_app_campaign_cinematic *playback);
/* TS reserveMovie(simulation): the request token (TS `{}` identity). */
uint64_t qa_application_reserve_movie(qa_application *app, qa_app_shared_simulation *simulation);
/* TS CinematicTimeline as ScreenCinematicCaptions.commands reads it. */
typedef qa_app_cinematic_caption_timeline qa_application_cinematic_timeline;
/* TS ScreenCinematicCaptions (campaign_cinematic.h). */
typedef qa_app_screen_cinematic_captions qa_application_screen_cinematic_captions;
/* TS screenCaptions(simulation, content, seat): the record lives until destroy. */
qa_application_screen_cinematic_captions *qa_application_screen_captions(qa_application *app, qa_app_shared_simulation *simulation,
    qa_app_loaded_content *content, qa_seat_id seat);
/* TS systemCinematics(simulation, content, assets, audio, renderer, seat, input, component?): component NULL is TS
 * undefined. The host and its handles live until destroy. */
const qa_app_system_cinematic_host *qa_application_system_cinematics(qa_application *app, qa_app_shared_simulation *simulation,
    qa_app_loaded_content *content, qa_app_application_assets *assets, qa_app_application_audio *audio,
    qa_app_native_renderer *renderer, qa_seat_id seat, qa_app_application_input *input,
    const qa_app_component_cinematic_scope *component);
bool qa_application_underwater(qa_application *app, qa_scene_camera camera, qa_actor_id actor);
qa_rect qa_application_local_viewport(qa_application *app, qa_seat_id seat, double width, double height);
void qa_application_retire_removed_local_players(qa_application *app);

/* ---- application_dispatch.c: 4241-4930 ---- */
void qa_application_after_command_dispatch(qa_application *app);
bool qa_application_pending_shell_publication(qa_application *app);
void qa_application_rank_local_match(qa_application *app);
/* TS commands(target?, requests?, held?): NULL requests reads the queue. */
void qa_application_commands(qa_application *app);
/* TS commands(undefined, undefined, routeApplications). */
void qa_application_commands_routed(qa_application *app, bool route_applications);
/* TS changeLocalPlayers(kind, args, invokingSeat): kind "join" | "drop". */
void qa_application_change_local_players(qa_application *app, const char *kind, const char *const *args, size_t count,
    qa_seat_id invoking_seat);
/* The same member with TS invokingSeat null (invoking_seat NULL). */
void qa_application_change_local_players_seat(qa_application *app, const char *kind, const char *const *args, size_t count,
    const qa_seat_id *invoking_seat);
/* TS commandActor(seat): seat NULL is TS null; raises "Command requires a player". */
qa_actor_id qa_application_command_actor(qa_application *app, const qa_seat_id *seat);
/* TS presentOutput(output, frame). */
typedef struct qa_application_present_frame {
  const qa_app_sim_presentation_event *const *source_events; size_t source_event_count;
  const qa_app_sim_presentation_event *const *round_events; size_t round_event_count;
  const qa_actor_command *local_commands; size_t local_command_count;
  double frame_milliseconds, frame_started_at, time_milliseconds;
} qa_application_present_frame;
void qa_application_present_output(qa_application *app, qa_simulation_output output, const qa_application_present_frame *frame);

/* ---- application_step.c / application_close.c (done) ---- */
void qa_application_recover_dedicated_frame(qa_application *app, const qa_caught_error *error);
const uint8_t *qa_application_read_pixels(qa_application *app, size_t *out_count);
/* TS closeFailedWorld: Promise<never>. */
noreturn void qa_application_close_failed_world(qa_application *app, const qa_caught_error *error);
void qa_application_close_owned(qa_application *app);

/* ---- Members of other modules the application reads (TS
 * ApplicationQ3Client.userCommandSelection in q3-client.ts). Their definitions belong to those modules. */
qa_app_q3_command_selection qa_app_application_q3_client_user_command_selection(qa_app_application_q3_client *client);
/* TS ApplicationQ3Client.input(event) (q3-client.ts; unported). */
void qa_app_application_q3_client_input(qa_app_application_q3_client *client, const qa_seat_input_event *event);
/* TS ApplicationQ3Client.resourceDiagnostics() (q3-client.ts; unported). NULL is TS null. */
qa_q3_pres_q3_renderer_resources *qa_app_application_q3_client_resource_diagnostics(qa_app_application_q3_client *client);

/* ---- TS-shaped members of other modules that the P4/P5 members call and whose C ports are host-shaped or
 * missing (docs/application_layout.md "Dependency gaps"). Declared here, defined by those modules. ---- */
/* original-save.ts prepareApplicationSave: qa_app_prepare_application_save_native (original_save.h). */
/* The SharedSimulation views createSimulationPredictionHost and ApplicationInput take (the access structs of
 * shared_simulation_access.c). */
qa_app_simulation_prediction_source qa_app_shared_sim_prediction_source(qa_app_shared_simulation *sim);
qa_app_sim_presentation_access qa_app_shared_sim_presentation_access(qa_app_shared_simulation *sim);
/* Q3SourceRuntime.settings.definitions names (restartSourceRound's serverSettings filter); borrowed. */
const char *const *qa_app_q3_source_runtime_setting_names(qa_app_q3_source_runtime *runtime, size_t *out_count);
/* Lower-layer members stageGraphical, createPresentationServices and the capture owner call with the TS shapes; their C
 * ports take hosts (docs/application_layout.md "Dependency gaps"). */
void qa_app_save_audio_settings_from(const qa_config_store *store, qa_app_application_audio *audio);
qa_app_audio_haptics qa_app_application_input_audio_haptics(qa_app_application_input *input);
const qa_scene_queries *qa_app_shared_sim_scene_queries(qa_app_shared_simulation *sim);
void qa_app_apply_frontend_preferences(qa_app_frontend_preference_overrides overrides, qa_app_application_input *input,
    qa_app_application_audio *audio);
void qa_app_apply_frontend_input(qa_app_frontend_preference_values values, qa_app_local_input *local);
const qa_q3_character_assets *qa_app_load_q3_character_for_content(qa_app_loaded_content *content, qa_content_id appearance,
    const char *model);
const char *qa_app_native_renderer_driver_name(qa_app_native_renderer *renderer);
qa_app_qvm_display_renderer qa_app_native_renderer_qvm_display(qa_app_native_renderer *renderer);
qa_app_component_media_host qa_app_component_media_host_of(qa_app_shared_simulation *simulation, qa_app_application_audio *audio,
    qa_app_application_assets *assets);
void qa_app_client_bootstrap_publish_output(qa_app_client_bootstrap *client, qa_unified_audio *engine);
/* P6 finishQ2Match(transition?) (application_dispatch.c): transition NULL is TS undefined. */
bool qa_application_finish_q2_match(qa_application *app, const qa_app_native_q2_map_transition *transition);
#endif
