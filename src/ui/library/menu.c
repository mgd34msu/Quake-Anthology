/* Port of src/ui/library/menu.ts. GPL-2.0-or-later. */
#include "qa/ui/library/menu.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/foundation/js_case.h"

typedef struct lib_state lib_state;
typedef struct lctx {
  lib_state *state;
  int action;
} lctx;

typedef struct lib_view {
  char *scope;
  char *query;
  char *selected; /* NULL = TS null */
} lib_view;

struct lib_state {
  qa_native_ui_controller *controller;
  const qa_ui_library_menu_service *service;
  char *id;
  char *title;
  char *selected; /* NULL = TS null */
  char *query;
  char *name;
  char *scope;
  lib_view *views;
  size_t view_count, view_capacity;
  int handle;
  qa_ui_control *controls;
  lctx *ctxs;
  size_t control_capacity;
  qa_ui_list_row *rows;
  const char **cells; /* 2 per row */
  size_t row_capacity;
  char **pool;
  size_t pool_count, pool_capacity;
};

enum { L_NONE, L_SEARCH, L_ENTRIES, L_OPEN, L_REFRESH, L_STOP, L_NAME,
       L_CREATE, L_BACK };

static void set_str(char **slot, const char *value) {
  free(*slot);
  *slot = qa_input_dup(value);
  if (*slot == NULL) qa_range_error("Out of memory");
}
static void set_nullable(char **slot, const char *value) {
  free(*slot);
  *slot = NULL;
  if (value != NULL) set_str(slot, value);
}
static void clear_pool(lib_state *s) {
  for (size_t i = 0; i < s->pool_count; i++) free(s->pool[i]);
  s->pool_count = 0;
}
static bool blank_trim(const char *text) {
  char *trimmed = qa_input_js_trim(text);
  if (trimmed == NULL) qa_range_error("Out of memory");
  bool empty = trimmed[0] == '\0';
  free(trimmed);
  return empty;
}
/* TS text.toLowerCase() (Unicode). */
static char *lower_dup(const char *text) {
  char *copy = qa_js_to_lower_case(text);
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}
static const char *service_scope(lib_state *s) {
  if (s->service->scope == NULL) return "";
  const char *scope = s->service->scope(s->service->ctx);
  return scope != NULL ? scope : "";
}

static void activate_entry(lib_state *s, const char *value) {
  size_t count = 0;
  const qa_ui_library_entry *entries = s->service->entries(s->service->ctx, &count);
  for (size_t i = 0; i < count; i++)
    if (strcmp(entries[i].id, value) == 0) {
      if (entries[i].unavailable == NULL) s->service->activate(s->service->ctx, value);
      return;
    }
}

static void on_activate(void *v, qa_seat_id seat) {
  (void)seat;
  lctx *c = v;
  lib_state *s = c->state;
  switch (c->action) {
    case L_OPEN:
      if (s->selected != NULL) activate_entry(s, s->selected);
      break;
    case L_REFRESH: s->service->refresh(s->service->ctx); break;
    case L_STOP:
      if (s->service->has_stop && s->service->stop_activate != NULL)
        s->service->stop_activate(s->service->ctx);
      break;
    case L_CREATE:
      if (s->service->has_create && s->service->create_submit != NULL)
        s->service->create_submit(s->service->ctx, s->name);
      break;
    case L_BACK: qa_native_ui_controller_close_menu(s->controller); break;
    default: break;
  }
}
static void on_change_query(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  set_str(&((lctx *)v)->state->query, value);
}
static void on_change_name(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  set_str(&((lctx *)v)->state->name, value);
}
static void on_select_row(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  set_str(&((lctx *)v)->state->selected, value);
}
static void on_activate_row(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  activate_entry(((lctx *)v)->state, value);
}
static void on_open(void *v, qa_seat_id seat) {
  (void)seat;
  lib_state *s = v;
  s->service->refresh(s->service->ctx);
}
static void on_noop(void *v, qa_seat_id seat) {
  (void)v;
  (void)seat;
}

static qa_ui_control *next_control(lib_state *s, size_t *at, int action) {
  if (*at >= s->control_capacity) {
    size_t capacity = s->control_capacity != 0 ? s->control_capacity * 2 : 16;
    qa_ui_control *controls = realloc(s->controls, capacity * sizeof *controls);
    lctx *ctxs = realloc(s->ctxs, capacity * sizeof *ctxs);
    if (controls == NULL || ctxs == NULL) qa_range_error("Out of memory");
    s->controls = controls;
    s->ctxs = ctxs;
    s->control_capacity = capacity;
  }
  qa_ui_control *control = &s->controls[*at];
  memset(control, 0, sizeof *control);
  s->ctxs[*at].state = s;
  s->ctxs[*at].action = action;
  control->ctx = &s->ctxs[*at];
  (*at)++;
  return control;
}
static void add_button(lib_state *s, size_t *at, const char *suffix,
                       const char *label, double x, double y, double width,
                       int action, bool enabled) {
  qa_ui_control *control = next_control(s, at, action);
  size_t length = strlen(suffix) + 16;
  char *buffer = malloc(length);
  if (buffer == NULL) qa_range_error("Out of memory");
  snprintf(buffer, length, "ui:library:%s", suffix);
  if (s->pool_count == s->pool_capacity) {
    size_t capacity = s->pool_capacity != 0 ? s->pool_capacity * 2 : 32;
    char **grown = realloc(s->pool, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    s->pool = grown;
    s->pool_capacity = capacity;
  }
  s->pool[s->pool_count++] = buffer;
  control->id = buffer;
  control->kind = "button";
  control->label = label;
  control->rect = (qa_rect){ x, y, width, 28 };
  control->enabled = enabled;
  control->visible = true;
  control->activate_seat = on_activate;
}

static qa_ui_menu factory(void *v) {
  lib_state *s = v;
  clear_pool(s);
  const char *next_scope = service_scope(s);
  if (strcmp(next_scope, s->scope) != 0) {
    lib_view *existing = NULL;
    for (size_t i = 0; i < s->view_count; i++)
      if (strcmp(s->views[i].scope, s->scope) == 0) existing = &s->views[i];
    if (existing == NULL) {
      if (s->view_count == s->view_capacity) {
        size_t capacity = s->view_capacity != 0 ? s->view_capacity * 2 : 8;
        lib_view *grown = realloc(s->views, capacity * sizeof *grown);
        if (grown == NULL) qa_range_error("Out of memory");
        s->views = grown;
        s->view_capacity = capacity;
      }
      existing = &s->views[s->view_count++];
      memset(existing, 0, sizeof *existing);
      set_str(&existing->scope, s->scope);
    }
    set_str(&existing->query, s->query);
    set_nullable(&existing->selected, s->selected);
    lib_view *restored = NULL;
    for (size_t i = 0; i < s->view_count; i++)
      if (strcmp(s->views[i].scope, next_scope) == 0) restored = &s->views[i];
    set_str(&s->query, restored != NULL ? restored->query : "");
    set_nullable(&s->selected, restored != NULL ? restored->selected : NULL);
    set_str(&s->scope, next_scope);
  }
  size_t entry_count = 0;
  const qa_ui_library_entry *all =
      s->service->entries(s->service->ctx, &entry_count);
  char *query_lower = lower_dup(s->query);
  /* Filtered entries, in service order. */
  const qa_ui_library_entry **entries = NULL;
  size_t filtered = 0;
  if (entry_count > 0) {
    entries = malloc(entry_count * sizeof *entries);
    if (entries == NULL) qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < entry_count; i++) {
    size_t length = strlen(all[i].label) +
                    strlen(all[i].detail != NULL ? all[i].detail : "") + 2;
    char *joined = malloc(length);
    if (joined == NULL) qa_range_error("Out of memory");
    snprintf(joined, length, "%s %s", all[i].label,
             all[i].detail != NULL ? all[i].detail : "");
    char *lowered = lower_dup(joined);
    free(joined);
    bool match = strstr(lowered, query_lower) != NULL;
    free(lowered);
    if (match) entries[filtered++] = &all[i];
  }
  free(query_lower);
  bool selected_found = false;
  for (size_t i = 0; i < filtered; i++)
    if (s->selected != NULL && strcmp(entries[i]->id, s->selected) == 0)
      selected_found = true;
  if (!selected_found)
    set_nullable(&s->selected, filtered > 0 ? entries[0]->id : NULL);
  if (filtered > s->row_capacity) {
    qa_ui_list_row *rows = realloc(s->rows, filtered * sizeof *rows);
    const char **cells = realloc(s->cells, filtered * 2 * sizeof *cells);
    if (rows == NULL || cells == NULL) qa_range_error("Out of memory");
    s->rows = rows;
    s->cells = cells;
    s->row_capacity = filtered;
  }
  for (size_t i = 0; i < filtered; i++) {
    memset(&s->rows[i], 0, sizeof s->rows[i]);
    s->rows[i].id = entries[i]->id;
    s->cells[i * 2] = entries[i]->label;
    s->cells[i * 2 + 1] = entries[i]->unavailable != NULL
                              ? entries[i]->unavailable
                              : entries[i]->detail != NULL ? entries[i]->detail
                                                            : "";
    s->rows[i].cells =
        (qa_string_view_list){ &s->cells[i * 2], 2 };
    s->rows[i].image = NULL;
    s->rows[i].enabled = entries[i]->unavailable == NULL;
  }
  size_t at = 0;
  qa_ui_control *search = next_control(s, &at, L_SEARCH);
  search->id = "ui:library:search";
  search->kind = "text-entry";
  search->label = "Search";
  search->text = s->query;
  search->maximum_length = 80;
  search->rect = (qa_rect){ 48, 88, 544, 28 };
  search->enabled = true;
  search->visible = true;
  search->change_text = on_change_query;
  search->submit_text = on_change_query;
  qa_ui_control *list = next_control(s, &at, L_ENTRIES);
  list->id = "ui:library:entries";
  list->kind = "list";
  list->label = s->title;
  list->rect = (qa_rect){ 48, 122, 544, 198 };
  list->has_row_height = true;
  list->row_height = 33;
  list->rows = s->rows;
  list->row_count = filtered;
  list->selected = s->selected;
  list->select_row = on_select_row;
  list->activate_row = on_activate_row;
  list->enabled = true;
  list->visible = true;
  bool open_enabled = false;
  if (s->selected != NULL)
    for (size_t i = 0; i < filtered; i++)
      if (strcmp(entries[i]->id, s->selected) == 0 &&
          entries[i]->unavailable == NULL)
        open_enabled = true;
  add_button(s, &at, "open", "Open", 48, 326, 164, L_OPEN, open_enabled);
  add_button(s, &at, "refresh", "Refresh", 222, 326, 164, L_REFRESH, true);
  if (s->service->has_stop)
    add_button(s, &at, "stop", s->service->stop_label, 396, 326, 196, L_STOP,
               true);
  if (s->service->has_create) {
    qa_ui_control *name = next_control(s, &at, L_NAME);
    name->id = "ui:library:name";
    name->kind = "text-entry";
    name->label = "Name";
    name->text = s->name;
    name->maximum_length = 128;
    name->rect = (qa_rect){ 48, 358, 354, 28 };
    name->enabled = true;
    name->visible = true;
    name->change_text = on_change_name;
    name->submit_text = on_change_name;
    add_button(s, &at, "create", s->service->create_label, 414, 358, 178,
               L_CREATE, !blank_trim(s->name));
  }
  const char *status = s->service->status(s->service->ctx);
  if (status == NULL) status = "";
  qa_ui_control *status_control = next_control(s, &at, L_NONE);
  status_control->id = "ui:library:status";
  status_control->kind = "button";
  status_control->label =
      status[0] != '\0' ? status : (filtered == 0 ? "No matching entries" : "");
  status_control->rect = (qa_rect){ 48, 390, 544, 22 };
  status_control->enabled = false;
  status_control->visible = true;
  status_control->activate_seat = on_activate;
  add_button(s, &at, "back", "Back", 48, 418, 544, L_BACK, true);
  free(entries);
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = s->id;
  menu.title = s->title;
  menu.full_screen = true;
  menu.controls = s->controls;
  menu.control_count = at;
  menu.ctx = s;
  menu.open = on_open;
  menu.close = on_noop;
  return menu;
}

static void lib_dispose(qa_ui_settings_menus *menus) {
  lib_state *s = menus->state;
  if (s == NULL) return;
  qa_native_ui_controller_unregister(s->controller, s->handle);
  clear_pool(s);
  free(s->pool);
  free(s->controls);
  free(s->ctxs);
  free(s->rows);
  free(s->cells);
  for (size_t i = 0; i < s->view_count; i++) {
    free(s->views[i].scope);
    free(s->views[i].query);
    free(s->views[i].selected);
  }
  free(s->views);
  free(s->id);
  free(s->title);
  free(s->selected);
  free(s->query);
  free(s->name);
  free(s->scope);
  free(s);
  menus->state = NULL;
}

qa_ui_settings_menus qa_ui_register_library_menu(
    qa_native_ui_controller *controller, qa_ui_menu_id id, const char *title,
    const qa_ui_library_menu_service *service) {
  lib_state *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->controller = controller;
  s->service = service;
  set_str(&s->id, id);
  set_str(&s->title, title);
  set_str(&s->query, "");
  set_str(&s->name, "");
  set_str(&s->scope, service_scope(s));
  int handle = qa_native_ui_controller_register(controller, id, factory, s);
  if (handle < 0) qa_range_error("Out of memory");
  s->handle = handle;
  qa_ui_settings_menus menus = { id, s, lib_dispose };
  return menus;
}
