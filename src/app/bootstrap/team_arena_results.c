/* Port of src/app/bootstrap/team-arena-results.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/team_arena_results.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/ui/common/layout.h"
typedef struct actx { qa_app_team_arena_results *results; int action; } actx;
struct qa_app_team_arena_results {
  qa_native_ui_controller *controller;
  qa_app_team_arena_result_service service;
  int handle;
  bool active;
  bool closed;
  qa_ui_control controls[6];
  char *labels[6];
  char *ids[6];
  char *title;
  actx actx_storage[6];
};

enum { ACT_NONE, ACT_NEXT, ACT_RETRY, ACT_DEMO, ACT_QUIT };
static void activate_button(void *ctx, qa_seat_id seat) {
  (void)seat;
  actx *a = ctx;
  qa_app_team_arena_result_service *service = &a->results->service;
  switch (a->action) {
    case ACT_NEXT: service->next(service->ctx); break;
    case ACT_RETRY: service->retry(service->ctx); break;
    case ACT_DEMO: if (service->has_demo) service->demo_play(service->ctx); break;
    case ACT_QUIT: service->quit(service->ctx); break;
    default: break;
  }
}
static void menu_noop(void *ctx, qa_seat_id seat) { (void)ctx; (void)seat; }
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
static char *num(double v) { char *r = qa_input_js_number(v); if (!r) qa_range_error("Out of memory"); return r; }
static qa_ui_menu results_factory(void *ctx) {
  qa_app_team_arena_results *r = ctx;
  qa_app_team_arena_result result; memset(&result, 0, sizeof result);
  bool has = r->service.read(r->service.ctx, &result);
  for (int i = 0; i < 6; i++) { free(r->labels[i]); free(r->ids[i]); r->labels[i] = NULL; r->ids[i] = NULL; }
  free(r->title); r->title = NULL;
  char buf[1024];
  if (!has) r->title = xdup("Match complete");
  else {
    char *score = num(result.score), *opp = num(result.opponent);
    snprintf(buf, sizeof buf, "%s: %s (%s - %s)", result.won ? "Victory" : "Defeat", result.title, score, opp);
    free(score); free(opp);
    r->title = xdup(buf);
  }
  size_t n = 0;
  /* score row */
  {
    char *points = has ? num(result.points) : NULL;
    snprintf(buf, sizeof buf, "Score %s \xc2\xb7 Time %s", points ? points : "", has ? result.time : "");
    free(points);
    r->labels[n] = xdup(has ? buf : "");
    r->ids[n] = xdup("ui:team-arena:score");
    r->actx_storage[n] = (actx){r, ACT_NONE};
    r->controls[n] = (qa_ui_control){0};
    r->controls[n].id = r->ids[n]; r->controls[n].kind = "button"; r->controls[n].label = r->labels[n];
    r->controls[n].rect = qa_ui_menu_row_default(0); r->controls[n].visible = true; r->controls[n].enabled = false;
    r->controls[n].ctx = &r->actx_storage[n]; r->controls[n].activate_seat = activate_button;
    n++;
  }
  const struct { const char *id, *label; double row; int action; } buttons[4] = {
      {"next", "Next match", 2, ACT_NEXT}, {"retry", "Retry match", 3, ACT_RETRY},
      {"demo", "Watch demo", 4, ACT_DEMO}, {"main-menu", "Main menu", 5, ACT_QUIT}};
  for (int i = 0; i < 4; i++) {
    if (buttons[i].action == ACT_DEMO && !(r->service.has_demo && r->service.demo_available(r->service.ctx))) continue;
    snprintf(buf, sizeof buf, "ui:team-arena:%s", buttons[i].id);
    r->ids[n] = xdup(buf);
    r->labels[n] = xdup(buttons[i].label);
    r->actx_storage[n] = (actx){r, buttons[i].action};
    r->controls[n] = (qa_ui_control){0};
    r->controls[n].id = r->ids[n]; r->controls[n].kind = "button"; r->controls[n].label = r->labels[n];
    r->controls[n].rect = qa_ui_menu_row_default(buttons[i].row); r->controls[n].visible = true; r->controls[n].enabled = has;
    r->controls[n].ctx = &r->actx_storage[n]; r->controls[n].activate_seat = activate_button;
    n++;
  }
  qa_ui_menu menu; memset(&menu, 0, sizeof menu);
  menu.id = "menu:application:team-arena-results";
  menu.title = r->title;
  menu.full_screen = true;
  menu.controls = r->controls; menu.control_count = n;
  menu.ctx = r; menu.open = menu_noop; menu.close = menu_noop;
  return menu;
}
qa_app_team_arena_results *qa_app_team_arena_results_create(qa_native_ui_controller *controller, qa_app_team_arena_result_service service) {
  qa_app_team_arena_results *r = calloc(1, sizeof *r);
  if (!r) return NULL;
  r->controller = controller; r->service = service;
  r->handle = qa_native_ui_controller_register(controller, "menu:application:team-arena-results", results_factory, r);
  if (r->handle < 0) { free(r); return NULL; }
  return r;
}
void qa_app_team_arena_results_destroy(qa_app_team_arena_results *r) {
  if (!r) return;
  qa_app_team_arena_results_close(r);
  for (int i = 0; i < 6; i++) { free(r->labels[i]); free(r->ids[i]); }
  free(r->title); free(r);
}
bool qa_app_team_arena_results_active(const qa_app_team_arena_results *r) { return r->active; }
void qa_app_team_arena_results_update(qa_app_team_arena_results *r) {
  qa_app_team_arena_result result;
  if (!r->service.read(r->service.ctx, &result)) return;
  if (!r->active) { r->active = true; qa_native_ui_controller_close_all(r->controller); }
  if (qa_native_ui_controller_active_menu(r->controller) == NULL)
    qa_native_ui_controller_open_menu(r->controller, "menu:application:team-arena-results");
}
void qa_app_team_arena_results_close(qa_app_team_arena_results *r) {
  if (r->closed) return;
  r->closed = true;
  qa_native_ui_controller_unregister(r->controller, r->handle);
}
