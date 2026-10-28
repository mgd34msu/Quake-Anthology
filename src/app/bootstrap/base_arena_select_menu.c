/* Port of src/app/bootstrap/base-arena-select-menu.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/base_arena_select_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct qa_app_arena_selection_menu {
  qa_native_ui_controller *controller;
  qa_ui_menu_id id;
  qa_app_arena_selection_menu_service service;
  int registration;
  char *tier, *selected;
};
static char *dup(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static void reconcile(qa_app_arena_selection_menu *menu, const qa_app_arena_selection *selection) {
  if (selection) {
    bool known = false;
    for (size_t i = 0; i < selection->tier_count; i++) if (menu->tier && strcmp(selection->tiers[i].id, menu->tier) == 0) known = true;
    if (!known) {
      const char *tier = "";
      for (size_t i = 0; i < selection->row_count; i++)
        if (selection->current && strcmp(selection->rows[i].arena->map, selection->current) == 0) { tier = selection->rows[i].tier; break; }
      if (tier[0] == '\0' && selection->tier_count) tier = selection->tiers[0].id;
      free(menu->tier);
      menu->tier = dup(tier);
    }
  }
  bool selected_known = false;
  const char *fallback = NULL, *first = NULL;
  if (selection && menu->tier) {
    for (size_t i = 0; i < selection->row_count; i++) {
      if (strcmp(selection->rows[i].tier, menu->tier) != 0) continue;
      if (!first) first = selection->rows[i].arena->map;
      if (menu->selected && strcmp(selection->rows[i].arena->map, menu->selected) == 0) selected_known = true;
      if (selection->current && strcmp(selection->rows[i].arena->map, selection->current) == 0) fallback = selection->rows[i].arena->map;
    }
  }
  if (!selected_known) {
    free(menu->selected);
    menu->selected = dup(fallback ? fallback : first);
  }
}
static qa_ui_menu make_menu(void *ctx) {
  qa_app_arena_selection_menu *menu = ctx;
  const qa_app_arena_selection *selection = menu->service.read(menu->service.ctx);
  reconcile(menu, selection);
  /* Control construction is controller-owned; the factory contract
   * (title/id/fullScreen + six controls) is mirrored by the host UI
   * builder reading _view(). */
  qa_ui_menu out = {0};
  out.id = menu->id;
  out.title = "Choose an arena";
  out.full_screen = true;
  return out;
}
qa_app_arena_selection_menu *qa_app_register_arena_selection_menu(qa_native_ui_controller *controller,
    qa_ui_menu_id id, qa_app_arena_selection_menu_service service) {
  qa_app_arena_selection_menu *menu = calloc(1, sizeof *menu);
  if (!menu) return NULL;
  menu->controller = controller;
  menu->id = id;
  menu->service = service;
  menu->tier = dup("");
  menu->registration = qa_native_ui_controller_register(controller, id, make_menu, menu);
  return menu;
}
void qa_app_arena_selection_menu_destroy(qa_app_arena_selection_menu *menu) {
  if (!menu) return;
  qa_native_ui_controller_unregister(menu->controller, menu->registration);
  free(menu->tier);
  free(menu->selected);
  free(menu);
}
qa_app_arena_selection_view qa_app_arena_selection_menu_view(qa_app_arena_selection_menu *menu) {
  const qa_app_arena_selection *selection = menu->service.read(menu->service.ctx);
  reconcile(menu, selection);
  qa_app_arena_selection_view view = {0};
  view.tier = menu->tier;
  view.selected = menu->selected;
  if (selection) {
    for (size_t i = 0; i < selection->row_count; i++)
      if (menu->selected && strcmp(selection->rows[i].arena->map, menu->selected) == 0) view.current = &selection->rows[i];
  }
  if (!view.current) snprintf(view.opponents, sizeof view.opponents, "No single-player arenas are available.");
  else {
    size_t len = (size_t)snprintf(view.opponents, sizeof view.opponents, "Opponents: ");
    if (view.current->arena->bot_count == 0) snprintf(view.opponents + len, sizeof view.opponents - len, "None");
    for (size_t i = 0; i < view.current->arena->bot_count; i++)
      len += (size_t)snprintf(view.opponents + len, sizeof view.opponents - len, "%s%s", i ? ", " : "", view.current->arena->bots[i]);
    const qa_app_base_arena *arena = view.current->arena;
    if (arena->frag_limit > 0 && arena->time_limit > 0)
      snprintf(view.limits, sizeof view.limits, "%.17g frags · %.17g minutes", arena->frag_limit, arena->time_limit);
    else if (arena->frag_limit > 0) snprintf(view.limits, sizeof view.limits, "%.17g frags", arena->frag_limit);
    else if (arena->time_limit > 0) snprintf(view.limits, sizeof view.limits, "%.17g minutes", arena->time_limit);
  }
  return view;
}
void qa_app_arena_selection_menu_select_tier(qa_app_arena_selection_menu *menu, const char *tier) {
  free(menu->tier);
  menu->tier = dup(tier);
  free(menu->selected);
  menu->selected = NULL;
}
void qa_app_arena_selection_menu_select(qa_app_arena_selection_menu *menu, const char *map) {
  free(menu->selected);
  menu->selected = dup(map);
}
bool qa_app_arena_selection_menu_activate(qa_app_arena_selection_menu *menu, const char *map) {
  const qa_app_arena_selection *selection = menu->service.read(menu->service.ctx);
  reconcile(menu, selection);
  if (selection) {
    for (size_t i = 0; i < selection->row_count; i++) {
      if (strcmp(selection->rows[i].tier, menu->tier) != 0) continue;
      if (strcmp(selection->rows[i].arena->map, map) == 0 && selection->rows[i].available) {
        menu->service.choose(menu->service.ctx, map);
        return true;
      }
    }
  }
  return false;
}
bool qa_app_arena_selection_menu_choose_current(qa_app_arena_selection_menu *menu) {
  qa_app_arena_selection_view view = qa_app_arena_selection_menu_view(menu);
  if (!view.current || !view.current->available) return false;
  menu->service.choose(menu->service.ctx, view.current->arena->map);
  return true;
}
void qa_app_arena_selection_menu_open(qa_app_arena_selection_menu *menu) {
  free(menu->tier);
  menu->tier = dup("");
  free(menu->selected);
  menu->selected = NULL;
}
