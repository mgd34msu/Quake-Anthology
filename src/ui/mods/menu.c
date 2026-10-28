/* Port of src/ui/mods/menu.ts. GPL-2.0-or-later. */
#include "qa/ui/mods/menu.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/support.h"

typedef struct mods_state mods_state;
typedef struct xctx {
  mods_state *state;
  int action;
} xctx;
struct mods_state {
  qa_native_ui_controller *controller;
  const qa_ui_mod_menu_service *service;
  char *query;
  char *selected; /* NULL = TS null */
  int handle;
  qa_ui_control *controls;
  xctx *ctxs;
  size_t control_capacity;
  qa_ui_list_row *rows;
  const char **cells; /* 3 per row */
  size_t row_capacity;
  char **pool;
  size_t pool_count, pool_capacity;
  qa_ui_menu_scroll scroll;
  qa_ui_control_id *scroll_ids;
  size_t scroll_capacity;
};

enum { X_NONE, X_SEARCH, X_LIST, X_TOGGLE, X_REFRESH, X_BACK };

static const double column_widths[3] = { 216, 168, 144 };

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
static char *pool_own(mods_state *s, char *copy) {
  if (copy == NULL) qa_range_error("Out of memory");
  if (s->pool_count == s->pool_capacity) {
    size_t capacity = s->pool_capacity != 0 ? s->pool_capacity * 2 : 32;
    char **grown = realloc(s->pool, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    s->pool = grown;
    s->pool_capacity = capacity;
  }
  s->pool[s->pool_count++] = copy;
  return copy;
}
static char *pool_format(mods_state *s, const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length < 0) qa_range_error("Out of memory");
  char *buffer = malloc((size_t)length + 1);
  if (buffer == NULL) qa_range_error("Out of memory");
  vsnprintf(buffer, (size_t)length + 1, format, args);
  va_end(args);
  return pool_own(s, buffer);
}
static void clear_pool(mods_state *s) {
  for (size_t i = 0; i < s->pool_count; i++) free(s->pool[i]);
  s->pool_count = 0;
}
static char *lower_dup(const char *text) {
  char *copy = qa_input_dup(text);
  if (copy == NULL) qa_range_error("Out of memory");
  for (char *p = copy; *p != '\0'; p++)
    *p = (char)tolower((unsigned char)*p);
  return copy;
}

static void toggle_row(mods_state *s, const char *id) {
  size_t count = 0;
  const qa_ui_mod_menu_row *rows = s->service->rows(s->service->ctx, &count);
  for (size_t i = 0; i < count; i++)
    if (strcmp(rows[i].id, id) == 0) {
      if (rows[i].enabled || rows[i].unavailable == NULL)
        s->service->set_enabled(s->service->ctx, id, !rows[i].enabled);
      return;
    }
}
static void on_activate(void *v, qa_seat_id seat) {
  (void)seat;
  xctx *c = v;
  mods_state *s = c->state;
  switch (c->action) {
    case X_TOGGLE:
      if (s->selected != NULL) toggle_row(s, s->selected);
      break;
    case X_REFRESH: s->service->refresh(s->service->ctx); break;
    case X_BACK: qa_native_ui_controller_close_menu(s->controller); break;
    default: break;
  }
}
static void on_change_query(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  set_str(&((xctx *)v)->state->query, value);
}
static void on_select_row(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  set_nullable(&((xctx *)v)->state->selected, value);
}
static void on_activate_row(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  toggle_row(((xctx *)v)->state, value);
}
static void on_open(void *v, qa_seat_id seat) {
  (void)seat;
  mods_state *s = v;
  s->service->refresh(s->service->ctx);
}
static void on_noop(void *v, qa_seat_id seat) {
  (void)v;
  (void)seat;
}

static qa_ui_control *next_control(mods_state *s, size_t *at, int action) {
  if (*at >= s->control_capacity) {
    size_t capacity = s->control_capacity != 0 ? s->control_capacity * 2 : 16;
    qa_ui_control *controls =
        realloc(s->controls, capacity * sizeof *controls);
    xctx *ctxs = realloc(s->ctxs, capacity * sizeof *ctxs);
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
  control->visible = true;
  (*at)++;
  return control;
}
static void add_button(mods_state *s, size_t *at, const char *suffix,
                       const char *label, double x, double y, double width,
                       int action, bool enabled) {
  qa_ui_control *control = next_control(s, at, action);
  control->id = pool_format(s, "ui:mods:%s", suffix);
  control->kind = "button";
  control->label = label;
  control->rect = (qa_rect){ x, y, width, 28 };
  control->enabled = enabled;
  control->activate_seat = on_activate;
}

/* One /.{1,42}(?:\s+|$)|\S{1,42}/gu match at *cursor (ASCII view),
 * returned pooled; NULL at end. Advances *cursor past the match. */
static char *next_wrap_line(mods_state *s, const char **cursor) {
  const char *text = *cursor;
  if (text[0] == '\0') return NULL;
  size_t length = strlen(text);
  size_t take = 0;
  if (length <= 42) {
    take = length;
  } else {
    long found = -1;
    for (size_t k = 42; k >= 1; k--)
      if (isspace((unsigned char)text[k])) {
        found = (long)k;
        break;
      }
    if (found >= 0) {
      take = (size_t)found;
      while (take < length && isspace((unsigned char)text[take])) take++;
    } else {
      take = 0;
      while (take < length && take < 42 &&
             !isspace((unsigned char)text[take]))
        take++;
      if (take == 0) take = 1; /* leading space run: .{1,42}\s+ */
    }
  }
  char *line = qa_input_dup_range(text, take);
  if (line == NULL) qa_range_error("Out of memory");
  *cursor = text + take;
  return pool_own(s, line);
}

static qa_ui_menu factory(void *v) {
  mods_state *s = v;
  clear_pool(s);
  size_t all_count = 0;
  const qa_ui_mod_menu_row *all_rows =
      s->service->rows(s->service->ctx, &all_count);
  char *query_lower = lower_dup(s->query);
  char *trimmed = qa_input_js_trim(query_lower);
  if (trimmed == NULL) qa_range_error("Out of memory");
  free(query_lower);
  /* Split the trimmed query on whitespace runs (JS split(/\s+/u);
   * an empty query yields one empty term, matching everything). */
  char **terms = NULL;
  size_t term_count = 0, term_capacity = 0;
  if (trimmed[0] == '\0') {
    terms = malloc(sizeof *terms);
    if (terms == NULL) qa_range_error("Out of memory");
    terms[0] = qa_input_dup("");
    if (terms[0] == NULL) qa_range_error("Out of memory");
    term_count = 1;
    term_capacity = 1;
  } else {
    const char *cursor = trimmed;
    while (*cursor != '\0') {
      while (*cursor != '\0' && isspace((unsigned char)*cursor)) cursor++;
      if (*cursor == '\0') break;
      const char *start = cursor;
      while (*cursor != '\0' && !isspace((unsigned char)*cursor)) cursor++;
      if (term_count == term_capacity) {
        term_capacity = term_capacity != 0 ? term_capacity * 2 : 8;
        terms = realloc(terms, term_capacity * sizeof *terms);
        if (terms == NULL) qa_range_error("Out of memory");
      }
      terms[term_count] =
          qa_input_dup_range(start, (size_t)(cursor - start));
      if (terms[term_count] == NULL) qa_range_error("Out of memory");
      term_count++;
    }
  }
  const qa_ui_mod_menu_row **rows = NULL;
  size_t filtered = 0;
  if (all_count > 0) {
    rows = malloc(all_count * sizeof *rows);
    if (rows == NULL) qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < all_count; i++) {
    size_t length = strlen(all_rows[i].title) + strlen(all_rows[i].source) + 2;
    char *joined = malloc(length);
    if (joined == NULL) qa_range_error("Out of memory");
    snprintf(joined, length, "%s %s", all_rows[i].title, all_rows[i].source);
    char *lowered = lower_dup(joined);
    free(joined);
    bool match = true;
    for (size_t t = 0; t < term_count; t++)
      if (strstr(lowered, terms[t]) == NULL) match = false;
    free(lowered);
    if (match) rows[filtered++] = &all_rows[i];
  }
  for (size_t t = 0; t < term_count; t++) free(terms[t]);
  free(terms);
  free(trimmed);
  const qa_ui_mod_menu_row *current = NULL;
  for (size_t i = 0; i < filtered; i++)
    if (s->selected != NULL && strcmp(rows[i]->id, s->selected) == 0)
      current = rows[i];
  if (current == NULL && filtered > 0) current = rows[0];
  set_nullable(&s->selected, current != NULL ? current->id : NULL);
  /* Detail messages -> wrapped lines. */
  const char *status = s->service->status(s->service->ctx);
  if (status == NULL) status = "";
  size_t enabled_count = 0;
  for (size_t i = 0; i < all_count; i++)
    if (all_rows[i].enabled) enabled_count++;
  char *fallback_status = NULL;
  const char *status_text = status;
  if (status[0] == '\0') {
    char *number = qa_input_js_number((double)enabled_count);
    if (number == NULL) qa_range_error("Out of memory");
    size_t length = strlen(number) + 16;
    fallback_status = malloc(length);
    if (fallback_status == NULL) qa_range_error("Out of memory");
    snprintf(fallback_status, length, "%s enabled", number);
    free(number);
    status_text = fallback_status;
  }
  const char *messages[3];
  size_t message_count = 0;
  char *current_line = NULL;
  if (current != NULL) {
    size_t length = strlen(current->title) + strlen(current->source) + 4;
    current_line = malloc(length);
    if (current_line == NULL) qa_range_error("Out of memory");
    snprintf(current_line, length, "%s (%s)", current->title, current->source);
    messages[message_count++] = current_line;
  }
  if (current != NULL && current->unavailable != NULL)
    messages[message_count++] = current->unavailable;
  messages[message_count++] = status_text;
  size_t line_count = 0;
  for (size_t m = 0; m < message_count; m++) {
    const char *cursor = messages[m];
    char *line;
    while ((line = next_wrap_line(s, &cursor)) != NULL) {
      (void)line;
      line_count++;
    }
  }
  /* Lines were pooled by next_wrap_line in order; the detail
   * controls reference the last line_count pool entries created
   * for wrapping, but pool also holds control ids added later, so
   * snapshot them now. */
  size_t wrap_pool_start = s->pool_count - line_count;
  char **lines = NULL;
  if (line_count > 0) {
    lines = malloc(line_count * sizeof *lines);
    if (lines == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < line_count; i++)
      lines[i] = s->pool[wrap_pool_start + i];
  }
  size_t row_total = filtered > 0 ? filtered : 1;
  if (row_total > s->row_capacity) {
    qa_ui_list_row *grown_rows =
        realloc(s->rows, row_total * sizeof *grown_rows);
    const char **grown_cells =
        realloc(s->cells, row_total * 3 * sizeof *grown_cells);
    if (grown_rows == NULL || grown_cells == NULL)
      qa_range_error("Out of memory");
    s->rows = grown_rows;
    s->cells = grown_cells;
    s->row_capacity = row_total;
  }
  if (filtered == 0) {
    memset(&s->rows[0], 0, sizeof s->rows[0]);
    s->rows[0].id = "empty";
    s->cells[0] = all_count == 0 ? "No mods available" : "No matching mods";
    s->rows[0].cells = (qa_string_view_list){ &s->cells[0], 1 };
    s->rows[0].image = NULL;
    s->rows[0].enabled = false;
  } else {
    for (size_t i = 0; i < filtered; i++) {
      memset(&s->rows[i], 0, sizeof s->rows[i]);
      s->rows[i].id = rows[i]->id;
      s->cells[i * 3] = rows[i]->title;
      s->cells[i * 3 + 1] = rows[i]->source;
      s->cells[i * 3 + 2] = rows[i]->enabled
                                ? "Enabled"
                                : rows[i]->unavailable == NULL ? "Disabled"
                                                               : "Unavailable";
      s->rows[i].cells = (qa_string_view_list){ &s->cells[i * 3], 3 };
      s->rows[i].image = NULL;
      s->rows[i].enabled = true;
    }
  }
  size_t at = 0;
  qa_ui_control *search = next_control(s, &at, X_SEARCH);
  search->id = "ui:mods:search";
  search->kind = "text-entry";
  search->label = "Search mods";
  search->text = s->query;
  search->maximum_length = 80;
  search->rect = (qa_rect){ 48, 80, 544, 28 };
  search->enabled = true;
  search->change_text = on_change_query;
  search->submit_text = on_change_query;
  add_button(s, &at, "title", "Mod", 48, 112, 216, X_NONE, false);
  add_button(s, &at, "source", "Source", 264, 112, 168, X_NONE, false);
  add_button(s, &at, "state", "State", 432, 112, 160, X_NONE, false);
  qa_ui_control *list = next_control(s, &at, X_LIST);
  list->id = "ui:mods:list";
  list->kind = "list";
  list->label = "Mods";
  list->rect = (qa_rect){ 48, 140, 544, 192 };
  list->has_row_height = true;
  list->row_height = 32;
  list->column_widths = (qa_double_view){ column_widths, 3 };
  list->enabled = filtered > 0;
  list->selected = s->selected;
  list->rows = s->rows;
  list->row_count = row_total;
  list->select_row = on_select_row;
  list->activate_row = on_activate_row;
  add_button(s, &at, "toggle",
             current != NULL && current->enabled ? "Disable" : "Enable", 48,
             340, 176, X_TOGGLE,
             current != NULL &&
                 (current->enabled || current->unavailable == NULL));
  add_button(s, &at, "refresh", "Refresh", 232, 340, 176, X_REFRESH, true);
  add_button(s, &at, "back", "Back", 416, 340, 176, X_BACK, true);
  if (line_count > s->scroll_capacity) {
    qa_ui_control_id *grown =
        realloc(s->scroll_ids, line_count * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    s->scroll_ids = grown;
    s->scroll_capacity = line_count;
  }
  for (size_t i = 0; i < line_count; i++) {
    char *trimmed_line = qa_input_js_trim(lines[i]);
    if (trimmed_line == NULL) qa_range_error("Out of memory");
    qa_ui_control *detail = next_control(s, &at, X_NONE);
    detail->id = pool_format(s, "ui:mods:detail:%zu", i);
    detail->kind = "button";
    detail->label = pool_own(s, trimmed_line);
    detail->rect = (qa_rect){ 48, 372 + (double)i * 24, 528, 24 };
    detail->enabled = false;
    detail->activate_seat = on_activate;
    s->scroll_ids[i] = detail->id;
  }
  free(lines);
  free(rows);
  free(current_line);
  free(fallback_status);
  s->scroll = (qa_ui_menu_scroll){ (qa_rect){ 48, 372, 544, 72 },
                                   (double)line_count * 24, s->scroll_ids,
                                   line_count };
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = "menu:mods:root";
  menu.title = "Mods";
  menu.full_screen = true;
  menu.controls = s->controls;
  menu.control_count = at;
  menu.scroll = &s->scroll;
  menu.ctx = s;
  menu.open = on_open;
  menu.close = on_noop;
  return menu;
}

static void mods_dispose(qa_ui_settings_menus *menus) {
  mods_state *s = menus->state;
  if (s == NULL) return;
  qa_native_ui_controller_unregister(s->controller, s->handle);
  clear_pool(s);
  free(s->pool);
  free(s->controls);
  free(s->ctxs);
  free(s->rows);
  free(s->cells);
  free(s->scroll_ids);
  free(s->query);
  free(s->selected);
  free(s);
  menus->state = NULL;
}

qa_ui_settings_menus qa_ui_register_mod_menu(
    qa_native_ui_controller *controller,
    const qa_ui_mod_menu_service *service) {
  mods_state *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->controller = controller;
  s->service = service;
  set_str(&s->query, "");
  int handle = qa_native_ui_controller_register(controller, "menu:mods:root",
                                                factory, s);
  if (handle < 0) qa_range_error("Out of memory");
  s->handle = handle;
  qa_ui_settings_menus menus = { "menu:mods:root", s, mods_dispose };
  return menus;
}
