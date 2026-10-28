/* Port of src/app/bootstrap/startup-menu.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup_menu.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_startup_menu {
  qa_app_startup_menu_host host;
  bool in_game, closed, failed;
};
qa_app_startup_menu *qa_app_startup_menu_create(qa_app_startup_menu_host host) {
  qa_app_startup_menu *m = calloc(1, sizeof *m);
  if (!m) return NULL;
  m->host = host;
  const char *error = host.build_menus(host.ctx);
  if (error != NULL) { free(m); qa_range_error("%s", error); }
  host.prepare_saves(host.ctx);
  return m;
}
void qa_app_startup_menu_destroy(qa_app_startup_menu *m) {
  if (!m) return;
  qa_app_startup_menu_close(m);
  free(m);
}
void qa_app_startup_menu_graphic_navigation(qa_app_startup_menu *m, const qa_menu_key_event *event) {
  /* TS graphicNavigation routes MenuKeyEvent through the controller. */
  m->host.graphic_navigation(m->host.ctx, event);
}
bool qa_app_startup_menu_handle(qa_app_startup_menu *m, const qa_sdl_event *event) {
  if (m->closed) return false;
  return m->host.handle_event(m->host.ctx, event);
}
void qa_app_startup_menu_command(qa_app_startup_menu *m, const char *command) {
  m->host.record_command(m->host.ctx, command);
}
void qa_app_startup_menu_set_display_mode(qa_app_startup_menu *m, bool hidden) {
  m->host.set_display_mode(m->host.ctx, hidden);
}
bool qa_app_startup_menu_open(qa_app_startup_menu *m) {
  if (m->closed) qa_range_error("Startup menu is closed");
  return m->host.open(m->host.ctx);
}
void qa_app_startup_menu_pump(qa_app_startup_menu *m) {
  if (m->closed) qa_range_error("Startup menu is closed");
  m->host.pump(m->host.ctx);
}
void qa_app_startup_menu_prepare_frame(qa_app_startup_menu *m, qa_render_frame frame) {
  (void)m; (void)frame;
}
void qa_app_startup_menu_navigate(qa_app_startup_menu *m, const char *target) {
  m->host.navigate(m->host.ctx, target);
}
void qa_app_startup_menu_close(qa_app_startup_menu *m) {
  if (m->closed) return;
  m->closed = true;
  m->host.close_saves(m->host.ctx);
  m->host.close_menu(m->host.ctx);
  m->host.close_services(m->host.ctx);
}
