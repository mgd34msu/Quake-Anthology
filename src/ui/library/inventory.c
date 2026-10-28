/* Port of src/ui/library/inventory.ts. GPL-2.0-or-later. */
#include "qa/ui/library/inventory.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/support.h"

typedef struct inv_state inv_state;
typedef struct ictx {
  inv_state *state;
  int action;
} ictx;
struct inv_state {
  qa_native_ui_controller *controller;
  qa_ui_inventory_items_fn items;
  void *items_ctx;
  qa_ui_inventory_command_fn command;
  void *command_ctx;
  char *selected; /* NULL = TS null */
  int handle;
  qa_ui_control controls[3];
  ictx ctxs[3];
  qa_ui_list_row *rows;
  const char **cells; /* 2 per row */
  char **counts;      /* String(item.count) per row */
  size_t row_capacity;
};

enum { I_ITEMS, I_USE, I_CLOSE };

static void set_nullable(char **slot, const char *value) {
  free(*slot);
  *slot = NULL;
  if (value != NULL) {
    *slot = qa_input_dup(value);
    if (*slot == NULL) qa_range_error("Out of memory");
  }
}
static void use_item(inv_state *s, const char *id) {
  const char *args[1] = { id };
  s->command(s->command_ctx, "use", args, 1);
}
static void on_activate(void *v, qa_seat_id seat) {
  (void)seat;
  ictx *c = v;
  inv_state *s = c->state;
  if (c->action == I_USE) {
    if (s->selected != NULL) use_item(s, s->selected);
  } else if (c->action == I_CLOSE) {
    qa_native_ui_controller_close_menu(s->controller);
  }
}
static void on_select_row(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  set_nullable(&((ictx *)v)->state->selected, value);
}
static void on_activate_row(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  use_item(((ictx *)v)->state, value);
}
static void on_open_noop(void *v, qa_seat_id seat) {
  (void)v;
  (void)seat;
}
static void on_close(void *v, qa_seat_id seat) {
  (void)seat;
  inv_state *s = v;
  s->command(s->command_ctx, "putaway", NULL, 0);
}

static qa_ui_menu factory(void *v) {
  inv_state *s = v;
  size_t count = 0;
  const qa_ui_hud_inventory_item *items = s->items(s->items_ctx, &count);
  if (items == NULL) count = 0;
  bool found = false;
  for (size_t i = 0; i < count; i++)
    if (s->selected != NULL && strcmp(items[i].id, s->selected) == 0)
      found = true;
  if (!found) {
    const char *fallback = NULL;
    for (size_t i = 0; i < count; i++)
      if (items[i].selected) {
        fallback = items[i].id;
        break;
      }
    if (fallback == NULL && count > 0) fallback = items[0].id;
    set_nullable(&s->selected, fallback);
  }
  if (count > s->row_capacity) {
    qa_ui_list_row *rows = realloc(s->rows, count * sizeof *rows);
    const char **cells = realloc(s->cells, count * 2 * sizeof *cells);
    char **counts = realloc(s->counts, count * sizeof *counts);
    if (rows == NULL || cells == NULL || counts == NULL)
      qa_range_error("Out of memory");
    for (size_t i = s->row_capacity; i < count; i++) counts[i] = NULL;
    s->rows = rows;
    s->cells = cells;
    s->counts = counts;
    s->row_capacity = count;
  }
  for (size_t i = 0; i < count; i++) {
    free(s->counts[i]);
    s->counts[i] = qa_input_js_number(items[i].count);
    if (s->counts[i] == NULL) qa_range_error("Out of memory");
    memset(&s->rows[i], 0, sizeof s->rows[i]);
    s->rows[i].id = items[i].id;
    s->cells[i * 2] = items[i].label;
    s->cells[i * 2 + 1] = s->counts[i];
    s->rows[i].cells = (qa_string_view_list){ &s->cells[i * 2], 2 };
    s->rows[i].image = items[i].icon;
    s->rows[i].enabled = items[i].count > 0;
  }
  for (size_t i = 0; i < 3; i++) {
    memset(&s->controls[i], 0, sizeof s->controls[i]);
    s->ctxs[i].state = s;
    s->controls[i].ctx = &s->ctxs[i];
    s->controls[i].visible = true;
  }
  s->ctxs[0].action = I_ITEMS;
  s->controls[0].id = "ui:inventory:items";
  s->controls[0].kind = "list";
  s->controls[0].label = "Items";
  s->controls[0].rect = (qa_rect){ 64, 96, 512, 264 };
  s->controls[0].has_row_height = true;
  s->controls[0].row_height = 33;
  s->controls[0].rows = s->rows;
  s->controls[0].row_count = count;
  s->controls[0].selected = s->selected;
  s->controls[0].select_row = on_select_row;
  s->controls[0].activate_row = on_activate_row;
  s->controls[0].enabled = true;
  s->ctxs[1].action = I_USE;
  s->controls[1].id = "ui:inventory:use";
  s->controls[1].kind = "button";
  s->controls[1].label = "Use selected item";
  s->controls[1].rect = (qa_rect){ 64, 376, 512, 32 };
  s->controls[1].enabled = s->selected != NULL;
  s->controls[1].activate_seat = on_activate;
  s->ctxs[2].action = I_CLOSE;
  s->controls[2].id = "ui:inventory:close";
  s->controls[2].kind = "button";
  s->controls[2].label = "Close";
  s->controls[2].rect = (qa_rect){ 64, 424, 512, 32 };
  s->controls[2].enabled = true;
  s->controls[2].activate_seat = on_activate;
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = "menu:application:inventory";
  menu.title = "Inventory";
  menu.full_screen = false;
  menu.controls = s->controls;
  menu.control_count = 3;
  menu.ctx = s;
  menu.open = on_open_noop;
  menu.close = on_close;
  return menu;
}

static void inv_dispose(qa_ui_settings_menus *menus) {
  inv_state *s = menus->state;
  if (s == NULL) return;
  qa_native_ui_controller_unregister(s->controller, s->handle);
  for (size_t i = 0; i < s->row_capacity; i++) free(s->counts[i]);
  free(s->counts);
  free(s->rows);
  free(s->cells);
  free(s->selected);
  free(s);
  menus->state = NULL;
}

qa_ui_settings_menus qa_ui_register_inventory_menu(
    qa_native_ui_controller *controller, qa_ui_inventory_items_fn items,
    void *items_ctx, qa_ui_inventory_command_fn command, void *command_ctx) {
  inv_state *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->controller = controller;
  s->items = items;
  s->items_ctx = items_ctx;
  s->command = command;
  s->command_ctx = command_ctx;
  int handle = qa_native_ui_controller_register(
      controller, "menu:application:inventory", factory, s);
  if (handle < 0) qa_range_error("Out of memory");
  s->handle = handle;
  qa_ui_settings_menus menus = { "menu:application:inventory", s, inv_dispose };
  return menus;
}
