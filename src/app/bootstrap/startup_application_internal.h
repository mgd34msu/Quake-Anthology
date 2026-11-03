/* Private state of the StartupApplication port (src/app/bootstrap/startup.ts),
 * shared by the startup_application*.c files. Each TS member keeps its TS
 * name in a comment where the C name differs. TS null is NULL (pointers) or a
 * has_* flag. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_APPLICATION_INTERNAL_H
#define QA_APP_BOOTSTRAP_STARTUP_APPLICATION_INTERNAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/startup_application.h"
#include "qa/app/bootstrap/addon_library.h"
#include "qa/app/bootstrap/application_class.h"
#include "qa/app/bootstrap/campaign_cinematic.h"
#include "qa/app/bootstrap/capture.h"
#include "qa/app/bootstrap/client_bootstrap.h"
#include "qa/app/bootstrap/config_scripts.h"
#include "qa/app/bootstrap/configuration.h"
#include "qa/app/bootstrap/console.h"
#include "qa/app/bootstrap/controller_settings.h"
#include "qa/app/bootstrap/demo_commands.h"
#include "qa/app/bootstrap/demo_recording_commands.h"
#include "qa/app/bootstrap/frontend_preferences.h"
#include "qa/app/bootstrap/image_settings.h"
#include "qa/app/bootstrap/input_devices.h"
#include "qa/app/bootstrap/keys.h"
#include "qa/app/bootstrap/local_lobby.h"
#include "qa/app/bootstrap/match_results.h"
#include "qa/app/bootstrap/remote_application_class.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/app/bootstrap/startup_audio.h"
#include "qa/app/bootstrap/startup_input_profile.h"
#include "qa/app/bootstrap/startup_menu.h"
#include "qa/app/bootstrap/startup_saves.h"
#include "qa/app/bootstrap/video_restart.h"
#include "qa/audio/music.h"
#include "qa/console/commands.h"
#include "qa/console/session.h"
#include "qa/input/router.h"
#include "qa/platform/controller.h"
#include "qa/settings/config.h"
#include "qa/ui/library/menu.h"
#include "qa/world/session/session.h"

/* TS LibraryMenuService closures (serverProfileLibrary, movieLibraryMenu,
 * demoLibraryMenu, configurationLibrary): one record per service. */
typedef struct qa_startup_library {
  qa_startup_application *app;
  qa_ui_library_menu_service service;
  qa_ui_library_entry *entries; size_t entry_count; /* owned rows (strings owned) */
  qa_ui_library_entry *visible; size_t visible_count; /* movieLibraryMenu visibleEntries() result */
  char *status;
  double generation;
  qa_config_store store; bool has_store;   /* serverProfileLibrary */
  int64_t release_completion; bool has_release_completion; /* configurationLibrary */
  const void *completion_descriptor;
  char *completion_expected;
  qa_command_context completion_local;
} qa_startup_library;

/* TS StartupGraphics (openGraphics' graphics object and the owners its close()
 * releases). */
typedef struct qa_startup_graphics {
  qa_app_startup_audio *audio;
  qa_app_image_settings *image_settings;
  /* display: Pick<ApplicationOptions, "renderer" | "gamma" | "width" | "height" | "hidden"> */
  char *display_renderer; double display_gamma, display_width, display_height; bool display_hidden;
  qa_app_native_renderer *renderer;
  qa_app_startup_menu *menu;
  qa_input_router *router;
  qa_sdl_controllers *controllers;
  qa_app_controller_settings *controller_settings;
  qa_app_startup_input_profile *input_profile;
  /* owners released by close() */
  struct qa_app_character_turntable *turntable;
  qa_app_input_devices *devices;
  struct qa_content_mounted *theme_mounts;
  struct qa_content_mounted *mounted;
  struct qa_app_menu_font *font;
  struct qa_app_menu_typography *typography;
  struct qa_native_ui_art *art;
  struct qa_scene_image_registry *images;
  void *draw_state;               /* SceneFrameBuilder, console text and presentation (graphics.c) */
  bool closed;                    /* close() ran (the openGraphics catch closes before open's close()) */
  /* Observation seam for tests: called with every frame graphics.draw hands to
   * native.execute, before it executes (TS tests spy on NativeRenderer.execute). */
  void (*frame_observer)(void *ctx, const qa_render_frame *frame); void *frame_observer_ctx;
  qa_seat_id seat; qa_client_id client;
  qa_input_controllers controllers_view;  /* the router's controllers (borrowed by the router) */
  qa_input_controllers client_controllers; /* activateFrontend's router view of client.controllers */
  qa_input_router **retired_routers; size_t retired_router_count;
  qa_app_controller_settings **retired_controller_settings; size_t retired_controller_setting_count;
} qa_startup_graphics;

/* TS StartupAction recorded for recordingPreparation / serverRecordingPreparation. */
typedef struct qa_startup_recording_preparation {
  const qa_app_client_source_lifetime *source;
  qa_remote_application *remote_source;  /* source instanceof RemoteApplication */
  qa_application *application_source;    /* source instanceof Application */
  qa_app_client_recording_feed client_feed; /* ClientSourceLifetime.prepareRecording feed */
  struct qa_application_recording_feed *feed; /* Application server/mvd feed */
  bool active;
} qa_startup_recording_preparation;

struct qa_startup_application {
  qa_startup_application_host host;
  qa_app_startup_selection_model *model;
  bool owns_model;
  const char *entry; /* "menu" | "run" */
  char *save_directory;
  /* TS identity and session field initializers. */
  qa_identity_owner identity; bool has_identity;
  qa_engine_session *session;
  qa_startup_graphics *graphics;
  /* The graphics openGraphics builds, before and after this.graphics publishes them: the
   * owners TS closures capture as locals (native, imageSettings, activeAudio, controllers). */
  qa_startup_graphics *graphics_local;
  qa_app_image_settings *image_settings;
  qa_application *game;
  qa_remote_application *remote;
  /* Applications this frontend opened, destroyed at close (TS GC). */
  qa_application **games; size_t game_count;
  qa_remote_application **remotes; size_t remote_count;
  /* Owned launch records whose strings the open applications borrow. */
  void **launches; size_t launch_count;
  /* Registries and consoles publishFrontendRouting / bindFrontendConsole create (TS GC). */
  qa_cvar_registry **owned_registries; size_t owned_registry_count;
  qa_app_startup_server_browser *browser;
  qa_config_store browser_config; bool has_browser_config; /* the store the browser borrows */
  qa_ui_binding_actions binding_actions; bool has_binding_actions; /* menu bindInput actions() result */
  void *menu_input_binding_record;
  bool has_pending; qa_app_startup_action pending;
  bool stopping, closed;
  double frames;
  char *status;
  bool refresh_saves;
  qa_app_startup_saves *saves;
  char **external_directories; size_t external_directory_count; /* saves.externalDirectories */
  qa_app_addon_library *addons;
  qa_app_frontend_preferences *preferences;
  char *baseline_product;  /* NULL is TS null */
  char *baseline_input;
  qa_config_store preference_store_storage; qa_config_store *preference_store;
  qa_app_client_bootstrap client_storage; qa_app_client_bootstrap *client;
  /* lobby: ApplicationLocalLobby over host.localLobbies ?? new LocalLobbyService() */
  qa_app_local_accounts *accounts;
  qa_online_account lobby_account;
  qa_local_lobby_service *owned_lobby_service;
  qa_app_application_local_lobby lobby;
  qa_app_startup_launch *lobby_selection;
  bool lobby_transition;   /* TS lobbyTransition !== null (the C transition runs synchronously) */
  const qa_app_client_source_lifetime *lobby_source;
  qa_app_match_rankings *match_rankings; bool owns_match_rankings;
  qa_app_keys *keys;
  qa_music_controls music_controls;
  qa_app_console_script_files *scripts;
  char *frontend_routing_baseline;
  void (*release_menu_input)(qa_startup_application *app); /* NULL is TS null */
  int64_t menu_input_binding, menu_output_binding; void *menu_discovery; void *menu_binding;
  void **retired_routings; size_t retired_routing_count;          /* publishFrontendRouting state records */
  qa_app_console_routing **routing_owners; size_t routing_owner_count;
  char *clipboard_text;                                           /* last readSdlClipboard() text */
  qa_seat_console *menu_console; /* candidate created by bindFrontendConsole (owned) */
  qa_seat_console **owned_consoles; size_t owned_console_count;
  bool has_configuration_write_started;
  double last_frame;
  qa_app_client_demo_commands *demos;
  void *release_demos; /* demos.attach binding; NULL is TS null */
  bool has_pending_demo; qa_app_client_demo_intent pending_demo;
  bool initial_configuration;
  bool has_active_demo; qa_app_demo_request active_demo; char *active_demo_name; const void *active_demo_token;
  /* frontendMovie: { movie, images } */
  qa_app_campaign_cinematic *frontend_movie;
  struct qa_scene_image_registry *frontend_movie_images;
  void *frontend_movie_captions; /* caption state owned with the movie */
  bool has_pending_movie; qa_app_startup_movie_request pending_movie;
  qa_app_startup_movie_request opening_movie; /* publishPendingSource's `movie` while openFrontendMovie runs */
  void **demo_completions; size_t demo_completion_count; /* openDemoBorrowed completion closures */
  double movie_generation;
  double movie_preparations; /* TS moviePreparations.size (synchronous preparations in flight) */
  /* gtv: { source, id, label } */
  qa_remote_application *gtv_source; double gtv_id_value; char *gtv_label;
  double gtv_id;
  bool gtv_abort_active; bool gtv_aborted; /* TS gtvAbort (AbortController) */
  bool gtv_preparation;
  qa_app_client_demo_recording *recording;
  const qa_app_client_source_lifetime *recording_owner;
  qa_application *server_recording_owner;
  qa_startup_recording_preparation server_recording_preparation;
  char *recording_root;
  qa_startup_recording_preparation recording_preparation;
  bool has_pending_recording; qa_app_startup_recording_intent pending_recording;
  /* releaseSourceCommands: CleanupStack of fn+ctx */
  struct { void (*run)(qa_startup_application *app, void *ctx); void *ctx; } *release_source_commands;
  size_t release_source_command_count;
  qa_app_console_routing *frontend_routing;
  qa_startup_library *libraries[5]; /* serverProfiles, movies, demos, configurations, playerProgress */
  void *player_progress;            /* PlayerProgressLibrary state (library.c) */
  void **demo_libraries; size_t demo_library_count;   /* currentDemoLibrary() records (TS GC) */
  void **write_notifies; size_t write_notify_count;   /* configurationWriteStarted result closures */
  void *registered_commands;        /* bindDemoCommands connect/disconnect/quit handlers */
  /* openGraphics initial state */
  qa_app_prepared_initial *initial;
  qa_app_configuration_content *configuration;
  qa_config_store settings_storage; bool has_settings;
  qa_cvar_archive_entry *source_archive; size_t source_archive_count;
  qa_app_options graphics_options; bool has_graphics_options; /* openGraphics' options (initial borrows them) */
};

/* Shared helpers (startup_application.c). */
char *qa_startup_dup(const char *text);
char *qa_startup_settings_directory(const char *save_directory);
void qa_startup_set_text(char **slot, const char *text);
double qa_startup_now(void);
void qa_startup_printf(qa_startup_application *app, const char *format, ...);
void qa_startup_clear_pending_all(qa_startup_application *app);
void qa_startup_push_release(qa_startup_application *app, void (*run)(qa_startup_application *app, void *ctx), void *ctx);
/* TS this.client?.source.current; NULL when there is no client. */
const qa_app_client_source_lifetime *qa_startup_current_source(const qa_startup_application *app);
/* The lifetime the given application publishes (its ctx is the application). */
bool qa_startup_source_is_game(const qa_app_client_source_lifetime *source, const qa_application *game);
bool qa_startup_source_is_remote(const qa_app_client_source_lifetime *source, const qa_remote_application *remote);
const qa_app_options *qa_startup_active_options(qa_startup_application *app);

void qa_startup_host_print(void *ctx, const char *text);
const char *qa_startup_clipboard(qa_startup_application *app);
/* startup_application_sources.c */
void qa_startup_bind_demo_commands(qa_startup_application *app, qa_app_prepared_startup *prepared);
/* startup_application_frontend.c */
void qa_startup_bind_frontend_console(qa_startup_application *app);
void qa_startup_activate_frontend(qa_startup_application *app, const qa_app_client_frontend_configuration *configuration);
void qa_startup_return_to_frontend(qa_startup_application *app);
void qa_startup_fail_active_source(qa_startup_application *app, bool active_is_game, const qa_caught_error *error);
void qa_startup_publish_frontend_routing(qa_startup_application *app, qa_app_client_bootstrap *client);
void qa_startup_close_frontend_movie(qa_startup_application *app);
void qa_startup_open_frontend_movie(qa_startup_application *app, const qa_app_startup_movie_request *request);
char *qa_startup_frontend_routing_state(qa_input_router *router);
void qa_startup_release_menu_input(qa_startup_application *app);
void qa_startup_retire_menu_commands(void *ctx);
void qa_startup_frontend_routing_free(void *routing);
void qa_startup_movie_captions_free(void *captions);
/* startup_application_library.c */
const qa_ui_library_menu_service *qa_startup_server_profile_library(qa_startup_application *app);
const qa_ui_library_menu_service *qa_startup_movie_library_menu(qa_startup_application *app);
const qa_ui_library_menu_service *qa_startup_demo_library_menu(qa_startup_application *app);
const qa_ui_library_menu_service *qa_startup_configuration_library(qa_startup_application *app);
const qa_ui_library_menu_service *qa_startup_player_progress_library(qa_startup_application *app);
struct qa_app_demo_library *qa_startup_current_demo_library(qa_startup_application *app);
void qa_startup_libraries_free(qa_startup_application *app);
/* TS this.configurationWriteStarted?.(source, path): notify NULL is TS undefined. */
qa_configuration_write_notify qa_startup_configuration_write_started_notify(qa_startup_application *app, qa_command_context source,
    const char *path);
/* startup_application_launch.c */
void qa_startup_launch(qa_startup_application *app, qa_app_startup_action *action, bool propagate_failure);
void qa_startup_connect_options(qa_startup_application *app, const qa_app_options *options);
void qa_startup_publish_pending_source(qa_startup_application *app);
void qa_startup_dispatch_application_request(qa_startup_application *app, const qa_app_configuration_command_request *request);
void qa_startup_persist_preferences_or_warn(qa_startup_application *app);
void qa_startup_save_frontend_input(qa_startup_application *app);
void qa_startup_refresh_preference_baseline(qa_startup_application *app, bool force);
const char *qa_startup_stage_lobby_launch(qa_startup_application *app, qa_app_startup_action *action);
void qa_startup_launches_free(qa_startup_application *app);
qa_app_startup_action *qa_startup_stage(qa_startup_application *app, qa_app_startup_action_kind kind);
/* startup_application_graphics.c */
void qa_startup_open_graphics(qa_startup_application *app);
void qa_startup_graphics_draw(qa_startup_application *app, const qa_app_client_source_lifetime *loading_owner, bool has_loading_owner);
void qa_startup_graphics_close(qa_startup_application *app);
/* graphics.menu.bindInput(input, () => sharedBindingActions(...), { available, reset }). */
void qa_startup_graphics_bind_menu_input(qa_startup_application *app, qa_seat_input *input, qa_seat_id seat,
    bool (*available)(void *ctx, qa_seat_id seat), void (*reset)(void *ctx, qa_seat_id seat));
#endif
