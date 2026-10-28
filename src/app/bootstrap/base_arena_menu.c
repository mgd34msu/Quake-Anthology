/* Port of src/app/bootstrap/base-arena-menu.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/base_arena_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/base_arena_select_menu.h"
const char qa_app_arena_progress_root[] = "menu:application:arena-progress";
static const struct { double skill; const char *label; } difficulties[] = {
    {1, "I Can Win"}, {2, "Bring It On"}, {3, "Hurt Me Plenty"}, {4, "Hardcore"}, {5, "Nightmare"}};
const char *qa_app_arena_difficulty_label(double skill) {
  for (size_t i = 0; i < 5; i++) if (difficulties[i].skill == skill) return difficulties[i].label;
  return "";
}
struct qa_app_base_arena_menus {
  qa_native_ui_controller *controller;
  qa_app_base_arena_menu_service service;
  qa_app_arena_selection_menu *selection_menu;
  int registrations[4];
  char *shown;
  char *selected_map;
  bool closed;
};
static char *dup(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static qa_ui_menu_id menu_id(const char *text) { return text; }
static const qa_app_arena_selection *select_read(void *ctx) {
  qa_app_base_arena_menus *menus = ctx;
  return menus->service.selection(menus->service.ctx);
}
static void select_choose(void *ctx, const char *map) {
  qa_app_base_arena_menus *menus = ctx;
  free(menus->selected_map);
  menus->selected_map = dup(map);
  qa_native_ui_controller_open_menu(menus->controller, menu_id("menu:application:arena-skill"));
}
static qa_ui_menu difficulty_menu(void *ctx) {
  qa_app_base_arena_menus *menus = ctx;
  (void)menus;
  qa_ui_menu out = {0};
  out.id = menu_id("menu:application:arena-skill");
  out.title = "Difficulty";
  out.full_screen = true;
  return out;
}
static qa_ui_menu progress_menu(void *ctx) {
  (void)ctx;
  qa_ui_menu out = {0};
  out.id = menu_id(qa_app_arena_progress_root);
  out.title = "Arena progress";
  out.full_screen = true;
  return out;
}
static qa_ui_menu reset_menu(void *ctx) {
  (void)ctx;
  qa_ui_menu out = {0};
  out.id = menu_id("menu:application:arena-reset");
  out.title = "Reset arena progress and awards?";
  out.full_screen = true;
  return out;
}
static qa_ui_menu result_menu(void *ctx) {
  qa_app_base_arena_menus *menus = ctx;
  const qa_app_arena_postgame_presentation *result = NULL;
  bool has = menus->service.result(menus->service.ctx, &result);
  qa_ui_menu out = {0};
  out.id = menu_id("menu:application:arena-result");
  out.title = has && result->result.rank == 1 ? "Victory" : "Match complete";
  out.full_screen = true;
  return out;
}
qa_app_base_arena_menus *qa_app_base_arena_menus_create(qa_native_ui_controller *controller, qa_app_base_arena_menu_service service) {
  qa_app_base_arena_menus *menus = calloc(1, sizeof *menus);
  if (!menus) return NULL;
  menus->controller = controller;
  menus->service = service;
  qa_app_arena_selection_menu_service select_service = {menus, select_read, select_choose};
  menus->selection_menu = qa_app_register_arena_selection_menu(controller, menu_id("menu:application:arena-selection"), select_service);
  menus->registrations[0] = qa_native_ui_controller_register(controller, menu_id("menu:application:arena-skill"), difficulty_menu, menus);
  menus->registrations[1] = qa_native_ui_controller_register(controller, menu_id(qa_app_arena_progress_root), progress_menu, menus);
  menus->registrations[2] = qa_native_ui_controller_register(controller, menu_id("menu:application:arena-reset"), reset_menu, menus);
  menus->registrations[3] = qa_native_ui_controller_register(controller, menu_id("menu:application:arena-result"), result_menu, menus);
  return menus;
}
void qa_app_base_arena_menus_destroy(qa_app_base_arena_menus *menus) {
  if (!menus) return;
  qa_app_base_arena_menus_close(menus);
  free(menus->shown);
  free(menus->selected_map);
  free(menus);
}
const char *qa_app_base_arena_menus_root(const qa_app_base_arena_menus *menus) {
  (void)menus;
  return qa_app_arena_progress_root;
}
void qa_app_base_arena_menus_update(qa_app_base_arena_menus *menus) {
  const qa_app_arena_postgame_presentation *result = NULL;
  if (!menus->service.result(menus->service.ctx, &result)) {
    free(menus->shown);
    menus->shown = NULL;
    return;
  }
  /* TS identity = JSON.stringify([result.result, result.podium]). */
  char identity[4096];
  size_t len = (size_t)snprintf(identity, sizeof identity, "[{\"rank\":%.17g,\"completedTier\":%.17g,\"unlockedMovie\":%s,\"nextLevel\":%.17g,\"awards\":[",
      result->result.rank, result->result.completed_tier,
      result->result.unlocked_movie < 0 ? "null" : "?", result->result.next_level);
  if (result->result.unlocked_movie >= 0) {
    /* Rebuild the movie field text exactly once. */
    char movie[40];
    snprintf(movie, sizeof movie, "%.17g", result->result.unlocked_movie);
    char *marker = strstr(identity, "\"unlockedMovie\":?");
    if (marker) {
      char tail[4096];
      snprintf(tail, sizeof tail, "%s", marker + 17);
      snprintf(marker, sizeof identity - (size_t)(marker - identity), "\"unlockedMovie\":%s", movie);
      strncat(identity, tail, sizeof identity - strlen(identity) - 1);
      len = strlen(identity);
    }
  }
  for (size_t i = 0; i < result->result.award_count; i++)
    len += (size_t)snprintf(identity + len, sizeof identity - len, "%s{\"medal\":%.17g,\"amount\":%.17g}",
        i ? "," : "", result->result.awards[i].medal, result->result.awards[i].amount);
  len += (size_t)snprintf(identity + len, sizeof identity - len, "]},[");
  for (size_t i = 0; i < result->podium_count; i++)
    len += (size_t)snprintf(identity + len, sizeof identity - len, "%s{\"client\":%.17g,\"rank\":%.17g,\"score\":%.17g}",
        i ? "," : "", result->podium[i].client, result->podium[i].rank, result->podium[i].score);
  snprintf(identity + len, sizeof identity - len, "]]");
  if (menus->shown && strcmp(menus->shown, identity) == 0) return;
  free(menus->shown);
  menus->shown = dup(identity);
  qa_native_ui_controller_close_all(menus->controller);
  qa_native_ui_controller_open_menu(menus->controller, menu_id("menu:application:arena-result"));
}
void qa_app_base_arena_menus_close(qa_app_base_arena_menus *menus) {
  if (!menus || menus->closed) return;
  menus->closed = true;
  qa_app_arena_selection_menu_destroy(menus->selection_menu);
  menus->selection_menu = NULL;
  for (size_t i = 0; i < 4; i++) qa_native_ui_controller_unregister(menus->controller, menus->registrations[i]);
}
