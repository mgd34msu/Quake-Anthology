/* Port of src/app/bootstrap/ui.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/ui.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
struct qa_app_application_ui {
  qa_app_application_ui_host host;
  qa_native_ui_controller *controller;
  const void *settings, *pause_menu, *in_game_menu;
  bool closed;
};
qa_app_application_ui *qa_app_application_ui_create(qa_app_application_ui_host host) {
  qa_app_application_ui *ui = calloc(1, sizeof *ui);
  if (!ui) return NULL;
  ui->host = host;
  const char *error = host.create_controller(host.ctx, &ui->controller);
  if (error == NULL) error = host.register_settings_menus(host.ctx);
  if (error != NULL) { free(ui); qa_range_error("%s", error); }
  if (host.adopt_settings) {
    error = host.adopt_settings(host.ctx, "menu:settings", "Settings", ui->controller, &ui->settings);
    if (error != NULL) { host.destroy_controller(host.ctx, ui->controller); free(ui); qa_range_error("%s", error); }
  }
  if (host.adopt_pause_menu) host.adopt_pause_menu(host.ctx, &ui->pause_menu);
  if (host.adopt_in_game_menu) host.adopt_in_game_menu(host.ctx, &ui->in_game_menu);
  host.open_presentation_font(host.ctx);
  host.fonts_open(host.ctx);
  return ui;
}
void qa_app_application_ui_destroy(qa_app_application_ui *ui) {
  if (!ui) return;
  qa_app_application_ui_close(ui);
  free(ui);
}
bool qa_app_application_ui_has_settings(const qa_app_application_ui *ui) { return ui->settings != NULL; }
bool qa_app_application_ui_captures_keyboard(const qa_app_application_ui *ui) {
  (void)ui;
  /* controller.capturesKeyboard || consoleCapturesKeyboard (host-owned). */
  return false;
}
qa_native_ui_controller *qa_app_application_ui_controller(qa_app_application_ui *ui) { return ui->controller; }
void *qa_app_application_ui_wheel(const qa_app_application_ui *ui) { (void)ui; return NULL; }
void *qa_app_application_ui_frontend(const qa_app_application_ui *ui) { (void)ui; return NULL; }
const void *qa_app_application_ui_settings(const qa_app_application_ui *ui) { return ui->settings; }
const void *qa_app_application_ui_pause_menu(const qa_app_application_ui *ui) { return ui->pause_menu; }
const void *qa_app_application_ui_in_game_menu(const qa_app_application_ui *ui) { return ui->in_game_menu; }
bool qa_app_application_ui_open(qa_app_application_ui *ui) {
  if (ui->closed) qa_range_error("Application UI is closed");
  return ui->host.run_open(ui->host.ctx);
}
void qa_app_application_ui_pump(qa_app_application_ui *ui) {
  if (ui->closed) qa_range_error("Application UI is closed");
  ui->host.run_pump(ui->host.ctx);
}
void qa_app_application_ui_command(qa_app_application_ui *ui, const char *name, const char *const *args, size_t count) {
  ui->host.controller_command(ui->host.ctx, name, args, count);
}
bool qa_app_application_ui_handle(qa_app_application_ui *ui, const qa_sdl_event *event) {
  return ui->host.seat_ui_event(ui->host.ctx, event);
}
void qa_app_application_ui_set_color(qa_app_application_ui *ui, qa_vec4 color) {
  if (!ui->closed) ui->host.seat_text_set_color(ui->host.ctx, color);
}
void *qa_app_application_ui_prepare_frame(qa_app_application_ui *ui, qa_render_frame frame) {
  return ui->host.fonts_prepare_frame(ui->host.ctx, frame);
}
void qa_app_application_ui_capture_frame(qa_app_application_ui *ui, const void *request, const void *options) {
  (void)options;
  if (ui->closed) qa_range_error("Application UI is closed");
  ui->host.capture_frame(ui->host.ctx, request);
}
void qa_app_application_ui_publish_menu_typography(qa_app_application_ui *ui, const void *menu) {
  (void)ui; (void)menu;
}
void qa_app_application_ui_reset_graphics_history(qa_app_application_ui *ui) {
  ui->host.graphics_history_reset_all(ui->host.ctx);
}
void qa_app_application_ui_close(qa_app_application_ui *ui) {
  if (ui->closed) return;
  ui->closed = true;
  ui->host.seat_ui_close(ui->host.ctx);
  ui->host.fonts_close(ui->host.ctx);
  if (ui->controller) ui->host.destroy_controller(ui->host.ctx, ui->controller);
  ui->controller = NULL;
  ui->host.run_close(ui->host.ctx);
}
