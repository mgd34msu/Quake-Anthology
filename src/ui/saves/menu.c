/* Port of src/ui/saves/menu.ts. GPL-2.0-or-later. */
#include "qa/ui/saves/menu.h"

#include <stdarg.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/ui/common/controls.h"
#include "qa/ui/common/layout.h"

typedef struct saves_state saves_state;
typedef struct sctx {
  saves_state *state;
  int action;
  size_t slot;
} sctx;
struct saves_state {
  qa_native_ui_controller *controller;
  const qa_ui_saved_game_menu_service *service; /* NULL = TS undefined */
  long page;
  char *draft;
  char *selected; /* NULL = TS null */
  bool busy;
  bool disposed; /* menus unregistered while an operation is pending */
  char *message;
  int handles[4];
  qa_ui_control controls[16];
  sctx ctxs[16];
  char **pool;
  size_t pool_count, pool_capacity;
  /* Captured slot rows for the current factory's slot buttons. */
  char *slot_ids[5];
  char *slot_labels[5];
  char *slot_unavailable[5];
};

enum { S_NONE, S_NEW, S_SAVE_SLOT, S_LOAD_SLOT, S_PREVIOUS, S_NEXT, S_REFRESH,
       S_BACK, S_NAME, S_WRITE, S_SELECTED };

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
static char *pool_own(saves_state *s, char *copy) {
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
static char *pool_dup(saves_state *s, const char *text) {
  return pool_own(s, qa_input_dup(text));
}
static char *pool_format(saves_state *s, const char *format, ...) {
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
static void clear_pool(saves_state *s) {
  for (size_t i = 0; i < s->pool_count; i++) free(s->pool[i]);
  s->pool_count = 0;
}

static const char *reason(saves_state *s, bool saving) {
  if (s->service != NULL) {
    const char *value =
        s->service->unavailable(s->service->ctx, saving ? "save" : "load");
    if (value != NULL) return value;
    return NULL;
  }
  return "Remote games cannot be saved or loaded here.";
}

/* TS run(operation): busy guard, "Working...", then message = "" on
 * resolve or the rejection's message; busy until the operation settles. */
static void saves_free(saves_state *s);
static void run_finish(saves_state *s, const char *error) {
  set_str(&s->message, error == NULL ? "" : error);
  s->busy = false;
  if (s->disposed) saves_free(s);
}
static bool run_begin(saves_state *s) {
  if (s->busy) return false;
  s->busy = true;
  set_str(&s->message, "Working...");
  return true;
}
static void settled_finish(void *ctx, const char *error) { run_finish(ctx, error); }
static qa_ui_saves_settle finish_settle(saves_state *s) {
  qa_ui_saves_settle settle = { settled_finish, s };
  return settle;
}
/* refresh: run(async () => { await service?.refresh(); }) */
static void do_refresh(saves_state *s) {
  if (!run_begin(s)) return;
  if (s->service == NULL) { run_finish(s, NULL); return; }
  s->service->refresh(s->service->ctx, finish_settle(s));
}
/* write: run(async () => { if (service === undefined) throw ...; await service.save(draft, selected);
 * await service.refresh(); controller.closeMenu(); message = "Game saved."; }) */
static void write_refreshed(void *ctx, const char *error) {
  saves_state *s = ctx;
  if (error != NULL) { run_finish(s, error); return; }
  if (!s->disposed) qa_native_ui_controller_close_menu(s->controller);
  set_str(&s->message, "Game saved.");
  run_finish(s, NULL);
}
static void write_saved(void *ctx, const char *error) {
  saves_state *s = ctx;
  if (error != NULL) { run_finish(s, error); return; }
  qa_ui_saves_settle settle = { write_refreshed, s };
  s->service->refresh(s->service->ctx, settle);
}
static void do_write(saves_state *s) {
  if (!run_begin(s)) return;
  if (s->service == NULL) {
    const char *value = reason(s, true);
    run_finish(s, value != NULL ? value : "Saving unavailable");
    return;
  }
  qa_ui_saves_settle settle = { write_saved, s };
  s->service->save(s->service->ctx, s->draft, s->selected, settle);
}
/* load: run(async () => { if (service !== undefined) await service.load(row.id); }) */
static void do_load(saves_state *s, const char *id) {
  if (!run_begin(s)) return;
  if (s->service == NULL) { run_finish(s, NULL); return; }
  s->service->load(s->service->ctx, id, finish_settle(s));
}

static void on_activate(void *v, qa_seat_id seat) {
  (void)seat;
  sctx *c = v;
  saves_state *s = c->state;
  switch (c->action) {
    case S_NEW: {
      set_nullable(&s->selected, NULL);
      qa_ui_startup_save_list list = s->service != NULL
                                         ? s->service->list(s->service->ctx)
                                         : (qa_ui_startup_save_list){ NULL, 0,
                                                                      NULL };
      long index = 1;
      for (;;) {
        char label[32];
        snprintf(label, sizeof label, "Save %03ld", index);
        bool exists = false;
        for (size_t i = 0; i < list.row_count; i++)
          if (strcmp(list.rows[i].label, label) == 0) exists = true;
        if (!exists) {
          set_str(&s->draft, label);
          break;
        }
        index++;
      }
      qa_native_ui_controller_open_menu(s->controller, "menu:saves:name");
      break;
    }
    case S_SAVE_SLOT: {
      const char *unavailable = s->slot_unavailable[c->slot];
      if (unavailable != NULL) {
        set_str(&s->message, unavailable);
        break;
      }
      set_nullable(&s->selected, s->slot_ids[c->slot]);
      set_str(&s->draft, s->slot_labels[c->slot]);
      qa_native_ui_controller_open_menu(s->controller,
                                        "menu:saves:overwrite");
      break;
    }
    case S_LOAD_SLOT: {
      const char *unavailable = s->slot_unavailable[c->slot];
      if (unavailable != NULL) {
        set_str(&s->message, unavailable);
        break;
      }
      do_load(s, s->slot_ids[c->slot]);
      break;
    }
    case S_PREVIOUS: case S_NEXT: {
      qa_ui_startup_save_list list = s->service != NULL
                                         ? s->service->list(s->service->ctx)
                                         : (qa_ui_startup_save_list){ NULL, 0,
                                                                      NULL };
      long pages = (long)ceil((double)list.row_count / 5);
      if (pages < 1) pages = 1;
      if (c->action == S_PREVIOUS)
        s->page = (s->page + pages - 1) % pages;
      else
        s->page = (s->page + 1) % pages;
      break;
    }
    case S_REFRESH: do_refresh(s); break;
    case S_WRITE: do_write(s); break;
    case S_BACK: qa_native_ui_controller_close_menu(s->controller); break;
    default: break;
  }
}
static void on_change_name(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  set_str(&((sctx *)v)->state->draft, value);
}
static void on_submit_name(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  (void)value;
  do_write(((sctx *)v)->state);
}
static void on_open_rows(void *v, qa_seat_id seat) {
  (void)seat;
  saves_state *s = v;
  s->page = 0;
  do_refresh(s);
}
static void on_noop(void *v, qa_seat_id seat) {
  (void)v;
  (void)seat;
}

static qa_ui_control *next_control(saves_state *s, size_t *at, int action,
                                   size_t slot) {
  qa_ui_control *control = &s->controls[*at];
  memset(control, 0, sizeof *control);
  s->ctxs[*at].state = s;
  s->ctxs[*at].action = action;
  s->ctxs[*at].slot = slot;
  control->ctx = &s->ctxs[*at];
  control->visible = true;
  (*at)++;
  return control;
}
static void add_button(saves_state *s, size_t *at, const char *id,
                       const char *label, double row, int action, size_t slot,
                       bool enabled) {
  qa_ui_control *control = next_control(s, at, action, slot);
  control->id = id;
  control->kind = "button";
  control->label = label;
  control->rect = qa_ui_menu_row_default(row);
  control->enabled = enabled && !s->busy;
  control->activate_seat = on_activate;
}
/* TS info(): wrapWords(label, 48), at most 2 rows. */
static void add_info(saves_state *s, size_t *at, const char *label) {
  size_t line_count = 0;
  char **lines = qa_ui_wrap_words(label, 48, &line_count);
  size_t shown = line_count < 2 ? line_count : 2;
  for (size_t i = 0; i < shown; i++)
    add_button(s, at, pool_format(s, "ui:saves:message:%zu", i), pool_dup(s, lines[i]),
               9 + (double)i, S_NONE, 0, false);
  qa_ui_wrapped_lines_free(lines, line_count);
}
static void add_back(saves_state *s, size_t *at) {
  add_button(s, at, "ui:saves:back", "Cancel", 11, S_BACK, 0, true);
}

static size_t build_rows(saves_state *s, bool saving, qa_ui_control *controls,
                         size_t *at_io) {
  (void)controls;
  size_t at = *at_io;
  qa_ui_startup_save_list list =
      s->service != NULL ? s->service->list(s->service->ctx)
                         : (qa_ui_startup_save_list){ NULL, 0, NULL };
  long pages = (long)ceil((double)list.row_count / 5);
  if (pages < 1) pages = 1;
  if (s->page > pages - 1) s->page = pages - 1;
  if (s->page < 0) s->page = 0;
  /* TS builds slot buttons first, then unshifts "new" when saving;
   * the C array is built in final order with the same rows. */
  if (saving)
    add_button(s, &at, "ui:saves:new", "New saved game", 0, S_NEW, 0,
               reason(s, true) == NULL);
  size_t first = (size_t)s->page * 5;
  size_t shown = 0;
  for (size_t i = first; i < list.row_count && shown < 5; i++) {
    const qa_ui_startup_save_row *row = &list.rows[i];
    set_nullable(&s->slot_ids[shown], row->id);
    set_nullable(&s->slot_labels[shown], row->label);
    set_nullable(&s->slot_unavailable[shown], row->unavailable);
    const char *label = pool_format(
        s, "%s - %s", row->label,
        row->unavailable == NULL ? row->map : "unavailable");
    saves_state * s2 = s;
    size_t * at2 = &at;
    const char * id2 = pool_format(s, "ui:saves:slot:%s", row->id);
    double row2 = (double)shown + 1;
    int action = saving ? S_SAVE_SLOT : S_LOAD_SLOT;
    size_t slot2 = shown;
    bool enabled = reason(s, saving) == NULL;
    add_button(s2, at2, id2, label,
               row2, action, slot2, enabled);
    shown++;
  }
  if (pages > 1) {
    add_button(s, &at, "ui:saves:previous", "Previous page", 6, S_PREVIOUS, 0,
               true);
    add_button(s, &at, "ui:saves:next",
               pool_format(s, "Next page (%ld/%ld)", s->page + 1, pages), 7,
               S_NEXT, 0, true);
  }
  add_button(s, &at, "ui:saves:refresh", "Refresh", 8, S_REFRESH, 0, true);
  const char *info = s->message;
  if (info[0] == '\0') info = reason(s, saving);
  if (info == NULL || info[0] == '\0') info = list.error;
  if (info == NULL || info[0] == '\0')
    info = list.row_count == 0 ? "No saved games yet." : "";
  add_info(s, &at, info);
  add_back(s, &at);
  *at_io = at;
  return at;
}

static qa_ui_menu make_menu(saves_state *s, const char *id, const char *title,
                            size_t count, void (*open)(void *, qa_seat_id)) {
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = id;
  menu.title = title;
  menu.full_screen = true;
  menu.controls = s->controls;
  menu.control_count = count;
  menu.ctx = s;
  menu.open = open;
  menu.close = on_noop;
  return menu;
}
static qa_ui_menu save_factory(void *v) {
  saves_state *s = v;
  clear_pool(s);
  size_t at = 0;
  build_rows(s, true, s->controls, &at);
  return make_menu(s, "menu:saves:save", "Save game", at, on_open_rows);
}
static qa_ui_menu load_factory(void *v) {
  saves_state *s = v;
  clear_pool(s);
  size_t at = 0;
  build_rows(s, false, s->controls, &at);
  return make_menu(s, "menu:saves:load", "Load game", at, on_open_rows);
}
static qa_ui_menu name_factory(void *v) {
  saves_state *s = v;
  clear_pool(s);
  size_t at = 0;
  qa_ui_control *name = next_control(s, &at, S_NAME, 0);
  name->id = "ui:saves:name";
  name->kind = "text-entry";
  name->label = "Name";
  name->text = s->draft;
  name->maximum_length = 48;
  name->rect = qa_ui_menu_row_default(1);
  name->enabled = !s->busy;
  name->change_text = on_change_name;
  name->submit_text = on_submit_name;
  add_button(s, &at, "ui:saves:write", "Save game", 3, S_WRITE, 0, true);
  add_info(s, &at, s->message);
  add_back(s, &at);
  return make_menu(s, "menu:saves:name", "New saved game", at, on_noop);
}
static qa_ui_menu confirm_factory(void *v) {
  saves_state *s = v;
  clear_pool(s);
  size_t at = 0;
  add_button(s, &at, "ui:saves:selected", s->draft, 1, S_SELECTED, 0, false);
  add_button(s, &at, "ui:saves:overwrite", "Overwrite this saved game", 3,
             S_WRITE, 0, true);
  add_info(s, &at, s->message);
  add_back(s, &at);
  return make_menu(s, "menu:saves:overwrite", "Overwrite saved game?", at,
                   on_noop);
}

static void saves_free(saves_state *s) {
  clear_pool(s);
  free(s->pool);
  for (size_t i = 0; i < 5; i++) {
    free(s->slot_ids[i]);
    free(s->slot_labels[i]);
    free(s->slot_unavailable[i]);
  }
  free(s->draft);
  free(s->selected);
  free(s->message);
  free(s);
}
static void saves_dispose(qa_ui_saved_game_menus *menus) {
  saves_state *s = menus->state;
  if (s == NULL) return;
  for (int i = 3; i >= 0; i--)
    qa_native_ui_controller_unregister(s->controller, s->handles[i]);
  menus->state = NULL;
  /* A pending operation's settle still runs its continuation (TS GC keeps
   * the closures); the state is freed when it settles. */
  if (s->busy) s->disposed = true;
  else saves_free(s);
}
void qa_ui_saved_game_menus_dispose(qa_ui_saved_game_menus *menus) {
  if (menus == NULL || menus->state == NULL) return;
  if (menus->dispose_fn != NULL &&
      menus->dispose_fn != qa_ui_saved_game_menus_dispose) {
    menus->dispose_fn(menus);
    return;
  }
  saves_dispose(menus);
}

qa_ui_saved_game_menus qa_ui_register_saved_game_menus(
    qa_native_ui_controller *controller,
    const qa_ui_saved_game_menu_service *service) {
  saves_state *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->controller = controller;
  s->service = service;
  set_str(&s->draft, "Save 001");
  set_str(&s->message, "");
  const char *ids[4] = { "menu:saves:save", "menu:saves:load",
                         "menu:saves:name", "menu:saves:overwrite" };
  qa_ui_menu_factory_fn factories[4] = { save_factory, load_factory,
                                         name_factory, confirm_factory };
  for (size_t i = 0; i < 4; i++) {
    int handle =
        qa_native_ui_controller_register(controller, ids[i], factories[i], s);
    if (handle < 0) qa_range_error("Out of memory");
    s->handles[i] = handle;
  }
  qa_ui_saved_game_menus menus = { "menu:saves:save", "menu:saves:load", s,
                                   saves_dispose };
  return menus;
}
