/* Port of src/ui/common/controller.ts. GPL-2.0-or-later. */
#include "qa/ui/common/controller.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/key_codes.h"
#include "qa/ui/common/accessibility.h"
#include "qa/ui/common/layout.h"

typedef struct menu_entry {
  qa_ui_menu_id id;
  qa_ui_menu_factory_fn factory;
  void *ctx;
  bool live;
} menu_entry;

typedef struct menu_cursor {
  qa_ui_menu_id id;
  qa_ui_control_id focus;
  double scroll;
} menu_cursor;

typedef struct field_cursor {
  qa_ui_control_id id;
  size_t cursor, start;
  bool overstrike;
} field_cursor;

typedef struct list_state {
  qa_ui_control_id id;
  double top;
  char **rows;
  size_t row_count;
} list_state;

typedef struct held_axis {
  char *id;
  double code, next;
} held_axis;

struct qa_native_ui_controller {
  qa_native_ui_options options;
  qa_seat_id seat;
  menu_entry *menus; size_t menu_count, menu_capacity;
  menu_cursor *stack; size_t stack_count, stack_capacity;
  field_cursor *fields; size_t field_count, field_capacity;
  list_state *lists; size_t list_count, list_capacity;
  double list_drag_offset;
  bool has_menu_drag_offset;
  double menu_drag_offset;
  held_axis *held; size_t held_count, held_capacity;
  qa_vec2 cursor;
  bool has_pointer;
  qa_vec2 pointer_position;
  qa_ui_transform transform;
  qa_ui_control_id dragging;
  bool shift, control;
  bool has_capture;
  void (*capture_accept)(void *ctx, qa_physical_input input);
  void (*capture_cancel)(void *ctx);
  void *capture_ctx;
  const qa_ui_notification *notifications;
  size_t notification_count;
  const qa_center_print_state *center_print;
  bool scores;
  qa_ui_draw_command *commands; size_t command_count, command_capacity;
  char **strings; size_t string_count, string_capacity;
  /* Scrolled copy of the active menu's controls (TS {...control}). */
  qa_ui_control *scrolled; size_t scrolled_capacity;
  qa_ui_menu active_menu;
};

static void clear_held(qa_native_ui_controller *c);

static void *grow(void *ptr, size_t *capacity, size_t needed, size_t size) {
  if (*capacity >= needed) return ptr;
  size_t cap = *capacity == 0 ? 8 : *capacity;
  while (cap < needed) cap *= 2;
  void *next = realloc(ptr, cap * size);
  if (next == NULL) return NULL;
  *capacity = cap;
  return next;
}

static char *xstrdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy != NULL) memcpy(copy, text, length);
  return copy;
}

static const qa_vec4 WHITE = { 1, 1, 1, 1 };

static bool control_enabled(const qa_ui_control *control) {
  return control->enabled && control->visible;
}
static bool kind_is(const qa_ui_control *control, const char *kind) {
  return control->kind != NULL && strcmp(control->kind, kind) == 0;
}
static double quantize(const qa_ui_control *control, double value) {
  return fmax(control->minimum, fmin(control->maximum,
    control->minimum + round((value - control->minimum) / control->step)
      * control->step));
}

/* UTF-8 code point helpers (TS Array.from). Invalid bytes count as
 * one code point each, matching the byte-wise fallback. */
static size_t cp_length(const char *text) {
  size_t count = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p != 0; p++)
    if ((*p & 0xc0) != 0x80) count++;
  return count;
}
static const char *cp_at(const char *text, size_t index) {
  const unsigned char *p = (const unsigned char *)text;
  while (*p != 0 && index > 0) {
    p++;
    while ((*p & 0xc0) == 0x80) p++;
    index--;
  }
  return (const char *)p;
}
static char *cp_slice(const char *text, size_t first, size_t last) {
  const char *a = cp_at(text, first), *b = cp_at(text, last);
  if (b < a) b = a;
  size_t length = (size_t)(b - a);
  char *out = malloc(length + 1);
  if (out == NULL) return NULL;
  memcpy(out, a, length);
  out[length] = '\0';
  return out;
}

qa_native_ui_controller *qa_native_ui_controller_create(
    const qa_native_ui_options *options) {
  qa_native_ui_controller *c = calloc(1, sizeof *c);
  if (c == NULL) return NULL;
  c->options = *options;
  c->seat = options->seat;
  c->cursor.x = 320;
  c->cursor.y = 240;
  c->transform.x = 0;
  c->transform.y = 0;
  c->transform.scale = 1;
  return c;
}

void qa_native_ui_controller_destroy(qa_native_ui_controller *c) {
  if (c == NULL) return;
  for (size_t i = 0; i < c->list_count; i++) {
    for (size_t j = 0; j < c->lists[i].row_count; j++) free(c->lists[i].rows[j]);
    free(c->lists[i].rows);
  }
  for (size_t i = 0; i < c->held_count; i++) free(c->held[i].id);
  for (size_t i = 0; i < c->string_count; i++) free(c->strings[i]);
  free(c->menus);
  free(c->stack);
  free(c->fields);
  free(c->lists);
  free(c->held);
  free(c->commands);
  free(c->strings);
  free(c->scrolled);
  free(c);
}

qa_seat_id qa_native_ui_controller_get_seat(
    const qa_native_ui_controller *controller) {
  return controller->seat;
}

static menu_entry *find_menu(qa_native_ui_controller *c, qa_ui_menu_id id) {
  for (size_t i = 0; i < c->menu_count; i++)
    if (c->menus[i].live && strcmp(c->menus[i].id, id) == 0) return &c->menus[i];
  return NULL;
}

int qa_native_ui_controller_register(qa_native_ui_controller *c,
                                     qa_ui_menu_id id,
                                     qa_ui_menu_factory_fn factory,
                                     void *factory_ctx) {
  if (find_menu(c, id) != NULL)
    qa_range_error("Menu already registered: %s", id);
  menu_entry *menus = grow(c->menus, &c->menu_capacity, c->menu_count + 1,
                           sizeof *menus);
  if (menus == NULL) return -1;
  c->menus = menus;
  size_t index = c->menu_count++;
  c->menus[index].id = id;
  c->menus[index].factory = factory;
  c->menus[index].ctx = factory_ctx;
  c->menus[index].live = true;
  return (int)index;
}

void qa_native_ui_controller_unregister(qa_native_ui_controller *c,
                                        int handle) {
  if (handle < 0 || (size_t)handle >= c->menu_count) return;
  menu_entry *entry = &c->menus[handle];
  if (!entry->live) return;
  qa_ui_menu_id id = entry->id;
  for (size_t i = 0; i < c->stack_count; i++) {
    if (strcmp(c->stack[i].id, id) == 0) {
      while (c->stack_count > i) qa_native_ui_controller_close_menu(c);
      break;
    }
  }
  entry->live = false;
}

qa_ui_menu_id qa_native_ui_controller_active_menu(
    const qa_native_ui_controller *c) {
  return c->stack_count == 0 ? NULL : c->stack[c->stack_count - 1].id;
}

bool qa_native_ui_controller_binding_capture(
    const qa_native_ui_controller *c) {
  return c->has_capture;
}

void qa_native_ui_controller_capture_binding(
    qa_native_ui_controller *c,
    void (*accept)(void *ctx, qa_physical_input input),
    void (*cancel)(void *ctx), void *ctx) {
  if (c->has_capture) c->capture_cancel(c->capture_ctx);
  c->has_capture = true;
  c->capture_accept = accept;
  c->capture_cancel = cancel;
  c->capture_ctx = ctx;
}

static void focus_changed(qa_native_ui_controller *c) {
  qa_seat_input_focus focus;
  memset(&focus, 0, sizeof focus);
  if (c->stack_count == 0) {
    focus.kind = "game";
  } else {
    menu_cursor *top = &c->stack[c->stack_count - 1];
    focus.kind = "menu";
    focus.menu = top->id;
    focus.control = top->focus;
  }
  c->options.focus(c->options.ctx, focus, c->options.now(c->options.ctx));
}

qa_seat_ui_state qa_native_ui_controller_state(qa_native_ui_controller *c) {
  qa_seat_ui_state state;
  memset(&state, 0, sizeof state);
  state.seat = c->seat;
  if (c->stack_count == 0) {
    state.focus.kind = "game";
  } else {
    menu_cursor *top = &c->stack[c->stack_count - 1];
    state.focus.kind = "menu";
    state.focus.menu = top->id;
    state.focus.control = top->focus;
  }
  state.cursor = c->cursor;
  size_t count = 0;
  state.bindings = c->options.bindings(c->options.ctx, &count);
  state.binding_count = count;
  state.notifications = c->notifications;
  state.notification_count = c->notification_count;
  state.center_print = c->center_print;
  state.show_scores = c->scores;
  return state;
}

void qa_native_ui_controller_presentation(
    qa_native_ui_controller *c, const qa_ui_notification *notifications,
    size_t notification_count, const qa_center_print_state *center_print,
    bool show_scores) {
  c->notifications = notifications;
  c->notification_count = notification_count;
  c->center_print = center_print;
  c->scores = show_scores;
}

void qa_native_ui_controller_open_menu(qa_native_ui_controller *c,
                                       qa_ui_menu_id id) {
  menu_entry *entry = find_menu(c, id);
  if (entry == NULL) qa_range_error("Unknown menu: %s", id);
  for (size_t i = 0; i < c->stack_count; i++) {
    if (strcmp(c->stack[i].id, id) == 0) {
      while (c->stack_count > i + 1) qa_native_ui_controller_close_menu(c);
      focus_changed(c);
      return;
    }
  }
  qa_ui_menu menu = entry->factory(entry->ctx);
  menu_cursor *stack = grow(c->stack, &c->stack_capacity, c->stack_count + 1,
                            sizeof *stack);
  if (stack == NULL) qa_range_error("Out of memory");
  c->stack = stack;
  menu_cursor *top = &c->stack[c->stack_count++];
  top->id = id;
  top->focus = NULL;
  top->scroll = 0;
  for (size_t i = 0; i < menu.control_count; i++)
    if (control_enabled(&menu.controls[i])) {
      top->focus = menu.controls[i].id;
      break;
    }
  c->dragging = NULL;
  c->has_menu_drag_offset = false;
  clear_held(c);
  if (menu.open != NULL) menu.open(menu.ctx, c->seat);
  focus_changed(c);
  c->options.sound(c->options.ctx, "open", c->seat);
}

void qa_native_ui_controller_close_menu(qa_native_ui_controller *c) {
  if (c->stack_count == 0) return;
  menu_cursor current = c->stack[--c->stack_count];
  if (c->has_capture) c->capture_cancel(c->capture_ctx);
  c->has_capture = false;
  c->dragging = NULL;
  c->has_menu_drag_offset = false;
  clear_held(c);
  menu_entry *entry = find_menu(c, current.id);
  if (entry != NULL) {
    qa_ui_menu menu = entry->factory(entry->ctx);
    if (menu.close != NULL) menu.close(menu.ctx, c->seat);
  }
  focus_changed(c);
  c->options.sound(c->options.ctx, "close", c->seat);
}

void qa_native_ui_controller_close_all(qa_native_ui_controller *c) {
  while (c->stack_count > 0) qa_native_ui_controller_close_menu(c);
}

void qa_native_ui_controller_execute_script(qa_native_ui_controller *c,
                                            qa_legacy_ui_script script) {
  c->options.execute_script(c->options.ctx, script, c->seat);
}

static void clear_held(qa_native_ui_controller *c) {
  for (size_t i = 0; i < c->held_count; i++) free(c->held[i].id);
  c->held_count = 0;
}

/* TS active(): re-reads the factory, repairs focus, clamps scroll and
 * returns a menu whose scrolled controls are shifted copies. */
static bool active(qa_native_ui_controller *c, qa_ui_menu *out_menu,
                   menu_cursor **out_cursor) {
  if (c->stack_count == 0) return false;
  menu_cursor *cursor = &c->stack[c->stack_count - 1];
  menu_entry *entry = find_menu(c, cursor->id);
  if (entry == NULL)
    qa_range_error("Active menu is not registered: %s", cursor->id);
  qa_ui_menu menu = entry->factory(entry->ctx);
  if (strcmp(menu.id, cursor->id) != 0)
    qa_range_error("Menu factory returned a different identity");
  bool focus_ok = false;
  for (size_t i = 0; i < menu.control_count; i++)
    if (cursor->focus != NULL
        && strcmp(menu.controls[i].id, cursor->focus) == 0
        && control_enabled(&menu.controls[i])) focus_ok = true;
  if (!focus_ok) {
    cursor->focus = NULL;
    for (size_t i = 0; i < menu.control_count; i++)
      if (control_enabled(&menu.controls[i])) {
        cursor->focus = menu.controls[i].id;
        break;
      }
  }
  if (menu.scroll != NULL) {
    double maximum = fmax(0, menu.scroll->content_height - menu.scroll->rect.height);
    cursor->scroll = fmax(0, fmin(maximum, cursor->scroll));
    qa_ui_control *scrolled = grow(c->scrolled, &c->scrolled_capacity,
                                   menu.control_count, sizeof *scrolled);
    if (scrolled == NULL && menu.control_count > 0)
      qa_range_error("Out of memory");
    c->scrolled = scrolled;
    for (size_t i = 0; i < menu.control_count; i++) {
      c->scrolled[i] = menu.controls[i];
      for (size_t j = 0; j < menu.scroll->control_count; j++)
        if (strcmp(menu.scroll->controls[j], menu.controls[i].id) == 0)
          c->scrolled[i].rect.y -= cursor->scroll;
    }
    menu.controls = c->scrolled;
  }
  c->active_menu = menu;
  *out_menu = menu;
  *out_cursor = cursor;
  return true;
}

static void reveal(const qa_ui_menu *menu, menu_cursor *cursor,
                   const qa_ui_control *control) {
  if (menu->scroll == NULL) return;
  bool scrolled = false;
  for (size_t i = 0; i < menu->scroll->control_count; i++)
    if (strcmp(menu->scroll->controls[i], control->id) == 0) scrolled = true;
  if (!scrolled) return;
  qa_rect sr = menu->scroll->rect;
  if (control->rect.y < sr.y) cursor->scroll -= sr.y - control->rect.y;
  else if (control->rect.y + control->rect.height > sr.y + sr.height)
    cursor->scroll += control->rect.y + control->rect.height - sr.y - sr.height;
  double maximum = fmax(0, menu->scroll->content_height - sr.height);
  cursor->scroll = fmax(0, fmin(maximum, cursor->scroll));
}

static void move_focus(qa_native_ui_controller *c, int direction) {
  qa_ui_menu menu;
  menu_cursor *cursor = NULL;
  if (!active(c, &menu, &cursor)) return;
  size_t count = 0;
  int index = -1;
  for (size_t i = 0; i < menu.control_count; i++) {
    if (!control_enabled(&menu.controls[i])) continue;
    if (cursor->focus != NULL
        && strcmp(menu.controls[i].id, cursor->focus) == 0)
      index = (int)count;
    count++;
  }
  if (count == 0) return;
  int next_index = (int)((index + direction + (int)count) % (int)count);
  size_t seen = 0;
  const qa_ui_control *next = NULL;
  for (size_t i = 0; i < menu.control_count; i++) {
    if (!control_enabled(&menu.controls[i])) continue;
    if (seen == (size_t)next_index) next = &menu.controls[i];
    seen++;
  }
  if (next != NULL && (cursor->focus == NULL
                       || strcmp(next->id, cursor->focus) != 0)) {
    cursor->focus = next->id;
    reveal(&menu, cursor, next);
    focus_changed(c);
    c->options.sound(c->options.ctx, "move", c->seat);
  }
}

static double menu_thumb(const qa_ui_menu *menu) {
  if (menu->scroll == NULL) return 0;
  qa_rect sr = menu->scroll->rect;
  return fmin(sr.height, fmax(24, sr.height * sr.height
                              / fmax(1, menu->scroll->content_height)));
}

static void menu_pointer(qa_native_ui_controller *c, const qa_ui_menu *menu,
                         menu_cursor *cursor) {
  if (menu->scroll == NULL || !c->has_menu_drag_offset) return;
  qa_rect sr = menu->scroll->rect;
  double travel = sr.height - menu_thumb(menu);
  double maximum = fmax(0, menu->scroll->content_height - sr.height);
  cursor->scroll = travel <= 0 ? 0 : fmax(0, fmin(maximum,
    (c->cursor.y - sr.y - c->menu_drag_offset) / travel * maximum));
}

static bool hit(qa_native_ui_controller *c, const qa_ui_menu *menu,
                const qa_ui_control *control) {
  if (!control_enabled(control) || !qa_ui_contains(control->rect, c->cursor))
    return false;
  if (menu->scroll == NULL) return true;
  bool scrolled = false;
  for (size_t i = 0; i < menu->scroll->control_count; i++)
    if (strcmp(menu->scroll->controls[i], control->id) == 0) scrolled = true;
  return !scrolled || qa_ui_contains(menu->scroll->rect, c->cursor);
}

static void change_control(qa_native_ui_controller *c,
                           const qa_ui_control *control, int direction) {
  if (!control_enabled(control)) return;
  if (kind_is(control, "toggle")) {
    control->change_checked(control->ctx, c->seat, !control->checked);
  } else if (kind_is(control, "slider")) {
    if (control->step <= 0 || control->minimum > control->maximum)
      qa_range_error("Invalid menu slider range");
    control->change_value(control->ctx, c->seat,
      quantize(control, control->value + control->step * direction));
  } else if (kind_is(control, "choice")) {
    if (control->choice_count == 0) return;
    int at = -1;
    for (size_t i = 0; i < control->choice_count; i++)
      if (control->selected != NULL
          && strcmp(control->choices[i].id, control->selected) == 0)
        at = (int)i;
    size_t next = (size_t)((at + direction + (int)control->choice_count)
                           % (int)control->choice_count);
    control->select_choice(control->ctx, c->seat, control->choices[next].id);
  } else return;
  c->options.sound(c->options.ctx, "change", c->seat);
}

static void activate_control(qa_native_ui_controller *c,
                             const qa_ui_control *control) {
  if (!control_enabled(control)) {
    c->options.sound(c->options.ctx, "reject", c->seat);
    return;
  }
  if (kind_is(control, "button")) {
    control->activate_seat(control->ctx, c->seat);
    c->options.sound(c->options.ctx, "change", c->seat);
  } else if (kind_is(control, "toggle") || kind_is(control, "slider")
             || kind_is(control, "choice")) {
    change_control(c, control, 1);
  } else if (kind_is(control, "text-entry")) {
    control->submit_text(control->ctx, c->seat, control->text);
  } else if (kind_is(control, "list")) {
    if (control->selected != NULL) {
      if (control->activate_row != NULL)
        control->activate_row(control->ctx, c->seat, control->selected);
      else control->select_row(control->ctx, c->seat, control->selected);
    }
  } else if (kind_is(control, "owner-draw")) {
    control->key(control->ctx, c->seat, QA_KEY_ENTER, true);
  }
}

static field_cursor *field_for(qa_native_ui_controller *c,
                               const qa_ui_control *control) {
  for (size_t i = 0; i < c->field_count; i++)
    if (strcmp(c->fields[i].id, control->id) == 0) {
      size_t length = cp_length(control->text);
      if (c->fields[i].cursor > length) c->fields[i].cursor = length;
      return &c->fields[i];
    }
  field_cursor *fields = grow(c->fields, &c->field_capacity,
                              c->field_count + 1, sizeof *fields);
  if (fields == NULL) qa_range_error("Out of memory");
  c->fields = fields;
  field_cursor *field = &c->fields[c->field_count++];
  field->id = control->id;
  field->cursor = cp_length(control->text);
  field->start = 0;
  field->overstrike = false;
  return field;
}

static void emit_text_change(const qa_ui_control *control, qa_seat_id seat,
                             char *text) {
  if (text == NULL) qa_range_error("Out of memory");
  control->change_text(control->ctx, seat, text);
  free(text);
}

/* Splice: replace `remove` code points at `at` with `insert`. */
static char *splice_text(const char *text, size_t at, size_t remove,
                         const char *insert) {
  const char *a = cp_at(text, at), *b = cp_at(text, at + remove);
  size_t head = (size_t)(a - text), tail = strlen(b), add = strlen(insert);
  char *out = malloc(head + add + tail + 1);
  if (out == NULL) return NULL;
  memcpy(out, text, head);
  memcpy(out + head, insert, add);
  memcpy(out + head + add, b, tail + 1);
  return out;
}

static void field_text(qa_native_ui_controller *c,
                       const qa_ui_control *control, const char *text) {
  field_cursor *field = field_for(c, control);
  size_t length = cp_length(control->text);
  /* Drop C0 controls and DEL (TS /[\u0000-\u001f\u007f]/u). */
  char *clean = malloc(strlen(text) + 1);
  if (clean == NULL) qa_range_error("Out of memory");
  size_t kept = 0;
  for (size_t i = 0; text[i] != '\0'; i++) {
    unsigned char ch = (unsigned char)text[i];
    if (ch >= 0x20 && ch != 0x7f) clean[kept++] = text[i];
    else if (ch >= 0x80) clean[kept++] = text[i];
  }
  clean[kept] = '\0';
  size_t incoming = cp_length(clean);
  double room_d = (double)control->maximum_length - (double)length
    + (field->overstrike ? (double)(length - field->cursor) : 0);
  size_t room = room_d <= 0 ? 0 : (size_t)room_d;
  size_t accepted = incoming < room ? incoming : room;
  char *add = cp_slice(clean, 0, accepted);
  free(clean);
  if (add == NULL) qa_range_error("Out of memory");
  char *next = splice_text(control->text, field->cursor,
                           field->overstrike ? accepted : 0, add);
  free(add);
  field->cursor += accepted;
  emit_text_change(control, c->seat, next);
}

static bool field_key(qa_native_ui_controller *c,
                      const qa_ui_control *control, double key) {
  field_cursor *field = field_for(c, control);
  size_t length = cp_length(control->text);
  int code = (int)key;
  if (code == QA_KEY_LEFT && field->cursor > 0) field->cursor--;
  else if (code == QA_KEY_LEFT) { /* stays 0 */ }
  else if (code == QA_KEY_RIGHT) {
    if (field->cursor < length) field->cursor++;
  } else if (code == QA_KEY_HOME || (c->control && code == 97)) {
    field->cursor = 0;
  } else if (code == QA_KEY_END || (c->control && code == 101)) {
    field->cursor = length;
  } else if (code == QA_KEY_INSERT) {
    field->overstrike = !field->overstrike;
  } else if (code == QA_KEY_BACKSPACE || (c->control && code == 104)) {
    if (field->cursor > 0) {
      field->cursor--;
      emit_text_change(control, c->seat,
                       splice_text(control->text, field->cursor, 1, ""));
    }
  } else if (code == QA_KEY_DELETE) {
    emit_text_change(control, c->seat,
                     splice_text(control->text, field->cursor, 1, ""));
  } else if (c->control && code == 117) {
    field->cursor = 0;
    control->change_text(control->ctx, c->seat, "");
  } else if (c->control && code == 118) {
    const char *text = c->options.clipboard != NULL
      ? c->options.clipboard(c->options.ctx) : NULL;
    if (text != NULL) field_text(c, control, text);
  } else return false;
  return true;
}

typedef struct list_layout {
  double top, page, height, maximum, thumb;
} list_layout;

static list_state *list_for(qa_native_ui_controller *c,
                            const qa_ui_control *control) {
  for (size_t i = 0; i < c->list_count; i++)
    if (strcmp(c->lists[i].id, control->id) == 0) return &c->lists[i];
  list_state *lists = grow(c->lists, &c->list_capacity, c->list_count + 1,
                           sizeof *lists);
  if (lists == NULL) qa_range_error("Out of memory");
  c->lists = lists;
  list_state *state = &c->lists[c->list_count++];
  state->id = control->id;
  state->top = 0;
  state->rows = NULL;
  state->row_count = 0;
  return state;
}

static list_layout list_layout_of(qa_native_ui_controller *c,
                                  const qa_ui_control *control) {
  double height = fmax(1, control->has_row_height ? control->row_height
                       : c->options.skin(c->options.ctx).line_height);
  double page = fmax(1, floor(control->rect.height / height));
  double maximum = fmax(0, (double)control->row_count - page);
  list_state *state = list_for(c, control);
  bool changed = state->rows == NULL || state->row_count != control->row_count;
  if (!changed)
    for (size_t i = 0; i < control->row_count; i++)
      if (strcmp(state->rows[i], control->rows[i].id) != 0) changed = true;
  if (changed) {
    for (size_t i = 0; i < state->row_count; i++) free(state->rows[i]);
    free(state->rows);
    state->rows = control->row_count == 0 ? NULL
      : malloc(control->row_count * sizeof *state->rows);
    if (state->rows == NULL && control->row_count > 0)
      qa_range_error("Out of memory");
    state->row_count = control->row_count;
    int selected = -1;
    for (size_t i = 0; i < control->row_count; i++) {
      state->rows[i] = xstrdup(control->rows[i].id);
      if (state->rows[i] == NULL) qa_range_error("Out of memory");
      if (control->selected != NULL
          && strcmp(control->rows[i].id, control->selected) == 0)
        selected = (int)i;
    }
    state->top = fmax(0, selected - page + 1);
  }
  double top = fmax(0, fmin(maximum, state->top));
  state->top = top;
  list_layout out = { top, page, height, maximum,
    fmin(control->rect.height,
         fmax(24, control->rect.height * page
                  / fmax(1, (double)control->row_count))) };
  return out;
}

static void list_pointer(qa_native_ui_controller *c,
                         const qa_ui_control *control) {
  list_layout layout = list_layout_of(c, control);
  double travel = control->rect.height - layout.thumb;
  list_state *state = list_for(c, control);
  state->top = travel <= 0 ? 0 : fmax(0, fmin(layout.maximum,
    round((c->cursor.y - control->rect.y - c->list_drag_offset) / travel
          * layout.maximum)));
}

static bool list_key(qa_native_ui_controller *c, const qa_ui_control *control,
                     double key) {
  int code = (int)key;
  if (code == QA_KEY_DELETE) {
    for (size_t i = 0; i < control->row_count; i++) {
      const qa_ui_list_row *row = &control->rows[i];
      if (control->selected != NULL && strcmp(row->id, control->selected) == 0
          && row->enabled && row->action != NULL)
        row->action->activate(row->action->ctx, c->seat);
    }
    return true;
  }
  list_layout layout = list_layout_of(c, control);
  int current = -1, enabled_count = 0;
  for (size_t i = 0; i < control->row_count; i++) {
    if (!control->rows[i].enabled) continue;
    if (control->selected != NULL
        && strcmp(control->rows[i].id, control->selected) == 0)
      current = enabled_count;
    enabled_count++;
  }
  int index = current;
  if (code == QA_KEY_UP) index--;
  else if (code == QA_KEY_DOWN) index++;
  else if (code == QA_KEY_PAGE_UP) index -= (int)layout.page;
  else if (code == QA_KEY_PAGE_DOWN) index += (int)layout.page;
  else if (code == QA_KEY_HOME) index = 0;
  else if (code == QA_KEY_END) index = enabled_count - 1;
  else return false;
  if (enabled_count > 0) {
    int clamped = index < 0 ? 0 : index > enabled_count - 1
      ? enabled_count - 1 : index;
    int seen = -1;
    const qa_ui_list_row *row = NULL;
    size_t source = 0;
    for (size_t i = 0; i < control->row_count; i++) {
      if (!control->rows[i].enabled) continue;
      seen++;
      if (seen == clamped) { row = &control->rows[i]; source = i; }
    }
    if (row != NULL) {
      control->select_row(control->ctx, c->seat, row->id);
      list_state *state = list_for(c, control);
      double top = state->top;
      state->top = (double)source < top ? (double)source
        : (double)source >= top + layout.page
          ? (double)source - layout.page + 1 : top;
    }
  }
  return true;
}

static const qa_ui_control *focused_control(const qa_ui_menu *menu,
                                            const menu_cursor *cursor) {
  if (cursor->focus == NULL) return NULL;
  for (size_t i = 0; i < menu->control_count; i++)
    if (strcmp(menu->controls[i].id, cursor->focus) == 0)
      return &menu->controls[i];
  return NULL;
}

static bool key_event(qa_native_ui_controller *c, double code_d, bool down) {
  int code = (int)code_d;
  if (code == QA_KEY_SHIFT) { c->shift = down; return true; }
  if (code == QA_KEY_CONTROL) { c->control = down; return true; }
  qa_ui_menu menu;
  menu_cursor *cursor = NULL;
  if (!active(c, &menu, &cursor)) return false;
  const qa_ui_control *control = focused_control(&menu, cursor);
  if (control != NULL && kind_is(control, "owner-draw")
      && control->key(control->ctx, c->seat, code_d, down)) return true;
  if (!down) return true;
  if (code == QA_KEY_ESCAPE) {
    qa_native_ui_controller_close_menu(c);
    return true;
  }
  if (control != NULL) reveal(&menu, cursor, control);
  if (control != NULL && kind_is(control, "text-entry")
      && field_key(c, control, code_d)) return true;
  if (control != NULL && kind_is(control, "list")
      && list_key(c, control, code_d)) return true;
  if (code == QA_KEY_TAB) move_focus(c, c->shift ? -1 : 1);
  else if (code == QA_KEY_UP || code == QA_KEY_KEYPAD_UP) move_focus(c, -1);
  else if (code == QA_KEY_DOWN || code == QA_KEY_KEYPAD_DOWN) move_focus(c, 1);
  else if (control != NULL && (code == QA_KEY_LEFT || code == QA_KEY_KEYPAD_LEFT))
    change_control(c, control, -1);
  else if (control != NULL && (code == QA_KEY_RIGHT || code == QA_KEY_KEYPAD_RIGHT))
    change_control(c, control, 1);
  else if (control != NULL && (code == QA_KEY_ENTER || code == QA_KEY_KEYPAD_ENTER
      || (code == QA_KEY_SPACE && !kind_is(control, "text-entry"))))
    activate_control(c, control);
  return true;
}

static void slider_pointer(qa_native_ui_controller *c,
                           const qa_ui_control *control) {
  double left = control->rect.x + control->rect.width * 0.6;
  double width = control->rect.width * 0.32;
  control->change_value(control->ctx, c->seat, quantize(control,
    control->minimum + (control->maximum - control->minimum)
      * (c->cursor.x - left) / width));
}

static void pointer_move(qa_native_ui_controller *c, qa_vec2 position) {
  c->has_pointer = true;
  c->pointer_position = position;
  c->cursor = qa_ui_point(position, c->transform);
  qa_ui_menu menu;
  menu_cursor *cursor = NULL;
  if (!active(c, &menu, &cursor)) return;
  if (c->has_menu_drag_offset) { menu_pointer(c, &menu, cursor); return; }
  const qa_ui_control *drag = NULL;
  if (c->dragging != NULL)
    for (size_t i = 0; i < menu.control_count; i++)
      if (strcmp(menu.controls[i].id, c->dragging) == 0)
        drag = &menu.controls[i];
  if (drag != NULL && kind_is(drag, "slider")) { slider_pointer(c, drag); return; }
  if (drag != NULL && kind_is(drag, "list")) { list_pointer(c, drag); return; }
  const qa_ui_control *hovered = NULL;
  for (size_t i = menu.control_count; i > 0; i--)
    if (hit(c, &menu, &menu.controls[i - 1])) {
      hovered = &menu.controls[i - 1];
      break;
    }
  if (hovered != NULL && (cursor->focus == NULL
                          || strcmp(hovered->id, cursor->focus) != 0)) {
    cursor->focus = hovered->id;
    focus_changed(c);
    c->options.sound(c->options.ctx, "move", c->seat);
  }
}

static bool capture_event(qa_native_ui_controller *c,
                          qa_seat_input_event event) {
  if (!c->has_capture) return false;
  void (*accept)(void *ctx, qa_physical_input input) = c->capture_accept;
  void (*cancel)(void *ctx) = c->capture_cancel;
  void *ctx = c->capture_ctx;
  if (strcmp(event.kind, "key") == 0 && event.down
      && (int)event.code == QA_KEY_ESCAPE) {
    c->has_capture = false;
    cancel(ctx);
    return true;
  }
  qa_physical_input input;
  memset(&input, 0, sizeof input);
  bool have = false;
  if (strcmp(event.kind, "key") == 0 && event.down && !event.repeat) {
    input.kind = QA_PHYSICAL_INPUT_KEY;
    input.code = event.code;
    have = true;
  } else if (strcmp(event.kind, "mouse-button") == 0 && event.down) {
    input.kind = QA_PHYSICAL_INPUT_MOUSE_BUTTON;
    input.button = event.button;
    have = true;
  } else if (strcmp(event.kind, "mouse-wheel") == 0 && event.delta.y != 0) {
    input.kind = QA_PHYSICAL_INPUT_KEY;
    input.code = event.delta.y > 0 ? QA_KEY_MOUSE_WHEEL_UP
                                   : QA_KEY_MOUSE_WHEEL_DOWN;
    have = true;
  } else if (strcmp(event.kind, "controller-button") == 0 && event.down) {
    input.kind = QA_PHYSICAL_INPUT_CONTROLLER_BUTTON;
    input.device = event.device;
    input.button = event.button;
    have = true;
  } else if (strcmp(event.kind, "controller-axis") == 0
             && fabs(event.value) > 0.65) {
    input.kind = QA_PHYSICAL_INPUT_CONTROLLER_AXIS;
    input.device = event.device;
    input.axis = event.axis;
    input.direction = event.value < 0 ? "negative" : "positive";
    have = true;
  }
  if (have) {
    c->has_capture = false;
    accept(ctx, input);
  }
  return true;
}

bool qa_native_ui_controller_input(qa_native_ui_controller *c,
                                   qa_seat_input_event event) {
  if (!qa_same_seat(event.seat, c->seat))
    qa_range_error("UI input delivered to another seat");
  if (strcmp(event.kind, "focus") == 0 && !event.focused) {
    c->dragging = NULL;
    c->has_menu_drag_offset = false;
    clear_held(c);
    c->shift = false;
    c->control = false;
    if (c->has_capture) c->capture_cancel(c->capture_ctx);
    c->has_capture = false;
  }
  if (capture_event(c, event)) return true;
  qa_ui_menu menu;
  menu_cursor *cursor = NULL;
  if (!active(c, &menu, &cursor)) return false;
  if (strcmp(event.kind, "key") == 0) {
    return key_event(c, event.code, event.down);
  } else if (strcmp(event.kind, "text") == 0) {
    const qa_ui_control *control = focused_control(&menu, cursor);
    if (control != NULL && kind_is(control, "text-entry")) {
      reveal(&menu, cursor, control);
      field_text(c, control, event.text);
    }
  } else if (strcmp(event.kind, "mouse-motion") == 0) {
    pointer_move(c, event.position);
  } else if (strcmp(event.kind, "mouse-button") == 0) {
    if (!event.down) {
      c->dragging = NULL;
      c->has_menu_drag_offset = false;
      return true;
    }
    if (event.button == 3) {
      qa_native_ui_controller_close_menu(c);
      return true;
    }
    if (event.button != 1) return true;
    if (menu.scroll != NULL
        && menu.scroll->content_height > menu.scroll->rect.height
        && qa_ui_contains(menu.scroll->rect, c->cursor)
        && c->cursor.x >= menu.scroll->rect.x + menu.scroll->rect.width - 16) {
      qa_rect sr = menu.scroll->rect;
      double thumb = menu_thumb(&menu);
      double y = sr.y + (sr.height - thumb) * cursor->scroll
        / (menu.scroll->content_height - sr.height);
      c->has_menu_drag_offset = true;
      c->menu_drag_offset = c->cursor.y >= y && c->cursor.y < y + thumb
        ? c->cursor.y - y : thumb / 2;
      menu_pointer(c, &menu, cursor);
      return true;
    }
    const qa_ui_control *control = NULL;
    for (size_t i = menu.control_count; i > 0; i--)
      if (hit(c, &menu, &menu.controls[i - 1])) {
        control = &menu.controls[i - 1];
        break;
      }
    if (control == NULL) return true;
    cursor->focus = control->id;
    focus_changed(c);
    if (kind_is(control, "slider")) {
      c->dragging = control->id;
      slider_pointer(c, control);
    } else if (kind_is(control, "text-entry")) {
      field_cursor *field = field_for(c, control);
      size_t length = cp_length(control->text);
      char *shown = malloc(strlen(control->text) + 1);
      if (shown == NULL) qa_range_error("Out of memory");
      if (control->has_masked && control->masked) {
        for (size_t i = 0; i < length; i++) shown[i] = '*';
        shown[length] = '\0';
      } else strcpy(shown, control->text);
      double x = c->cursor.x - control->rect.x - control->rect.width * 0.5;
      qa_native_ui_appearance appearance = c->options.appearance != NULL
        ? c->options.appearance(c->options.ctx)
        : (qa_native_ui_appearance){ 1, 1, false, NULL };
      double scale = c->options.skin(c->options.ctx).font_scale
        * appearance.text_scale;
      field->cursor = field->start;
      while (field->cursor < length) {
        char *prefix = cp_slice(shown, field->start, field->cursor + 1);
        if (prefix == NULL) qa_range_error("Out of memory");
        double width = c->options.measure_text != NULL
          ? c->options.measure_text(c->options.ctx, prefix, scale)
          : (double)cp_length(prefix) * 8 * scale;
        free(prefix);
        if (width >= x) break;
        field->cursor++;
      }
      free(shown);
    } else if (kind_is(control, "list")) {
      list_layout layout = list_layout_of(c, control);
      if (layout.maximum > 0
          && c->cursor.x >= control->rect.x + control->rect.width - 16) {
        double thumb_y = control->rect.y
          + (control->rect.height - layout.thumb) * layout.top
            / layout.maximum;
        c->list_drag_offset = c->cursor.y >= thumb_y
          && c->cursor.y < thumb_y + layout.thumb
            ? c->cursor.y - thumb_y : layout.thumb / 2;
        c->dragging = control->id;
        list_pointer(c, control);
      } else {
        double at = layout.top
          + floor((c->cursor.y - control->rect.y) / layout.height);
        if (at >= 0 && at < (double)control->row_count) {
          const qa_ui_list_row *row = &control->rows[(size_t)at];
          if (row->enabled) {
            control->select_row(control->ctx, c->seat, row->id);
            double action_x = control->rect.x + control->rect.width
              - (layout.maximum > 0 ? 16 : 0) - 28;
            if (row->action != NULL && c->cursor.x >= action_x)
              row->action->activate(row->action->ctx, c->seat);
            else if (control->activate_row != NULL)
              control->activate_row(control->ctx, c->seat, row->id);
          }
        }
      }
    } else activate_control(c, control);
  } else if (strcmp(event.kind, "mouse-wheel") == 0) {
    if (menu.scroll != NULL && qa_ui_contains(menu.scroll->rect, c->cursor)) {
      double maximum = fmax(0, menu.scroll->content_height
                              - menu.scroll->rect.height);
      double sign = event.delta.y > 0 ? 1 : event.delta.y < 0 ? -1 : 0;
      cursor->scroll = fmax(0, fmin(maximum, cursor->scroll - sign * 84));
      return true;
    }
    const qa_ui_control *hovered = NULL;
    for (size_t i = 0; i < menu.control_count; i++)
      if (control_enabled(&menu.controls[i])
          && kind_is(&menu.controls[i], "list")
          && qa_ui_contains(menu.controls[i].rect, c->cursor))
        hovered = &menu.controls[i];
    const qa_ui_control *control = hovered != NULL ? hovered
      : focused_control(&menu, cursor);
    if (control != NULL && kind_is(control, "list")) {
      list_layout layout = list_layout_of(c, control);
      double sign = event.delta.y > 0 ? 1 : event.delta.y < 0 ? -1 : 0;
      list_state *state = list_for(c, control);
      state->top = fmax(0, fmin(layout.maximum, layout.top - sign * 3));
    } else if (event.delta.y != 0)
      key_event(c, event.delta.y > 0 ? QA_KEY_UP : QA_KEY_DOWN, true);
  } else if (strcmp(event.kind, "controller-button") == 0) {
    static const int codes[] = { QA_KEY_ENTER, QA_KEY_ESCAPE, -1, -1, -1, -1,
      QA_KEY_ESCAPE, -1, -1, -1, -1, QA_KEY_UP, QA_KEY_DOWN, QA_KEY_LEFT,
      QA_KEY_RIGHT };
    int button = (int)event.button;
    if (button >= 0 && button < 15 && codes[button] >= 0)
      key_event(c, codes[button], event.down);
  } else if (strcmp(event.kind, "controller-axis") == 0) {
    if (strcmp(event.axis, "left-x") == 0
        || strcmp(event.axis, "left-y") == 0) {
      char id[64];
      snprintf(id, sizeof id, "%g:%s", event.device, event.axis);
      if (fabs(event.value) < 0.35) {
        for (size_t i = 0; i < c->held_count; i++)
          if (strcmp(c->held[i].id, id) == 0) {
            free(c->held[i].id);
            c->held[i] = c->held[--c->held_count];
            break;
          }
      } else if (fabs(event.value) >= 0.6) { /* held repeat arms below */
        double code = strcmp(event.axis, "left-x") == 0
          ? (event.value < 0 ? QA_KEY_LEFT : QA_KEY_RIGHT)
          : (event.value < 0 ? QA_KEY_UP : QA_KEY_DOWN);
        held_axis *held = NULL;
        for (size_t i = 0; i < c->held_count; i++)
          if (strcmp(c->held[i].id, id) == 0) held = &c->held[i];
        if (held == NULL || held->code != code) {
          key_event(c, code, true);
          if (held == NULL) {
            held_axis *grown = grow(c->held, &c->held_capacity,
                                    c->held_count + 1, sizeof *grown);
            if (grown == NULL) qa_range_error("Out of memory");
            c->held = grown;
            held = &c->held[c->held_count++];
            held->id = xstrdup(id);
            if (held->id == NULL) qa_range_error("Out of memory");
          }
          held->code = code;
          held->next = event.time_milliseconds + 300;
        }
      }
    }
  }
  return true;
}

/* ---- draw ---- */

typedef struct draw_stage {
  qa_ui_draw_command *items;
  size_t count, capacity;
} draw_stage;

static void stage_push(draw_stage *stage, qa_ui_draw_command command) {
  qa_ui_draw_command *grown = grow(stage->items, &stage->capacity,
                                   stage->count + 1, sizeof *grown);
  if (grown == NULL) qa_range_error("Out of memory");
  stage->items = grown;
  stage->items[stage->count++] = command;
}

static const char *keep_string(qa_native_ui_controller *c, char *owned) {
  if (owned == NULL) qa_range_error("Out of memory");
  char **grown = grow(c->strings, &c->string_capacity, c->string_count + 1,
                      sizeof *grown);
  if (grown == NULL) qa_range_error("Out of memory");
  c->strings = grown;
  c->strings[c->string_count++] = owned;
  return owned;
}

static const char *localized(qa_native_ui_controller *c, const char *text) {
  if (c->options.localize == NULL) return text;
  return keep_string(c, xstrdup(c->options.localize(c->options.ctx, text)));
}

static double measure(qa_native_ui_controller *c, const char *value,
                      double scale) {
  const char *text = c->options.localize != NULL
    ? c->options.localize(c->options.ctx, value) : value;
  return c->options.measure_text != NULL
    ? c->options.measure_text(c->options.ctx, text, scale)
    : (double)cp_length(value) * 8 * scale;
}

typedef struct fit_result { const char *text; double scale; } fit_result;

/* Shrink (then ellipsize) so control text never leaves its rect. */
static fit_result fit_text(qa_native_ui_controller *c, const char *value,
                           double max_width, double base) {
  double width = measure(c, value, base);
  if (width <= max_width) {
    fit_result out = { value, base };
    return out;
  }
  double shrunk = base * fmax(0, max_width) / fmax(1, width);
  if (shrunk >= base * 0.7) {
    fit_result out = { value, shrunk };
    return out;
  }
  double scale = base * 0.7;
  size_t glyphs = cp_length(value), low = 0, high = glyphs;
  while (low < high) {
    size_t middle = (low + high + 1) / 2;
    char *prefix = cp_slice(value, 0, middle);
    if (prefix == NULL) qa_range_error("Out of memory");
    char *candidate = malloc(strlen(prefix) + 4);
    if (candidate == NULL) qa_range_error("Out of memory");
    strcpy(candidate, prefix);
    strcat(candidate, "\xe2\x80\xa6"); /* … */
    free(prefix);
    double w = measure(c, candidate, scale);
    free(candidate);
    if (w <= max_width) low = middle; else high = middle - 1;
  }
  fit_result out;
  out.scale = scale;
  if (measure(c, "\xe2\x80\xa6", scale) <= max_width) {
    char *prefix = cp_slice(value, 0, low);
    if (prefix == NULL) qa_range_error("Out of memory");
    char *text = malloc(strlen(prefix) + 4);
    if (text == NULL) qa_range_error("Out of memory");
    strcpy(text, prefix);
    strcat(text, "\xe2\x80\xa6");
    free(prefix);
    out.text = keep_string(c, text);
  } else out.text = "";
  return out;
}

static qa_ui_draw_command text_command(const char *text, qa_resource_id font,
                                       double scale, double x, double y,
                                       qa_vec4 color, const char *align) {
  qa_ui_draw_command cmd;
  memset(&cmd, 0, sizeof cmd);
  cmd.kind = QA_UI_DRAW_TEXT;
  cmd.origin.x = x;
  cmd.origin.y = y;
  cmd.text = text;
  cmd.font = font;
  cmd.scale = scale;
  cmd.color = color;
  cmd.align = align;
  cmd.shadow = true;
  return cmd;
}

static qa_ui_draw_command fill_command(qa_rect rect, qa_vec4 color) {
  qa_ui_draw_command cmd;
  memset(&cmd, 0, sizeof cmd);
  cmd.kind = QA_UI_DRAW_FILL;
  cmd.rect = rect;
  cmd.color = color;
  return cmd;
}

static void push_slice(draw_stage *stage, const qa_ui_image_slice *slice,
                       qa_rect rect) {
  qa_ui_draw_command buffer[9];
  size_t count = qa_ui_nine_slice(slice, rect, WHITE, buffer, 9);
  for (size_t i = 0; i < count; i++) stage_push(stage, buffer[i]);
}

const qa_ui_draw_command *qa_native_ui_controller_draw(
    qa_native_ui_controller *c, qa_ui_draw_context context,
    size_t *out_count) {
  *out_count = 0;
  for (size_t i = 0; i < c->string_count; i++) free(c->strings[i]);
  c->string_count = 0;
  c->command_count = 0;
  if (!qa_same_seat(context.binding.seat, c->seat))
    qa_range_error("UI draw delivered to another seat");
  qa_native_ui_appearance appearance = c->options.appearance != NULL
    ? c->options.appearance(c->options.ctx)
    : (qa_native_ui_appearance){ 1, 1, false, NULL };
  c->transform = qa_ui_fit(context.binding.safe_area, appearance.menu_scale);
  if (c->has_pointer)
    c->cursor = qa_ui_point(c->pointer_position, c->transform);
  for (size_t i = 0; i < c->held_count; i++)
    if (context.time_milliseconds >= c->held[i].next) {
      key_event(c, c->held[i].code, true);
      c->held[i].next = context.time_milliseconds + 80;
    }
  qa_ui_menu menu;
  menu_cursor *cursor = NULL;
  if (!active(c, &menu, &cursor)) return c->commands;
  qa_ui_skin original = c->options.skin(c->options.ctx);
  qa_ui_skin skin = original;
  skin.font_scale = original.font_scale * appearance.text_scale;
  skin.colors = qa_ui_accessible_colors(original.colors,
                                        appearance.high_contrast,
                                        appearance.color_mode);
  draw_stage stage = { NULL, 0, 0 };
  if (menu.full_screen && skin.background != NULL) {
    qa_ui_draw_command cmd;
    memset(&cmd, 0, sizeof cmd);
    cmd.kind = QA_UI_DRAW_IMAGE;
    cmd.resource = skin.background;
    cmd.rect.x = 0; cmd.rect.y = 0;
    cmd.rect.width = 640; cmd.rect.height = 480;
    cmd.tex_coords[1].x = 1; cmd.tex_coords[1].y = 1;
    cmd.color = WHITE;
    stage_push(&stage, cmd);
  }
  qa_rect panel = { 32, 24, 576, 432 };
  stage_push(&stage, fill_command(panel, skin.colors.panel));
  if (skin.panel != NULL) push_slice(&stage, skin.panel, panel);
  if (!skin.has_title_font) {
    fit_result fit = fit_text(c, menu.title, 512, skin.font_scale);
    stage_push(&stage, text_command(localized(c, fit.text), skin.font,
                                    fit.scale, 320, 48, skin.colors.accent,
                                    "center"));
  } else {
    double title_scale = skin.has_title_scale ? skin.title_scale
                                              : skin.font_scale;
    fit_result fit = fit_text(c, menu.title, 512, title_scale);
    stage_push(&stage, text_command(localized(c, fit.text), skin.title_font,
                                    fit.scale, 64, 44, skin.colors.accent,
                                    "left"));
  }
  for (size_t ci = 0; ci < menu.control_count; ci++) {
    const qa_ui_control *control = &menu.controls[ci];
    if (!control->visible) continue;
    bool scrolled = false;
    if (menu.scroll != NULL)
      for (size_t j = 0; j < menu.scroll->control_count; j++)
        if (strcmp(menu.scroll->controls[j], control->id) == 0)
          scrolled = true;
    if (scrolled && (control->rect.y + control->rect.height
                       <= menu.scroll->rect.y
                     || control->rect.y >= menu.scroll->rect.y
                       + menu.scroll->rect.height)) continue;
    qa_ui_draw_command clip;
    memset(&clip, 0, sizeof clip);
    clip.kind = QA_UI_DRAW_CLIP;
    clip.has_clip_rect = scrolled;
    if (scrolled) clip.rect = menu.scroll->rect;
    stage_push(&stage, clip);
    bool focused = cursor->focus != NULL
      && strcmp(control->id, cursor->focus) == 0;
    qa_vec4 color = !control->enabled ? skin.colors.disabled
      : focused ? skin.colors.accent : skin.colors.text;
    if (kind_is(control, "owner-draw")) {
      qa_ui_draw_context inner = context;
      inner.binding.viewport.x = 0; inner.binding.viewport.y = 0;
      inner.binding.viewport.width = 640; inner.binding.viewport.height = 480;
      inner.binding.safe_area = inner.binding.viewport;
      size_t drawn = 0;
      const qa_ui_draw_command *commands =
        control->draw(control->ctx, inner, &drawn);
      for (size_t i = 0; i < drawn; i++) stage_push(&stage, commands[i]);
      continue;
    }
    bool focused_control = focused && !kind_is(control, "list");
    const qa_ui_image_slice *decoration = focused_control
      ? (skin.focus != NULL ? skin.focus : skin.button) : skin.button;
    qa_rect fill_rect = control->rect;
    fill_rect.height -= 2;
    stage_push(&stage, fill_command(fill_rect,
      focused_control ? skin.colors.focused : skin.colors.control));
    if (decoration != NULL) push_slice(&stage, decoration, control->rect);
    if (kind_is(control, "list")) {
      list_layout layout = list_layout_of(c, control);
      double content_width = control->rect.width
        - (layout.maximum > 0 ? 16 : 0);
      qa_rect list_clip = control->rect;
      list_clip.width = content_width;
      qa_ui_draw_command cc;
      memset(&cc, 0, sizeof cc);
      cc.kind = QA_UI_DRAW_CLIP;
      cc.has_clip_rect = true;
      cc.rect = list_clip;
      stage_push(&stage, cc);
      size_t first = (size_t)layout.top, page = (size_t)layout.page;
      for (size_t index = 0; index < page && first + index < control->row_count;
           index++) {
        const qa_ui_list_row *row = &control->rows[first + index];
        double y = control->rect.y + (double)index * layout.height;
        bool selected = control->selected != NULL
          && strcmp(row->id, control->selected) == 0;
        if (selected) {
          qa_rect sel = { control->rect.x, y, content_width, layout.height };
          stage_push(&stage, fill_command(sel, skin.colors.focused));
        }
        qa_vec4 row_color = !row->enabled ? skin.colors.disabled
          : selected ? skin.colors.accent : skin.colors.text;
        double text_y = y + (layout.height - 8 * skin.font_scale) / 2;
        if (control->column_widths.values == NULL) {
          size_t length = 1;
          for (size_t j = 0; j < row->cells.count; j++)
            length += strlen(row->cells.values[j]) + 2;
          char *joined = malloc(length);
          if (joined == NULL) qa_range_error("Out of memory");
          joined[0] = '\0';
          for (size_t j = 0; j < row->cells.count; j++) {
            if (j > 0) strcat(joined, "  ");
            strcat(joined, row->cells.values[j]);
          }
          const char *text = keep_string(c, joined);
          fit_result fit = fit_text(c, text, content_width - 16
            - (row->action == NULL ? 0 : 28), skin.font_scale);
          stage_push(&stage, text_command(localized(c, fit.text), skin.font,
            fit.scale, control->rect.x + 8,
            text_y + (8 * skin.font_scale - 8 * fit.scale) / 2, row_color,
            "left"));
        } else {
          double x = control->rect.x;
          for (size_t column = 0; column < row->cells.count; column++) {
            const char *value = row->cells.values[column];
            double column_width = column < control->column_widths.count
              ? control->column_widths.values[column] : content_width;
            double width = fmin(column_width, control->rect.x + content_width
              - (row->action == NULL ? 0 : 28) - x);
            double measured = c->options.measure_text != NULL
              ? c->options.measure_text(c->options.ctx, value,
                                        skin.font_scale)
              : (double)cp_length(value) * 8 * skin.font_scale;
            double available = fmax(0, width - 16);
            double scale = skin.font_scale
              * fmax(0.75, fmin(1, available / fmax(1, measured)));
            fit_result fit = { value, scale };
            if (measured * scale / skin.font_scale > available)
              fit = fit_text(c, value, available, scale / 0.7 * 0.7);
            stage_push(&stage, text_command(fit.text, skin.font, scale,
              x + 8, y + (layout.height - 8 * scale) / 2, row_color,
              "left"));
            x += width;
          }
        }
        if (row->action != NULL)
          stage_push(&stage, text_command(
            localized(c, row->action->label), skin.font, skin.font_scale,
            control->rect.x + content_width - 14, text_y, row_color,
            "center"));
      }
      qa_ui_draw_command unclip;
      memset(&unclip, 0, sizeof unclip);
      unclip.kind = QA_UI_DRAW_CLIP;
      stage_push(&stage, unclip);
      if (layout.maximum > 0) {
        double x = control->rect.x + control->rect.width - 14;
        qa_rect track = { x, control->rect.y, 12, control->rect.height };
        stage_push(&stage, fill_command(track, skin.colors.control));
        qa_rect thumb = { x, control->rect.y
          + (control->rect.height - layout.thumb) * layout.top
            / layout.maximum, 12, layout.thumb };
        stage_push(&stage, fill_command(thumb,
          focused ? skin.colors.accent : skin.colors.disabled));
      }
      continue;
    }
    double right = control->rect.x + control->rect.width - 10;
    double y = control->rect.y + 6;
    if (!kind_is(control, "slider")) {
      const char *value = "";
      if (kind_is(control, "toggle")) value = control->checked ? "On" : "Off";
      else if (kind_is(control, "choice")) {
        for (size_t i = 0; i < control->choice_count; i++)
          if (control->selected != NULL
              && strcmp(control->choices[i].id, control->selected) == 0)
            value = control->choices[i].label;
      }
      double value_width = 0;
      if (value[0] != '\0') {
        fit_result fit = fit_text(c, value, control->rect.width * 0.45,
                                  skin.font_scale);
        stage_push(&stage, text_command(localized(c, fit.text), skin.font,
          fit.scale, right,
          y + (8 * skin.font_scale - 8 * fit.scale) / 2, color, "right"));
        value_width = measure(c, fit.text, fit.scale);
      }
      fit_result fit = fit_text(c, control->label,
        control->rect.width - 20 - (value_width == 0 ? 0 : value_width + 10),
        skin.font_scale);
      stage_push(&stage, text_command(localized(c, fit.text), skin.font,
        fit.scale, control->rect.x + 10,
        y + (8 * skin.font_scale - 8 * fit.scale) / 2, color, "left"));
    }
    if (kind_is(control, "slider")) {
      double x = control->rect.x + control->rect.width * 0.6;
      double width = control->rect.width * 0.32;
      char buffer[64];
      const char *value = control->value_label;
      if (value == NULL) {
        double rounded = round(control->value * 1e6) / 1e6;
        snprintf(buffer, sizeof buffer, "%g", rounded);
        value = buffer;
      }
      double value_right = x - 12;
      double measured_value = c->options.measure_text != NULL
        ? c->options.measure_text(c->options.ctx, value, skin.font_scale)
        : (double)cp_length(value) * 8 * skin.font_scale;
      double measured_label = c->options.measure_text != NULL
        ? c->options.measure_text(c->options.ctx, control->label,
                                  skin.font_scale)
        : (double)cp_length(control->label) * 8 * skin.font_scale;
      double value_scale = skin.font_scale
        * fmin(1, control->rect.width * 0.16 / fmax(1, measured_value));
      double label_width = value_right - measured_value * value_scale
        / skin.font_scale - 12 - (control->rect.x + 10);
      double label_scale = skin.font_scale
        * fmin(1, fmax(1, label_width) / fmax(1, measured_label));
      stage_push(&stage, text_command(control->label, skin.font, label_scale,
                                      control->rect.x + 10, y, color,
                                      "left"));
      stage_push(&stage, text_command(value == buffer
          ? keep_string(c, xstrdup(buffer)) : value,
        skin.font, value_scale, value_right, y, color, "right"));
      double ratio = control->maximum == control->minimum ? 0
        : fmax(0, fmin(1, (control->value - control->minimum)
                         / (control->maximum - control->minimum)));
      qa_rect track = { x, y + 7, width, 2 };
      stage_push(&stage, fill_command(track, skin.colors.disabled));
      qa_rect knob = { x + width * ratio - 3, y + 2, 6, 12 };
      stage_push(&stage, fill_command(knob, color));
    } else if (kind_is(control, "text-entry")) {
      field_cursor *field = field_for(c, control);
      size_t length = cp_length(control->text);
      char *all = malloc(strlen(control->text) + 1);
      if (all == NULL) qa_range_error("Out of memory");
      if (control->has_masked && control->masked) {
        for (size_t i = 0; i < length; i++) all[i] = '*';
        all[length] = '\0';
      } else strcpy(all, control->text);
      double available = control->rect.width * 0.5 - 12;
      size_t start = field->start < field->cursor ? field->start
                                                   : field->cursor;
      size_t end = field->cursor;
      for (;;) {
        if (start >= field->cursor) break;
        char *prefix = cp_slice(all, start, field->cursor);
        if (prefix == NULL) qa_range_error("Out of memory");
        double w = c->options.measure_text != NULL
          ? c->options.measure_text(c->options.ctx, prefix, skin.font_scale)
          : (double)cp_length(prefix) * 8 * skin.font_scale;
        free(prefix);
        if (w <= available) break;
        start++;
      }
      for (;;) {
        if (end >= length) break;
        char *prefix = cp_slice(all, start, end + 1);
        if (prefix == NULL) qa_range_error("Out of memory");
        double w = c->options.measure_text != NULL
          ? c->options.measure_text(c->options.ctx, prefix, skin.font_scale)
          : (double)cp_length(prefix) * 8 * skin.font_scale;
        free(prefix);
        if (w > available) break;
        end++;
      }
      field->start = start;
      char *shown = cp_slice(all, start, end);
      if (shown == NULL) qa_range_error("Out of memory");
      double x = control->rect.x + control->rect.width * 0.5;
      stage_push(&stage, text_command(localized(c, keep_string(c, shown)),
                                      skin.font, skin.font_scale, x, y,
                                      color, "left"));
      if (focused
          && (int)floor(context.time_milliseconds / 256) % 2 == 0) {
        char *prefix = cp_slice(all, start, field->cursor);
        if (prefix == NULL) qa_range_error("Out of memory");
        double w = c->options.measure_text != NULL
          ? c->options.measure_text(c->options.ctx, prefix, skin.font_scale)
          : (double)cp_length(prefix) * 8 * skin.font_scale;
        free(prefix);
        stage_push(&stage, text_command(field->overstrike ? "_" : "|",
                                        skin.font, skin.font_scale, x + w, y,
                                        color, "left"));
      }
      free(all);
    }
  }
  qa_ui_draw_command unclip;
  memset(&unclip, 0, sizeof unclip);
  unclip.kind = QA_UI_DRAW_CLIP;
  stage_push(&stage, unclip);
  if (menu.scroll != NULL
      && menu.scroll->content_height > menu.scroll->rect.height) {
    double thumb = menu_thumb(&menu);
    double x = menu.scroll->rect.x + menu.scroll->rect.width - 14;
    qa_rect track = { x, menu.scroll->rect.y, 12, menu.scroll->rect.height };
    stage_push(&stage, fill_command(track, skin.colors.control));
    qa_rect thumb_rect = { x, menu.scroll->rect.y
      + (menu.scroll->rect.height - thumb) * cursor->scroll
        / (menu.scroll->content_height - menu.scroll->rect.height),
      12, thumb };
    stage_push(&stage, fill_command(thumb_rect, skin.colors.accent));
  }
  if (c->has_capture)
    stage_push(&stage, text_command(
      localized(c, "Press key/button. Esc cancels."), skin.font,
      skin.font_scale, 380, 432, skin.colors.accent, "center"));
  /* Transform everything into drawable pixels, clipped to safeArea. */
  qa_ui_draw_command head;
  memset(&head, 0, sizeof head);
  head.kind = QA_UI_DRAW_CLIP;
  head.has_clip_rect = true;
  head.rect = context.binding.safe_area;
  qa_ui_draw_command *grown = grow(c->commands, &c->command_capacity,
                                   stage.count + 2, sizeof *grown);
  if (grown == NULL) qa_range_error("Out of memory");
  c->commands = grown;
  c->commands[c->command_count++] = head;
  for (size_t i = 0; i < stage.count; i++) {
    qa_ui_draw_command transformed =
      qa_ui_transform_command(stage.items[i], c->transform);
    if (transformed.kind == QA_UI_DRAW_CLIP) {
      transformed.has_clip_rect = true;
      transformed.rect = stage.items[i].has_clip_rect
        ? qa_ui_intersect(context.binding.safe_area, transformed.rect)
        : context.binding.safe_area;
    }
    c->commands[c->command_count++] = transformed;
  }
  qa_ui_draw_command tail;
  memset(&tail, 0, sizeof tail);
  tail.kind = QA_UI_DRAW_CLIP;
  c->commands[c->command_count++] = tail;
  free(stage.items);
  *out_count = c->command_count;
  return c->commands;
}
