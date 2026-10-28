/* Port of src/ui/settings/local-lobby.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/local_lobby.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/ui/common/layout.h"

typedef struct lobby_state lobby_state;
typedef struct lctx { lobby_state *state; int action; size_t slot; } lctx;
struct lobby_state {
  qa_native_ui_controller *controller;
  const qa_ui_application_local_lobby *(*current)(void *);
  void *current_ctx;
  const char *(*host_selection)(void *, qa_ui_local_lobby_selection *);
  void *host_selection_ctx;
  double (*read_seats)(void *);
  void *read_seats_ctx;
  char *name;
  double capacity;
  bool busy;
  char *status;
  long page;
  long generation;
  int handle;
  qa_ui_control controls[14];
  lctx ctxs[14];
  char *labels[14];
  char *ids[14];
  char *targets[14]; /* join lobby ids, per slot */
  /* Factory-time snapshot for the ready action. */
  bool ready_value;
};

enum { N_NONE, N_NAME, N_CAPACITY, N_HOST, N_JOIN, N_PREV, N_NEXT, N_READY,
       N_START, N_LEAVE, N_MEMBERS, N_BACK };

static void set_str(char **slot, const char *value) {
  free(*slot);
  *slot = qa_input_dup(value);
  if (*slot == NULL) qa_range_error("Out of memory");
}
static char *pool(char **slot, const char *text) {
  *slot = qa_input_dup(text);
  if (*slot == NULL) qa_range_error("Out of memory");
  return *slot;
}
static void clear_pool(lobby_state *s) {
  for (size_t i = 0; i < 14; i++) {
    free(s->labels[i]);
    free(s->ids[i]);
    free(s->targets[i]);
    s->labels[i] = NULL;
    s->ids[i] = NULL;
    s->targets[i] = NULL;
  }
}
static bool blank_trim(const char *text) {
  char *trimmed = qa_input_js_trim(text);
  if (trimmed == NULL) qa_range_error("Out of memory");
  bool empty = trimmed[0] == '\0';
  free(trimmed);
  return empty;
}
static char *js_num(double value) {
  char *text = qa_input_js_number(value);
  if (text == NULL) qa_range_error("Out of memory");
  return text;
}

/* Synchronous run(): op returns an error message or NULL. */
typedef const char *(*lobby_op)(lobby_state *s,
                                const qa_ui_application_local_lobby *owner,
                                const char *arg);
static void run(lobby_state *s, lobby_op op, const char *arg) {
  if (s->busy) return;
  const qa_ui_application_local_lobby *owner = s->current(s->current_ctx);
  if (owner == NULL) return;
  long request = s->generation;
  s->busy = true;
  set_str(&s->status, "");
  const char *error = op(s, owner, arg);
  if (request == s->generation) {
    if (error != NULL)
      set_str(&s->status, error[0] != '\0'
                               ? error
                               : "Local lobby operation failed.");
    s->busy = false;
  }
}
static const char *op_host(lobby_state *s,
                           const qa_ui_application_local_lobby *owner,
                           const char *arg) {
  (void)arg;
  qa_ui_local_lobby_selection selection;
  memset(&selection, 0, sizeof selection);
  const char *error =
      s->host_selection(s->host_selection_ctx, &selection);
  if (error != NULL) return error;
  return owner->host(owner->ctx, s->name, s->capacity, selection,
                     s->read_seats(s->read_seats_ctx));
}
static const char *op_join(lobby_state *s,
                           const qa_ui_application_local_lobby *owner,
                           const char *arg) {
  return owner->join(owner->ctx, arg, s->read_seats(s->read_seats_ctx));
}
static const char *op_ready(lobby_state *s,
                            const qa_ui_application_local_lobby *owner,
                            const char *arg) {
  (void)arg;
  return owner->ready(owner->ctx, s->ready_value);
}
static const char *op_start(lobby_state *s,
                            const qa_ui_application_local_lobby *owner,
                            const char *arg) {
  (void)s;
  (void)arg;
  return owner->start(owner->ctx);
}
static const char *op_leave(lobby_state *s,
                            const qa_ui_application_local_lobby *owner,
                            const char *arg) {
  (void)s;
  (void)arg;
  return owner->leave(owner->ctx);
}

static void on_activate(void *v, qa_seat_id seat) {
  (void)seat;
  lctx *c = v;
  lobby_state *s = c->state;
  switch (c->action) {
    case N_HOST: run(s, op_host, NULL); break;
    case N_JOIN:
      if (s->targets[c->slot] != NULL) run(s, op_join, s->targets[c->slot]);
      break;
    case N_PREV: s->page--; break;
    case N_NEXT: s->page++; break;
    case N_READY: run(s, op_ready, NULL); break;
    case N_START: run(s, op_start, NULL); break;
    case N_LEAVE: run(s, op_leave, NULL); break;
    case N_MEMBERS: {
      const qa_ui_application_local_lobby *owner =
          s->current(s->current_ctx);
      const qa_lobby *lobby = owner != NULL ? owner->current(owner->ctx)
                                            : NULL;
      if (lobby != NULL) {
        long pages = (long)((lobby->member_count + 3) / 4);
        if (pages < 1) pages = 1;
        s->page = (s->page + 1) % pages;
      }
      break;
    }
    case N_BACK:
      qa_native_ui_controller_close_menu(s->controller);
      break;
    default: break;
  }
}
static void on_change_text(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  set_str(&((lctx *)v)->state->name, value);
}
static void on_change_value(void *v, qa_seat_id seat, double value) {
  (void)seat;
  ((lctx *)v)->state->capacity = value;
}
static void on_submit_noop(void *v, qa_seat_id seat, const char *value) {
  (void)v;
  (void)seat;
  (void)value;
}
static void no_op_open(void *v, qa_seat_id seat) { (void)v; (void)seat; }

static qa_ui_control button(lobby_state *s, size_t slot, const char *id,
                            const char *label, double row, int action,
                            bool enabled) {
  qa_ui_control control;
  memset(&control, 0, sizeof control);
  control.id = id;
  control.kind = "button";
  control.label = label;
  control.rect = qa_ui_menu_row_default(row);
  control.visible = true;
  control.enabled = enabled;
  s->ctxs[slot].state = s;
  s->ctxs[slot].action = action;
  s->ctxs[slot].slot = slot;
  control.ctx = &s->ctxs[slot];
  control.activate_seat = on_activate;
  return control;
}

static qa_ui_menu factory(void *v) {
  lobby_state *s = v;
  clear_pool(s);
  const qa_ui_application_local_lobby *owner = s->current(s->current_ctx);
  const qa_lobby *lobby = owner != NULL ? owner->current(owner->ctx) : NULL;
  qa_ui_control *controls = s->controls;
  size_t at = 0;
  if (owner == NULL) {
    controls[at] = button(s, at, "ui:lobby:unavailable",
                          "No local lobby service is configured.", 0,
                          N_NONE, false);
    at++;
  } else if (lobby == NULL) {
    size_t total = 0;
    const qa_lobby *const *all = owner->list(owner->ctx, &total);
    /* Open lobbies only, in list order (TS filter). */
    size_t open_count = 0;
    for (size_t i = 0; i < total; i++)
      if (strcmp(all[i]->phase, "open") == 0) open_count++;
    long max_page = (long)((open_count + 3) / 4) - 1;
    if (max_page < 0) max_page = 0;
    if (s->page > max_page) s->page = max_page;
    if (s->page < 0) s->page = 0;
    qa_ui_control name;
    memset(&name, 0, sizeof name);
    name.id = "ui:lobby:name";
    name.kind = "text-entry";
    name.label = "Lobby name";
    name.rect = qa_ui_menu_row_default(0);
    name.visible = true;
    name.enabled = !s->busy;
    name.text = s->name;
    name.maximum_length = 64;
    s->ctxs[at].state = s;
    s->ctxs[at].action = N_NAME;
    s->ctxs[at].slot = at;
    name.ctx = &s->ctxs[at];
    name.change_text = on_change_text;
    name.submit_text = on_submit_noop;
    controls[at++] = name;
    qa_ui_control capacity_ctl;
    memset(&capacity_ctl, 0, sizeof capacity_ctl);
    capacity_ctl.id = "ui:lobby:capacity";
    capacity_ctl.kind = "slider";
    capacity_ctl.label = "Player capacity";
    capacity_ctl.rect = qa_ui_menu_row_default(1);
    capacity_ctl.visible = true;
    capacity_ctl.enabled = !s->busy;
    capacity_ctl.value = s->capacity;
    capacity_ctl.minimum = 1;
    capacity_ctl.maximum = 64;
    capacity_ctl.step = 1;
    s->ctxs[at].state = s;
    s->ctxs[at].action = N_CAPACITY;
    s->ctxs[at].slot = at;
    capacity_ctl.ctx = &s->ctxs[at];
    capacity_ctl.change_value = on_change_value;
    controls[at++] = capacity_ctl;
    controls[at] = button(s, at, "ui:lobby:host", "Host selected game", 2,
                          N_HOST, !s->busy && !blank_trim(s->name));
    at++;
    size_t shown = 0, skipped = 0;
    size_t skip = (size_t)s->page * 4;
    for (size_t i = 0; i < total && shown < 4; i++) {
      if (strcmp(all[i]->phase, "open") != 0) continue;
      if (skipped < skip) {
        skipped++;
        continue;
      }
      double seats = 0;
      for (size_t m = 0; m < all[i]->member_count; m++)
        seats += all[i]->members[m].seats;
      char *seat_text = js_num(seats);
      char *cap_text = js_num(all[i]->capacity);
      char label[1024], id[64];
      snprintf(label, sizeof label, "Join %s (%s/%s)", all[i]->name,
               seat_text, cap_text);
      free(seat_text);
      free(cap_text);
      snprintf(id, sizeof id, "ui:lobby:join-%zu", shown);
      pool(&s->targets[at], all[i]->id);
      controls[at] = button(s, at, pool(&s->ids[at], id),
                            pool(&s->labels[at], label), 3 + (double)shown,
                            N_JOIN, !s->busy);
      at++;
      shown++;
    }
    controls[at] = button(s, at, "ui:lobby:previous", "Previous lobbies", 7,
                          N_PREV, !s->busy && s->page > 0);
    at++;
    controls[at] = button(
        s, at, "ui:lobby:next", "Next lobbies", 8, N_NEXT,
        !s->busy && (size_t)(s->page + 1) * 4 < open_count);
    at++;
  } else {
    const qa_lobby_member *member = NULL;
    for (size_t i = 0; i < lobby->member_count; i++)
      if (strcmp(lobby->members[i].account.id, owner->account_id) == 0)
        member = &lobby->members[i];
    bool host = strcmp(lobby->owner, owner->account_id) == 0;
    s->ready_value = member == NULL || !member->ready;
    char label[1024];
    snprintf(label, sizeof label, "%s: %s", lobby->name,
             strcmp(lobby->phase, "playing") == 0
                 ? "Playing"
                 : strcmp(lobby->phase, "starting") == 0
                       ? "Starting host"
                       : "Waiting for readiness");
    controls[at] = button(s, at, "ui:lobby:phase",
                          pool(&s->labels[at], label), 0, N_NONE, false);
    at++;
    long pages = (long)((lobby->member_count + 3) / 4);
    if (pages < 1) pages = 1;
    if (s->page > pages - 1) s->page = pages - 1;
    if (s->page < 0) s->page = 0;
    size_t first = (size_t)s->page * 4;
    for (size_t i = first; i < lobby->member_count && i < first + 4; i++) {
      const qa_lobby_member *entry = &lobby->members[i];
      char *seat_text = js_num(entry->seats);
      char id[64];
      snprintf(label, sizeof label, "%s: %s (%s seat%s)",
               entry->account.name, entry->ready ? "Ready" : "Not ready",
               seat_text, entry->seats == 1 ? "" : "s");
      free(seat_text);
      snprintf(id, sizeof id, "ui:lobby:member-%zu", i - first);
      controls[at] = button(s, at, pool(&s->ids[at], id),
                            pool(&s->labels[at], label),
                            1 + (double)(i - first), N_NONE, false);
      at++;
    }
    bool all_ready = true;
    for (size_t i = 0; i < lobby->member_count; i++)
      if (!lobby->members[i].ready) all_ready = false;
    bool open = strcmp(lobby->phase, "open") == 0;
    controls[at] = button(
        s, at, "ui:lobby:ready",
        member != NULL && member->ready ? "Not ready" : "Ready", 5, N_READY,
        !s->busy && open);
    at++;
    controls[at] = button(
        s, at, "ui:lobby:start",
        lobby->match_generation == 0 ? "Start match" : "Start next match",
        6, N_START, !s->busy && host && open && all_ready);
    at++;
    controls[at] = button(s, at, "ui:lobby:leave",
                          host ? "Close lobby" : "Leave lobby", 7, N_LEAVE,
                          !s->busy);
    at++;
    controls[at] = button(s, at, "ui:lobby:members", "More members", 8,
                          N_MEMBERS, !s->busy && pages > 1);
    at++;
  }
  controls[at] = button(
      s, at, "ui:lobby:status",
      s->status[0] != '\0'
          ? s->status
          : s->busy ? "Working..."
                    : "Local shared service; not a retail platform lobby.",
      9, N_NONE, false);
  at++;
  controls[at] = button(s, at, "ui:lobby:back", "Back", 11, N_BACK, !s->busy);
  at++;
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = "menu:local:lobby";
  menu.title = "Local lobbies";
  menu.full_screen = false;
  menu.controls = controls;
  menu.control_count = at;
  menu.ctx = s;
  menu.open = no_op_open;
  menu.close = no_op_open;
  return menu;
}

static void lobby_dispose(qa_ui_settings_menus *menus) {
  lobby_state *s = menus->state;
  if (s == NULL) return;
  s->generation++;
  qa_native_ui_controller_unregister(s->controller, s->handle);
  clear_pool(s);
  free(s->name);
  free(s->status);
  free(s);
  menus->state = NULL;
}

qa_ui_settings_menus qa_ui_register_local_lobby_menu(
    qa_native_ui_controller *controller,
    const qa_ui_application_local_lobby *(*current)(void *ctx),
    void *current_ctx,
    const char *(*host_selection)(void *ctx,
                                  qa_ui_local_lobby_selection *out),
    void *host_selection_ctx, double (*read_seats)(void *ctx),
    void *read_seats_ctx) {
  lobby_state *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->controller = controller;
  s->current = current;
  s->current_ctx = current_ctx;
  s->host_selection = host_selection;
  s->host_selection_ctx = host_selection_ctx;
  s->read_seats = read_seats;
  s->read_seats_ctx = read_seats_ctx;
  s->capacity = 4;
  set_str(&s->name, "Local lobby");
  set_str(&s->status, "");
  int handle = qa_native_ui_controller_register(controller,
                                                "menu:local:lobby",
                                                factory, s);
  if (handle < 0) qa_range_error("Out of memory");
  s->handle = handle;
  qa_ui_settings_menus menus = { "menu:local:lobby", s, lobby_dispose };
  return menus;
}
