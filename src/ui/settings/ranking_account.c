/* Port of src/ui/settings/ranking-account.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/ranking_account.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/ui/common/controls.h"
#include "qa/ui/common/layout.h"

typedef struct account_state account_state;
typedef struct actx { account_state *state; int action; } actx;
struct account_state {
  qa_native_ui_controller *controller;
  const qa_ui_ranking_account_actions *(*current)(void *);
  void *ctx;
  char *username, *password, *email, *message;
  bool create, busy;
  long generation;
  int handle;
  qa_ui_control controls[16];
  char *labels[16];
  char *ids[16];
  char status_buffer[1024];
  actx ctxs[16];
};

static void set_str(char **slot, const char *value) {
  free(*slot);
  *slot = qa_input_dup(value);
  if (*slot == NULL) qa_range_error("Out of memory");
}
static void clear_fields(account_state *s) {
  set_str(&s->username, "");
  set_str(&s->password, "");
  set_str(&s->email, "");
}
static void close_state(account_state *s) {
  s->generation++;
  clear_fields(s);
  s->busy = false;
  set_str(&s->message, "");
}
/* Synchronous run(): error text becomes the message, guarded by the
 * generation in case the operation re-entered the menu. */
static void run(account_state *s, const char *(*op)(account_state *,
                                                    const qa_ui_ranking_account_actions *)) {
  const qa_ui_ranking_account_actions *actions = s->current(s->ctx);
  if (s->busy || actions == NULL) return;
  long request = s->generation;
  s->busy = true;
  set_str(&s->message, "");
  const char *error = op(s, actions);
  if (error != NULL && request == s->generation) set_str(&s->message, error);
  else if (error != NULL && request != s->generation) { /* discarded */ }
  if (request == s->generation) s->busy = false;
}
static const qa_ui_ranking_account_actions *actions_of(account_state *s) {
  return s->current(s->ctx);
}
static bool editable(account_state *s) {
  const qa_ui_ranking_account_actions *actions = actions_of(s);
  if (actions == NULL) return false;
  qa_ui_ranking_account_view view = qa_ui_ranking_account_view_for(actions);
  if (strcmp(view.kind, "account") != 0) return false;
  return !s->busy && strcmp(view.player.kind, "active") != 0
         && strcmp(view.player.kind, "pending") != 0;
}
static bool blank(const char *text) {
  for (; *text != '\0'; text++)
    if (!qa_input_js_space((unsigned char)*text)) return false;
  return true;
}

enum { A_USERNAME, A_PASSWORD, A_EMAIL, A_CREATE, A_SUBMIT, A_RESET,
       A_SPECTATE, A_BACK, A_NONE };
#define MAX_ACTX 16

static const char *op_submit(account_state *s,
                             const qa_ui_ranking_account_actions *a) {
  qa_ranking_account_request request;
  memset(&request, 0, sizeof request);
  request.kind = s->create ? "create" : "login";
  request.username = s->username;
  request.password = "";
  request.email = s->create ? s->email : NULL;
  /* TS captures the password into the request then clears the field
   * before running; do the same with a private copy. */
  char *password = s->password;
  s->password = qa_input_dup("");
  if (s->password == NULL) qa_range_error("Out of memory");
  request.password = password;
  const char *error = a->submit(a->ctx, request);
  free(password);
  return error;
}
static const char *op_reset(account_state *s,
                            const qa_ui_ranking_account_actions *a) {
  (void)s;
  return a->reset(a->ctx);
}
static const char *op_spectate(account_state *s,
                               const qa_ui_ranking_account_actions *a) {
  (void)s;
  return a->spectate(a->ctx);
}
static void on_activate(void *v, qa_seat_id seat) {
  (void)seat;
  actx *c = v;
  account_state *s = c->state;
  switch (c->action) {
    case A_SUBMIT:
      if (!editable(s) || blank(s->username) || s->password[0] == '\0')
        return;
      run(s, op_submit);
      break;
    case A_RESET:
      run(s, op_reset);
      break;
    case A_SPECTATE:
      clear_fields(s);
      run(s, op_spectate);
      break;
    case A_BACK:
      qa_native_ui_controller_close_menu(s->controller);
      break;
    default: break;
  }
}
static void on_change_text(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  actx *c = v;
  if (c->action == A_USERNAME) set_str(&c->state->username, value);
  else if (c->action == A_PASSWORD) set_str(&c->state->password, value);
  else if (c->action == A_EMAIL) set_str(&c->state->email, value);
}
static void on_submit_noop(void *v, qa_seat_id seat, const char *value) {
  (void)v; (void)seat; (void)value;
}
static void on_change_checked(void *v, qa_seat_id seat, bool value) {
  (void)seat;
  actx *c = v;
  c->state->create = value;
}
static void clear_labels(account_state *s, size_t count) {
  for (size_t i = 0; i < count; i++) {
    free(s->labels[i]);
    free(s->ids[i]);
    s->labels[i] = NULL;
    s->ids[i] = NULL;
  }
}
static qa_ui_control button(account_state *s, size_t slot, const char *id,
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
  if (slot < MAX_ACTX) {
    s->ctxs[slot].state = s;
    s->ctxs[slot].action = action;
    control.ctx = &s->ctxs[slot];
  }
  control.activate_seat = on_activate;
  return control;
}
static qa_ui_control field(account_state *s, size_t slot, const char *id,
                           const char *label, double row, const char *text,
                           int action, bool editable_now, bool masked) {
  qa_ui_control control;
  memset(&control, 0, sizeof control);
  control.id = id;
  control.kind = "text-entry";
  control.label = label;
  control.rect = qa_ui_menu_row_default(row);
  control.visible = true;
  control.enabled = editable_now;
  control.text = text;
  control.has_masked = true;
  control.masked = masked;
  control.maximum_length = 128;
  if (slot < MAX_ACTX) {
    s->ctxs[slot].state = s;
    s->ctxs[slot].action = action;
    control.ctx = &s->ctxs[slot];
  }
  control.change_text = on_change_text;
  control.submit_text = on_submit_noop;
  return control;
}

static void menu_close(void *v, qa_seat_id seat) {
  (void)seat;
  close_state((account_state *)v);
}
static qa_ui_menu factory(void *v) {
  account_state *s = v;
  clear_labels(s, 16);
  const qa_ui_ranking_account_actions *actions = actions_of(s);
  bool have_view = actions != NULL;
  qa_ui_ranking_account_view view;
  memset(&view, 0, sizeof view);
  if (have_view) view = qa_ui_ranking_account_view_for(actions);
  bool is_account = have_view && strcmp(view.kind, "account") == 0;
  const qa_ranking_player_state *player = is_account ? &view.player : NULL;
  bool can_edit = !s->busy && player != NULL
                  && strcmp(player->kind, "active") != 0
                  && strcmp(player->kind, "pending") != 0;
  const char *status;
  if (s->message[0] != '\0') status = s->message;
  else if (s->busy) status = "Working...";
  else if (!have_view) status = "No ranking account service for this player.";
  else if (strcmp(view.kind, "unavailable") == 0) status = view.message;
  else if (strcmp(view.kind, "disabled") == 0)
    status = "Rankings are disabled for this match.";
  else if (strcmp(view.kind, "busy") == 0
           || (player != NULL && strcmp(player->kind, "pending") == 0))
    status = "Contacting ranking provider...";
  else if (player != NULL && strcmp(player->kind, "active") == 0) {
    char *rank = qa_input_js_number(player->account.rank);
    if (rank == NULL) qa_range_error("Out of memory");
    snprintf(s->status_buffer, sizeof s->status_buffer,
             "Signed in. Rank: %s", rank);
    free(rank);
    status = s->status_buffer;
  } else if (player != NULL && strcmp(player->kind, "denied") == 0)
    status = player->reason;
  else if (player != NULL && strcmp(player->kind, "spectator") == 0)
    status = "Spectating. Sign in to play ranked.";
  else
    status = "Sign in or create an account.";
  qa_ui_control *controls = s->controls;
  size_t at = 0;
  controls[at] = field(s, at, "ui:rankings:username", "Username", 0,
                       s->username, A_USERNAME, can_edit, false);
  at++;
  controls[at] = field(s, at, "ui:rankings:password", "Password", 1,
                       s->password, A_PASSWORD, can_edit, true);
  at++;
  controls[at] = field(s, at, "ui:rankings:email", "Email (new account)",
                       2, s->email, A_EMAIL, can_edit, false);
  at++;
  qa_ui_control create;
  memset(&create, 0, sizeof create);
  create.id = "ui:rankings:create";
  create.kind = "toggle";
  create.label = "Create a new account";
  create.rect = qa_ui_menu_row_default(3);
  create.visible = true;
  create.enabled = can_edit;
  create.checked = s->create;
  s->ctxs[at].state = s;
  s->ctxs[at].action = A_CREATE;
  create.ctx = &s->ctxs[at];
  create.change_checked = on_change_checked;
  controls[at++] = create;
  controls[at] = button(s, at, "ui:rankings:submit",
                        s->create ? "Create account" : "Sign in", 4, A_SUBMIT,
                        can_edit && !blank(s->username)
                            && s->password[0] != '\0'
                            && (!s->create || !blank(s->email)));
  at++;
  controls[at] = button(s, at, "ui:rankings:reset", "Reset account status",
                        5, A_RESET,
                        !s->busy && player != NULL
                            && (strcmp(player->kind, "denied") == 0
                                || strcmp(player->kind, "spectator") == 0));
  at++;
  controls[at] = button(s, at, "ui:rankings:spectate", "Spectate / sign out",
                        6, A_SPECTATE,
                        !s->busy && player != NULL
                            && strcmp(player->kind, "pending") != 0);
  at++;
  size_t back_at = at;
  controls[at] = button(s, at, "ui:rankings:back", "Back", 11, A_BACK, true);
  at++;
  (void)back_at;
  /* TS wrapWords(status, 64).slice(0, 3). */
  size_t line_count = 0;
  char **lines = qa_ui_wrap_words(status, 64, &line_count);
  for (size_t line = 0; line < 3 && line < line_count && at < 16; line++) {
    char id[48];
    snprintf(id, sizeof id, "ui:rankings:status-%zu", line);
    s->labels[at] = qa_input_dup(lines[line]);
    s->ids[at] = qa_input_dup(id);
    if (s->labels[at] == NULL || s->ids[at] == NULL)
      qa_range_error("Out of memory");
    controls[at] = button(s, at, s->ids[at], s->labels[at], 8 + (double)line,
                          A_NONE, false);
    at++;
  }
  qa_ui_wrapped_lines_free(lines, line_count);
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = "menu:rankings:account";
  menu.title = "Ranking account";
  menu.full_screen = false;
  menu.controls = controls;
  menu.control_count = at;
  menu.ctx = s;
  menu.close = menu_close;
  return menu;
}

static void account_dispose(qa_ui_settings_menus *menus) {
  account_state *s = menus->state;
  if (s == NULL) return;
  close_state(s);
  qa_native_ui_controller_unregister(s->controller, s->handle);
  clear_labels(s, 16);
  free(s->username);
  free(s->password);
  free(s->email);
  free(s->message);
  free(s);
  menus->state = NULL;
}

qa_ui_settings_menus qa_ui_register_ranking_account_menu(
    qa_native_ui_controller *controller,
    const qa_ui_ranking_account_actions *(*current)(void *ctx), void *ctx) {
  account_state *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->controller = controller;
  s->current = current;
  s->ctx = ctx;
  s->handle = -1;
  clear_fields(s);
  set_str(&s->message, "");
  int handle = qa_native_ui_controller_register(controller,
                                                "menu:rankings:account",
                                                factory, s);
  if (handle < 0) qa_range_error("Out of memory");
  s->handle = handle;
  qa_ui_settings_menus menus = { "menu:rankings:account", s,
                                 account_dispose };
  return menus;
}
