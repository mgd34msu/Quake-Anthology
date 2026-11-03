/* Port of src/app/bootstrap/player-death.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/player_death.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/key_codes.h"
#include "qa/ui/common/layout.h"
const qa_ui_menu_id qa_app_player_death_menu = "menu:application:death";
struct qa_app_seat_player_death {
  qa_native_ui_controller *controller;
  const qa_ui_saved_game_menu_service *saves;
  qa_ui_menu_id load_menu;
  void (*quit)(void *ctx);
  void *quit_ctx;
  bool active;
  bool busy;
  bool closed; /* closed while a restart is pending */
  char *message;
  int handle;
  qa_ui_control controls[4];
};
static char *dup_text(const char *text) {
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
/* TS restart(): try { await saves.recovery.restart(); } catch (error) { message = error.message } finally { busy = false }.
 * The application settles the queued restart in its step; a death prompt closed before then is freed at the settle. */
static void restart_settled(void *ctx, const char *error) {
  qa_app_seat_player_death *death = ctx;
  if (error != NULL) { free(death->message); death->message = dup_text(error); }
  death->busy = false;
  if (death->closed) { free(death->message); free(death); }
}
static void restart(qa_app_seat_player_death *death) {
  if (death->busy || death->saves == NULL || !death->saves->has_recovery) return;
  death->busy = true;
  free(death->message); death->message = dup_text("Restarting level...");
  qa_ui_saves_settle settle = { restart_settled, death };
  death->saves->recovery_restart(death->saves->ctx, settle);
}
static void activate_load(void *ctx, qa_seat_id seat) {
  (void)seat;
  qa_app_seat_player_death *death = ctx;
  qa_native_ui_controller_open_menu(death->controller, death->load_menu);
}
static void activate_restart(void *ctx, qa_seat_id seat) {
  (void)seat;
  restart(ctx);
}
static void activate_quit(void *ctx, qa_seat_id seat) {
  (void)seat;
  qa_app_seat_player_death *death = ctx;
  death->quit(death->quit_ctx);
}
static void activate_noop(void *ctx, qa_seat_id seat) { (void)ctx; (void)seat; }
static qa_ui_control button(qa_app_seat_player_death *death, const char *id, const char *label,
    double row, void (*activate)(void *, qa_seat_id), bool enabled) {
  qa_ui_control control = {0};
  control.id = id;
  control.label = label;
  control.kind = "button";
  qa_ui_menu_row_options none = {false, false, false, false, 0, 0, 0, 0};
  control.rect = qa_ui_menu_row(row, none);
  control.visible = true;
  control.enabled = enabled && !death->busy;
  control.ctx = death;
  control.activate_seat = activate;
  return control;
}
static void menu_open(void *ctx, qa_seat_id seat) { (void)ctx; (void)seat; }
static qa_ui_menu factory(void *v) {
  qa_app_seat_player_death *death = v;
  bool load_available = death->saves != NULL && death->saves->unavailable != NULL
      && death->saves->unavailable(death->saves->ctx, "load") == NULL;
  size_t count = 0;
  death->controls[count++] = button(death, "ui:death:load", "Load saved game", 2, activate_load, load_available);
  death->controls[count++] = button(death, "ui:death:restart", "Restart level", 3, activate_restart, true);
  death->controls[count++] = button(death, "ui:death:quit", "End game", 6, activate_quit, true);
  if (death->message[0] != '\0')
    death->controls[count++] = button(death, "ui:death:message", death->message, 8, activate_noop, false);
  qa_ui_menu menu = {0};
  menu.id = qa_app_player_death_menu;
  menu.title = "You died";
  menu.full_screen = true;
  menu.controls = death->controls;
  menu.control_count = count;
  menu.ctx = death;
  menu.open = menu_open;
  menu.close = menu_open;
  return menu;
}
qa_app_seat_player_death *qa_app_seat_player_death_create(qa_native_ui_controller *controller,
    const qa_ui_saved_game_menu_service *saves, qa_ui_menu_id load_menu,
    void (*quit)(void *ctx), void *quit_ctx) {
  qa_app_seat_player_death *death = calloc(1, sizeof *death);
  if (death == NULL) qa_range_error("Out of memory");
  death->controller = controller;
  death->saves = saves;
  death->load_menu = load_menu;
  death->quit = quit;
  death->quit_ctx = quit_ctx;
  death->message = dup_text("");
  death->handle = qa_native_ui_controller_register(controller, qa_app_player_death_menu, factory, death);
  if (death->handle < 0) qa_range_error("Out of memory");
  return death;
}
bool qa_app_seat_player_death_active(const qa_app_seat_player_death *death) { return death->active; }
void qa_app_seat_player_death_observe(qa_app_seat_player_death *death, double health) {
  if (death->saves == NULL || !death->saves->has_recovery) return;
  if (health > 0) {
    if (death->active) qa_native_ui_controller_close_all(death->controller);
    death->active = false;
    return;
  }
  if (!death->active) { qa_native_ui_controller_close_all(death->controller); death->active = true; }
  if (qa_native_ui_controller_active_menu(death->controller) == NULL)
    qa_native_ui_controller_open_menu(death->controller, qa_app_player_death_menu);
}
bool qa_app_seat_player_death_input(qa_app_seat_player_death *death, qa_seat_input_event event) {
  qa_ui_menu_id active = qa_native_ui_controller_active_menu(death->controller);
  if (!death->active || active == NULL || strcmp(active, qa_app_player_death_menu) != 0) return false;
  return (strcmp(event.kind, "key") == 0 && event.code == QA_KEY_ESCAPE)
      || (strcmp(event.kind, "controller-button") == 0 && (event.button == 1 || event.button == 6))
      || (strcmp(event.kind, "mouse-button") == 0 && event.button == 3);
}
void qa_app_seat_player_death_close(qa_app_seat_player_death *death) {
  if (death == NULL) return;
  qa_native_ui_controller_unregister(death->controller, death->handle);
  if (death->busy) { death->closed = true; return; }
  free(death->message);
  free(death);
}
