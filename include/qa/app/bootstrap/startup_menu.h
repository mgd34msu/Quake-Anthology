/* Port of src/app/bootstrap/startup-menu.ts. Menu tree construction,
 * campaign/lobby/saves services and main-loop/menu-state types
 * (sibling bootstrap + ui slices) are host fn+ctx; menu lifetime,
 * registration, event routing and graphic navigation ordering are
 * kept here. Async -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_MENU_H
#define QA_APP_BOOTSTRAP_STARTUP_MENU_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/options.h"
#include "qa/contracts/common.h"
#include "qa/contracts/render.h"
/* qa_menu_key_event mirror below (input barrel sibling name). */
#include "qa/platform/sdl.h"
/* Mirror: TS MenuKeyEvent (input/core.ts). */
typedef struct qa_menu_key_event { const char *key; bool down; } qa_menu_key_event;
#include "qa/ui/common/controller.h"
#include "qa/app/bootstrap/base_arena_select_menu.h"
#include "qa/app/bootstrap/server_browser.h"
#include "qa/app/bootstrap/startup_saves.h"
#include "qa/llm/settings.h"
#include "qa/ui/library/menu.h"
#include "qa/ui/mods/menu.h"
#include "qa/ui/settings/bindings.h"
#include "qa/ui/settings/gyro.h"
#include "qa/ui/settings/index.h"
#include "qa/ui/settings/local_lobby.h"
typedef struct qa_app_startup_menu_host {
  void *ctx;
  const char *(*build_menus)(void *ctx);
  qa_native_ui_controller *(*controller)(void *ctx);
  bool (*handle_event)(void *ctx, const qa_sdl_event *event);
  void (*graphic_navigation)(void *ctx, const qa_menu_key_event *event);
  void (*record_command)(void *ctx, const char *command);
  void (*commands_append)(void *ctx, const char *text, const qa_command_context *source);
  void (*navigate)(void *ctx, const char *target);
  void (*set_display_mode)(void *ctx, bool hidden);
  void (*enter_game)(void *ctx);
  void (*leave_game)(void *ctx);
  void (*prepare_saves)(void *ctx);
  void (*close_saves)(void *ctx);
  const char *(*resolve)(void *ctx);
  void (*close_menu)(void *ctx);
  bool (*open)(void *ctx);
  void (*pump)(void *ctx);
  int (*run_frame)(void *ctx); /* -1 continue, 0 selected, 1 failed */
  void (*close_services)(void *ctx);
} qa_app_startup_menu_host;
typedef struct qa_app_startup_menu qa_app_startup_menu;
qa_app_startup_menu *qa_app_startup_menu_create(qa_app_startup_menu_host host);
void qa_app_startup_menu_destroy(qa_app_startup_menu *m);
void qa_app_startup_menu_graphic_navigation(qa_app_startup_menu *m, const qa_menu_key_event *event);
bool qa_app_startup_menu_handle(qa_app_startup_menu *m, const qa_sdl_event *event);
void qa_app_startup_menu_command(qa_app_startup_menu *m, const char *command);
void qa_app_startup_menu_set_display_mode(qa_app_startup_menu *m, bool hidden);
bool qa_app_startup_menu_open(qa_app_startup_menu *m);
void qa_app_startup_menu_pump(qa_app_startup_menu *m);
void qa_app_startup_menu_prepare_frame(qa_app_startup_menu *m, qa_render_frame frame);
void qa_app_startup_menu_navigate(qa_app_startup_menu *m, const char *target);
void qa_app_startup_menu_close(qa_app_startup_menu *m);
/* --- TS StartupMenu class body (StartupMenuOptions) over the landed
 * NativeUiController + ui/settings, ui/mods, ui/library and
 * base-arena-select-menu registrations. Model rows/presets carry the
 * full TS StartupSelectionChoice fields the landed
 * startup_selection.h mirror lacks, so the model is fn+ctx here.
 * All returned arrays/strings are borrowed until the next model call.
 * Error-returning model fns return an error message or NULL (TS
 * throws that startup-menu.ts catches into status). Async model
 * preparations run synchronously. */
/** TS StartupSelectionChoice. unavailable/group/edition NULL = absent/null. */
typedef struct qa_app_startup_menu_choice {
  const char *id; const char *label; const char *unavailable;
  const char *group; const char *edition; /* "classic"|"rerelease" */
} qa_app_startup_menu_choice;
/** TS StartupSelectionRow (id is the field name). */
typedef struct qa_app_startup_menu_row {
  const char *field; const char *label; const char *value;
  const qa_app_startup_menu_choice *choices; size_t choice_count;
} qa_app_startup_menu_row;
/** TS MonsterRosterRow. classname NULL = TS null. */
typedef struct qa_app_startup_menu_monster_row {
  const char *classname; const char *label; const char *value;
  const char *effective_label;
  const qa_app_startup_menu_choice *choices; size_t choice_count;
} qa_app_startup_menu_monster_row;
/** TS StartupNativePreset (productId/rules/mode unused by the menu). */
typedef struct qa_app_startup_menu_preset {
  const char *id; const char *label; const char *unavailable;
  const char *family; /* "q1"|"q2"|"q3" */
  const char *edition; const char *default_skill;
  const qa_app_startup_menu_choice *difficulties; size_t difficulty_count;
} qa_app_startup_menu_preset;
/** TS StartupHosting. q1_protocol_version meaningful when has_q1_protocol. */
typedef struct qa_app_startup_menu_hosting {
  const char *kind; /* "offline"|"native-server"|"unified-server" */
  double port; bool has_q1_protocol; double q1_protocol_version;
} qa_app_startup_menu_hosting;
/** TS StartupMenuOptions. ctx is shared by every callback. */
typedef struct qa_app_startup_menu_options {
  void *ctx;
  qa_seat_id seat;
  qa_resource_id skin_font;               /* TS art.skin.font */
  double (*now)(void *ctx);               /* required */
  void (*sound)(void *ctx, qa_ui_sound sound); /* optional */
  const char *(*clipboard)(void *ctx);    /* optional fn */
  /* TS measure() over layoutText; NULL falls back to byte length*scale. */
  double (*measure)(void *ctx, const char *text, double scale);
  /* StartupSelectionModel surface used by startup-menu.ts. rows()
   * returning NULL is the TS throw that safeRows() turns into []. */
  const qa_app_startup_menu_preset *(*presets)(void *ctx, size_t *out_count);
  const qa_app_startup_menu_row *(*rows)(void *ctx, size_t *out_count);
  void (*select)(void *ctx, const char *field, const char *id);
  void (*select_monster)(void *ctx, const char *classname, const char *id);
  void (*select_monster_source)(void *ctx, const char *id);
  qa_app_startup_menu_row (*monster_source_row)(void *ctx);
  const qa_app_startup_menu_monster_row *(*monster_roster_rows)(void *ctx, size_t *out_count);
  const char *(*prepare_monster_roster)(void *ctx);
  /* *out_prepared false is the TS null (no preparation needed). */
  const char *(*prepare_map_choices)(void *ctx, double offset, double count, bool *out_prepared);
  const char *(*refresh_base_arenas)(void *ctx);
  const qa_app_arena_selection *(*base_arenas)(void *ctx); /* NULL = TS null */
  qa_app_startup_menu_hosting (*hosting)(void *ctx);
  const char *(*set_hosting)(void *ctx, qa_app_startup_menu_hosting value);
  const char *(*mode)(void *ctx);            /* model.options.mode */
  const char *(*product_family)(void *ctx);  /* catalog product family */
  const qa_ui_mod_menu_service *mods;        /* borrowed; NULL skips */
  void (*play)(void *ctx);
  void (*play_preset)(void *ctx, const char *id, double skill, const char *arena_map); /* optional */
  void (*load)(void *ctx, const char *id);
  const qa_app_startup_save_list *(*saves)(void *ctx);
  void (*refresh_saves)(void *ctx);
  void (*quit)(void *ctx);
  qa_app_startup_server_browser *browser;    /* optional, borrowed */
  void (*connect)(void *ctx, qa_app_browser_connection connection); /* optional */
  const qa_ui_setting_binding *settings; size_t setting_count; /* borrowed */
  bool has_libraries;
  const qa_ui_library_menu_service *library_addons, *library_demos, *library_movies,
      *library_configurations, *library_server_profiles, *library_player_progress;
  bool has_lobby;
  const qa_ui_application_local_lobby *(*lobby_current)(void *ctx);
  const char *(*lobby_selection)(void *ctx, qa_ui_local_lobby_selection *out);
  double (*lobby_seats)(void *ctx);
  qa_llm_settings_service *llm; /* optional, borrowed */
  bool has_team_arena;
  const qa_ui_choice *(*team_choices)(void *ctx, size_t *out_count);
  void (*team_read)(void *ctx, const char **out_player, const char **out_opponent);
  void (*team_write)(void *ctx, const char *side, const char *team);
} qa_app_startup_menu_options;
/** TS StartupMenu constructor. NULL only on OOM. Destroy with
 * qa_app_startup_menu_destroy; qa_app_startup_menu_close is TS close(). */
qa_app_startup_menu *qa_app_startup_menu_new(qa_app_startup_menu_options options);
qa_native_ui_controller *qa_app_startup_menu_controller(qa_app_startup_menu *m);
void qa_app_startup_menu_open_main(qa_app_startup_menu *m);
void qa_app_startup_menu_open_local_lobby(qa_app_startup_menu *m);
bool qa_app_startup_menu_is_busy(const qa_app_startup_menu *m);
const char *qa_app_startup_menu_status(const qa_app_startup_menu *m); /* borrowed */
void qa_app_startup_menu_set_status(qa_app_startup_menu *m, const char *text, bool busy);
qa_ui_menu_id qa_app_startup_menu_ensure_active_menu(qa_app_startup_menu *m);
void qa_app_startup_menu_resume_display_options(qa_app_startup_menu *m);
void qa_app_startup_menu_resume_server_browser(qa_app_startup_menu *m);
/** TS input(event). */
bool qa_app_startup_menu_input(qa_app_startup_menu *m, qa_seat_input_event event);
/** TS bindGyro(settings). */
void qa_app_startup_menu_bind_gyro(qa_app_startup_menu *m, const qa_ui_gyro_settings *settings);
/** TS bindInput(input, actions, reset): actions via get_source (TS
 * actions() re-read) or a fixed source array when get_source is NULL. */
void qa_app_startup_menu_bind_input(qa_app_startup_menu *m, qa_seat_input *input,
    const qa_ui_binding_action *source, size_t source_count,
    const qa_ui_binding_action *(*get_source)(void *ctx, size_t *out_count), void *source_ctx,
    const qa_ui_binding_reset *reset);
#endif
