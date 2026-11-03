/* Port of src/app/bootstrap/startup-menu.ts: the StartupMenu class over the
 * NativeUiController, the StartupSelectionModel (startup_selection.h), the
 * menu art (NativeUiArt), the body/title fonts and the character turntable.
 * C accommodations:
 *  - StartupMenuOptions closures are fn+ctx with one ctx shared by every
 *    callback; NULL fns are absent TS options. The model, art, turntable,
 *    browser, settings, library services and LLM service are borrowed for
 *    the menu's life.
 *  - Async model preparations (refreshBaseArenas, prepareMonsterRoster,
 *    prepareMapChoices) run synchronously; a raise is the TS rejection the
 *    menu catches into its status.
 *  - draw() emits through emit/material (fn+ctx); captionCommands() returns
 *    an array owned by the menu until its next captionCommands call.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_MENU_H
#define QA_APP_BOOTSTRAP_STARTUP_MENU_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/base_arena_select_menu.h"
#include "qa/app/bootstrap/character_turntable.h"
#include "qa/app/bootstrap/server_browser.h"
#include "qa/app/bootstrap/startup_saves.h"
#include "qa/app/bootstrap/startup_selection.h"
#include "qa/contracts/common.h"
#include "qa/contracts/render.h"
#include "qa/contracts/ui.h"
#include "qa/llm/settings.h"
#include "qa/text/atlas.h"
#include "qa/text/captions.h"
#include "qa/text/draw2d.h"
#include "qa/ui/common/assets.h"
#include "qa/ui/common/controller.h"
#include "qa/ui/library/menu.h"
#include "qa/ui/mods/menu.h"
#include "qa/ui/settings/bindings.h"
#include "qa/ui/settings/gyro.h"
#include "qa/ui/settings/index.h"
#include "qa/ui/settings/local_lobby.h"

/** TS StartupMenuOptions. ctx is shared by every callback. */
typedef struct qa_app_startup_menu_options {
  void *ctx;
  /* lobby?: { current(), selection(), seats() } (has_lobby false = undefined). */
  bool has_lobby;
  const qa_ui_application_local_lobby *(*lobby_current)(void *ctx);
  const char *(*lobby_selection)(void *ctx, qa_ui_local_lobby_selection *out);
  double (*lobby_seats)(void *ctx);
  void (*sound)(void *ctx, qa_ui_sound sound);     /* optional */
  qa_llm_settings_service *llm;                     /* optional */
  const char *(*clipboard)(void *ctx);              /* optional; NULL return is TS null */
  qa_seat_id seat;
  qa_app_startup_selection_model *model;
  qa_app_character_turntable *character_turntable;  /* optional */
  const qa_native_ui_art *art;
  qa_text_font_selection font;
  qa_text_font_selection title_font;
  double (*now)(void *ctx);
  void (*play)(void *ctx);
  void (*play_preset)(void *ctx, const char *id, double skill, const char *arena_map); /* optional; arena_map NULL = undefined */
  qa_app_startup_server_browser *browser;           /* optional */
  void (*connect)(void *ctx, qa_app_browser_connection connection); /* optional */
  void (*load)(void *ctx, const char *id);
  const qa_app_startup_save_list *(*saves)(void *ctx);
  void (*refresh_saves)(void *ctx);
  void (*quit)(void *ctx);
  const qa_ui_setting_binding *settings; size_t setting_count; /* settings? (NULL/0 = undefined) */
  qa_ui_preference_values (*appearance)(void *ctx); /* optional */
  /* teamArena?: the model's teamArena service (team_arena false = undefined). */
  bool team_arena;
  /* libraries? (has_libraries false = undefined); NULL services are absent. */
  bool has_libraries;
  const qa_ui_library_menu_service *library_addons, *library_player_progress, *library_demos,
      *library_movies, *library_server_profiles, *library_configurations;
} qa_app_startup_menu_options;

typedef struct qa_app_startup_menu qa_app_startup_menu;
/** TS new StartupMenu(options). NULL only on OOM. qa_app_startup_menu_close is
 * TS close(); destroy frees the C record. */
qa_app_startup_menu *qa_app_startup_menu_new(qa_app_startup_menu_options options);
void qa_app_startup_menu_destroy(qa_app_startup_menu *m);
void qa_app_startup_menu_close(qa_app_startup_menu *m);
/** TS controller (borrowed). */
qa_native_ui_controller *qa_app_startup_menu_controller(qa_app_startup_menu *m);
void qa_app_startup_menu_open_main(qa_app_startup_menu *m);
void qa_app_startup_menu_open_local_lobby(qa_app_startup_menu *m);
bool qa_app_startup_menu_is_busy(const qa_app_startup_menu *m);
const char *qa_app_startup_menu_status(const qa_app_startup_menu *m); /* borrowed */
/** TS setStatus(text, busy = false). */
void qa_app_startup_menu_set_status(qa_app_startup_menu *m, const char *text, bool busy);
qa_ui_menu_id qa_app_startup_menu_ensure_active_menu(qa_app_startup_menu *m);
void qa_app_startup_menu_resume_display_options(qa_app_startup_menu *m);
void qa_app_startup_menu_resume_server_browser(qa_app_startup_menu *m);
/** TS input(event). */
bool qa_app_startup_menu_input(qa_app_startup_menu *m, qa_seat_input_event event);
/** TS draw(context, emit, material). */
void qa_app_startup_menu_draw(qa_app_startup_menu *m, qa_ui_draw_context context,
    void (*emit)(void *ctx, const qa_render_command *command),
    void (*material)(void *ctx, const qa_material_text_draw *draw), void *ctx);
/** TS captionCommands(captions, context): borrowed until the next call. */
const qa_render_command *qa_app_startup_menu_caption_commands(qa_app_startup_menu *m,
    const qa_active_caption *captions, size_t caption_count, qa_ui_draw_context context, size_t *out_count);
/** TS bindGyro(settings). */
void qa_app_startup_menu_bind_gyro(qa_app_startup_menu *m, const qa_ui_gyro_settings *settings);
/** TS bindInput(input, actions, reset): actions via get_source (TS
 * actions() re-read) or a fixed source array when get_source is NULL. */
void qa_app_startup_menu_bind_input(qa_app_startup_menu *m, qa_seat_input *input,
    const qa_ui_binding_action *source, size_t source_count,
    const qa_ui_binding_action *(*get_source)(void *ctx, size_t *out_count), void *source_ctx,
    const qa_ui_binding_reset *reset);
#endif
