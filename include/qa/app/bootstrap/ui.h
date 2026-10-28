/* Port of src/app/bootstrap/ui.ts. SeatUi menus/settings bindings and
 * frame building (ui library + sibling bootstrap slices) are host
 * fn+ctx; UI lifetime, controller wiring and close ordering are kept
 * here. Async -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_UI_H
#define QA_APP_BOOTSTRAP_UI_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/render.h"
#include "qa/core/cvars/index.h"
#include "qa/input/seat.h"
#include "qa/platform/sdl.h"
#include "qa/ui/common/controller.h"
/* TS ApplicationUiOptions: dependency graph stays opaque in host. */
typedef struct qa_app_application_ui_host {
  void *ctx;
  qa_seat_input *(*seat_input)(void *ctx);
  qa_cvar_registry *(*seat_cvars)(void *ctx);
  bool (*q3_seat)(void *ctx);
  const char *(*create_controller)(void *ctx, qa_native_ui_controller **out);
  const char *(*register_settings_menus)(void *ctx);
  const char *(*adopt_settings)(void *ctx, const char *id, const char *title, qa_native_ui_controller *controller, const void **out_settings);
  const char *(*adopt_pause_menu)(void *ctx, const void **out_pause_menu);
  const char *(*adopt_in_game_menu)(void *ctx, const void **out_menu);
  bool (*run_open)(void *ctx);
  void (*run_pump)(void *ctx);
  void (*run_close)(void *ctx);
  void (*controller_command)(void *ctx, const char *name, const char *const *args, size_t count);
  bool (*seat_ui_event)(void *ctx, const void *event);
  void (*seat_text_set_color)(void *ctx, qa_vec4 color);
  void *(*fonts_prepare_frame)(void *ctx, qa_render_frame frame);
  void (*capture_frame)(void *ctx, const void *request);
  void (*open_presentation_font)(void *ctx);
  void (*fonts_open)(void *ctx);
  void (*fonts_close)(void *ctx);
  void (*graphics_history_reset_all)(void *ctx);
  void (*seat_ui_close)(void *ctx);
  void (*destroy_controller)(void *ctx, qa_native_ui_controller *controller);
} qa_app_application_ui_host;
typedef struct qa_app_application_ui qa_app_application_ui;
qa_app_application_ui *qa_app_application_ui_create(qa_app_application_ui_host host);
void qa_app_application_ui_destroy(qa_app_application_ui *ui);
bool qa_app_application_ui_has_settings(const qa_app_application_ui *ui);
bool qa_app_application_ui_captures_keyboard(const qa_app_application_ui *ui);
qa_native_ui_controller *qa_app_application_ui_controller(qa_app_application_ui *ui);
void *qa_app_application_ui_wheel(const qa_app_application_ui *ui);
void *qa_app_application_ui_frontend(const qa_app_application_ui *ui);
const void *qa_app_application_ui_settings(const qa_app_application_ui *ui);
const void *qa_app_application_ui_pause_menu(const qa_app_application_ui *ui);
const void *qa_app_application_ui_in_game_menu(const qa_app_application_ui *ui);
bool qa_app_application_ui_open(qa_app_application_ui *ui);
void qa_app_application_ui_pump(qa_app_application_ui *ui);
void qa_app_application_ui_command(qa_app_application_ui *ui, const char *name, const char *const *args, size_t count);
bool qa_app_application_ui_handle(qa_app_application_ui *ui, const qa_sdl_event *event);
void qa_app_application_ui_set_color(qa_app_application_ui *ui, qa_vec4 color);
void *qa_app_application_ui_prepare_frame(qa_app_application_ui *ui, qa_render_frame frame);
void qa_app_application_ui_capture_frame(qa_app_application_ui *ui, const void *request, const void *options);
void qa_app_application_ui_publish_menu_typography(qa_app_application_ui *ui, const void *menu);
void qa_app_application_ui_reset_graphics_history(qa_app_application_ui *ui);
void qa_app_application_ui_close(qa_app_application_ui *ui);
#endif
