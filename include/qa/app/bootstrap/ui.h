/* Port of src/app/bootstrap/ui.ts: ApplicationSeatUi, one seat's native
 * menus, settings, binding editor, weapon wheel, HUD messages and HUD
 * drawing.
 *
 * C accommodations:
 *  - The TS constructor's long positional list becomes
 *    qa_app_seat_ui_options; NULL pointers are TS undefined.
 *  - prepare/prepareImageRefresh/prepareNativeQ2Hud are synchronous
 *    (PORTING.md); prepareImageRefresh's returned commit closure is the
 *    qa_app_seat_ui_image_refresh handle.
 *  - The weapon HUD assets, Q1 wheel, sound captions, native Q2 HUD and game
 *    prompt read the ApplicationAssets passed to prepare, as TS.
 *  - sharedSettings(): ApplicationInput returns the shared registry current
 *    at construction; the TS view follows a later publishSharedCvars.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_UI_H
#define QA_APP_BOOTSTRAP_UI_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/assets.h"
#include "qa/app/bootstrap/audio.h"
#include "qa/app/bootstrap/base_arena_menu.h"
#include "qa/app/bootstrap/game_prompt.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/menu_font.h"
#include "qa/app/bootstrap/q1_wheel.h"
#include "qa/app/bootstrap/q2_native_hud.h"
#include "qa/app/bootstrap/rerelease_presentation.h"
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/app/bootstrap/sound_captions.h"
#include "qa/app/bootstrap/team_arena_results.h"
#include "qa/app/bootstrap/weapon_hud.h"
#include "qa/contracts/render.h"
#include "qa/contracts/ui.h"
#include "qa/text/atlas.h"
#include "qa/text/captions.h"
#include "qa/text/ui.h"
#include "qa/ui/common/assets.h"
#include "qa/ui/common/controller.h"
#include "qa/ui/common/localization.h"
#include "qa/ui/hud/index.h"
#include "qa/ui/hud/q2_native.h"
#include "qa/ui/saves/menu.h"
#include "qa/ui/settings/gameplay.h"
#include "qa/ui/settings/index.h"
#include "qa/ui/settings/llm.h"
#include "qa/ui/settings/rankings.h"
#include "qa/ui/settings/server.h"
#include "qa/ui/settings/services.h"

/* ui.ts menuCatalogs: translations register per lowercase language name. */
void qa_app_seat_ui_register_menu_catalog(qa_ui_menu_catalog catalog);

/* TS options.lobby. */
typedef struct qa_app_seat_ui_lobby { void *ctx; void (*return_to_lobby)(void *ctx); } qa_app_seat_ui_lobby;
/* TS options.rankings. current NULL return is TS null. */
typedef struct qa_app_seat_ui_rankings {
  void *ctx;
  const qa_ui_ranking_account_actions *(*current)(void *ctx);
  bool (*take_menu_request)(void *ctx);
} qa_app_seat_ui_rankings;
/* TS localize(content, text, args) / the nativeHudLocalizer closure's call:
 * malloc'd text. */
typedef char *(*qa_app_seat_ui_localize_fn)(void *ctx, qa_content_id content, const char *text,
                                            const char *const *args, size_t arg_count);


/* TS ApplicationSeatUi constructor arguments. */
typedef struct qa_app_seat_ui_options {
  qa_app_local_input *local;
  const qa_native_ui_art *art;
  qa_app_application_input *input;
  /* TS mutateWindow(operation); NULL applies the mutation at once. */
  void (*mutate_window)(void *ctx, const qa_ui_window_mutation *mutation); void *mutate_ctx;
  const qa_app_sim_presentation_access *simulation; /* player_ui */
  qa_text_font_selection font;
  qa_app_application_audio *audio;
  void (*quit)(void *ctx); void *quit_ctx;
  void (*command)(void *ctx, const char *name, const char *const *args, size_t arg_count); void *command_ctx;
  const qa_app_menu_typography *typography;
  const qa_ui_host_server_settings *host_settings; /* NULL undefined */
  const qa_ui_setting_binding *language;          /* NULL undefined */
  const qa_ui_saved_game_menu_service *saves;     /* NULL undefined */
  const qa_ui_setting_binding *view_setting;      /* NULL undefined */
  qa_llm_settings_service *llm;                   /* NULL undefined */
  bool guest_ui;
  const qa_app_team_arena_result_service *team_arena; /* NULL undefined */
  /* options.* */
  const qa_app_base_arena_menu_service *base_arena;
  const qa_ui_gameplay_settings_source *gameplay;
  const qa_app_seat_ui_lobby *lobby;
  const qa_app_seat_ui_rankings *rankings;
  qa_app_seat_ui_localize_fn native_hud_localizer; void *native_hud_localizer_ctx;
  qa_app_seat_ui_localize_fn localize; void *localize_ctx;
} qa_app_seat_ui_options;

typedef struct qa_app_seat_ui qa_app_seat_ui;
/* Attaches to the input as its seat UI. NULL only on allocation failure. */
qa_app_seat_ui *qa_app_seat_ui_create(const qa_app_seat_ui_options *options);
/* Frees a closed UI (closes it first when still open). */
void qa_app_seat_ui_destroy(qa_app_seat_ui *ui);

qa_native_ui_controller *qa_app_seat_ui_controller(qa_app_seat_ui *ui);
bool qa_app_seat_ui_pause_menu_open(const qa_app_seat_ui *ui);
qa_ui_seat_preferences *qa_app_seat_ui_preferences(qa_app_seat_ui *ui);
qa_seat_hud_messages *qa_app_seat_ui_messages(qa_app_seat_ui *ui);
qa_seat_weapon_wheel *qa_app_seat_ui_weapon_wheel(qa_app_seat_ui *ui);
qa_ui_text_renderer *qa_app_seat_ui_text(qa_app_seat_ui *ui);
qa_app_local_input *qa_app_seat_ui_local(const qa_app_seat_ui *ui);
const qa_native_ui_art *qa_app_seat_ui_art(const qa_app_seat_ui *ui);

void qa_app_seat_ui_refresh_images(qa_app_seat_ui *ui, qa_text_font_selection font, const qa_app_menu_typography *typography);
/* TS prepareImageRefresh(providers): providers is the PreparedApplicationImages;
 * the returned handle's commit runs the closure. */
typedef struct qa_app_seat_ui_image_refresh qa_app_seat_ui_image_refresh;
qa_app_seat_ui_image_refresh *qa_app_seat_ui_prepare_image_refresh(qa_app_seat_ui *ui, qa_app_prepared_images *providers);
void qa_app_seat_ui_image_refresh_commit(qa_app_seat_ui_image_refresh *refresh);
void qa_app_seat_ui_prepare(qa_app_seat_ui *ui, qa_app_application_assets *assets);

/* ApplicationInputUi. */
bool qa_app_seat_ui_input(qa_app_seat_ui *ui, const qa_seat_input_event *event, qa_seat_input_focus focus);
void qa_app_seat_ui_sample(qa_app_seat_ui *ui, qa_seat_input_sample *input);
void qa_app_seat_ui_wheel(qa_app_seat_ui *ui, qa_wheel_mode mode, bool down);
bool qa_app_seat_ui_switch_weapon(qa_app_seat_ui *ui, double first, double second);
bool qa_app_seat_ui_cycle_weapon(qa_app_seat_ui *ui, int direction);
void qa_app_seat_ui_close_menus(qa_app_seat_ui *ui);
void qa_app_seat_ui_clear_prompt(qa_app_seat_ui *ui);

void qa_app_seat_ui_center_print(qa_app_seat_ui *ui, const char *text, double time_milliseconds, double duration_milliseconds);
void qa_app_seat_ui_receive(qa_app_seat_ui *ui, const qa_app_sim_presentation_event *events, size_t count);
/* TS weaponOcclusion: false only on OOM; out is empty for []. */
bool qa_app_seat_ui_weapon_occlusion(qa_app_seat_ui *ui, qa_ui_draw_context context, bool game_visible,
                                     qa_ui_hud_rects *out);

/* TS draw emit/material sinks. */
typedef struct qa_app_seat_ui_sinks {
  void *ctx;
  void (*emit)(void *ctx, const qa_render_command *command);
  void (*material)(void *ctx, const qa_material_text_draw *draw);
} qa_app_seat_ui_sinks;
/* TS draw sourceStatus: kind "vitals" (health, armor) | "native"; NULL undefined. */
typedef struct qa_app_seat_ui_source_status { const char *kind; double health, armor; } qa_app_seat_ui_source_status;
typedef struct qa_app_seat_ui_draw_flags {
  bool game_visible, crosshair_visible, native_status, show_aggregate_warning, native_crosshair;
} qa_app_seat_ui_draw_flags;
/* The TS defaults: visible, crosshair, no native status, warning, nativeCrosshair = nativeStatus. */
qa_app_seat_ui_draw_flags qa_app_seat_ui_default_draw_flags(void);
void qa_app_seat_ui_draw(qa_app_seat_ui *ui, qa_ui_draw_context context, const qa_scene_camera *camera,
                         qa_app_seat_ui_sinks sinks, qa_app_seat_ui_draw_flags flags,
                         const qa_app_seat_ui_source_status *source_status);

/* TS component argument of prepareNativeQ2Hud/drawNativeQ2Hud. */
typedef struct qa_app_seat_ui_q2_component {
  qa_app_q2_native_hud *renderer;
  const char *mode; /* "layout-overlay" | "replace-status" */
  qa_app_q2_hud_assert_fn assert_current; void *assert_ctx;
} qa_app_seat_ui_q2_component;
void qa_app_seat_ui_prepare_native_q2_hud(qa_app_seat_ui *ui, const qa_native_q2_hud_frame *frame, qa_content_id content,
    qa_app_application_assets *assets, qa_ui_draw_context context, const qa_app_seat_ui_q2_component *component);
void qa_app_seat_ui_draw_native_q2_hud(qa_app_seat_ui *ui, const qa_native_q2_hud_frame *frame, qa_ui_draw_context context,
    qa_app_seat_ui_sinks sinks, qa_q2_hud_binding_fn binding, void *binding_ctx, const qa_app_seat_ui_q2_component *component);

/* TS captionCommands: render commands (malloc'd; caller frees the array).
 * Raises "Caption text requested a material draw" as the TS does. */
qa_render_command *qa_app_seat_ui_caption_commands(qa_app_seat_ui *ui, const qa_active_caption *captions,
    size_t caption_count, qa_ui_draw_context context, size_t *out_count);

void qa_app_seat_ui_close(qa_app_seat_ui *ui);
#endif
