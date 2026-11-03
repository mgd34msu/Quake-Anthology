/* Port of src/app/bootstrap/q2-match-ui.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/q2_match_ui.h"
#include "qa/network/common/js_number.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/ui/common/layout.h"
#include <math.h>
typedef struct name_entry { qa_actor_id actor; char *name; } name_entry;
typedef struct menu_entry_ctx {
  qa_app_q2_match_ui *ui;
  char **command; size_t command_count; /* owned, NULL = TS null */
} menu_entry_ctx;
struct qa_app_q2_match_ui {
  qa_actor_id actor;
  qa_native_ui_controller *controller;
  qa_app_q2_match_command_fn command;
  void *command_ctx;
  qa_app_q2_match_print_fn print;
  void *print_ctx;
  name_entry *names; size_t name_count, name_capacity;
  qa_ui_hud_prompt *prompts; size_t prompt_count;
  char **prompt_strings; size_t prompt_string_count;
  int menu_handle; /* -1 none */
  /* Menu factory storage (one live menu). */
  qa_ui_control *controls; size_t control_count;
  menu_entry_ctx *entry_ctxs;
  char *menu_title;
  qa_app_q2_match_setting *settings; size_t setting_count;
};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static char *dup_text(const char *text) {
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
qa_app_q2_match_ui *qa_app_q2_match_ui_create(qa_actor_id actor, qa_native_ui_controller *controller,
    qa_app_q2_match_command_fn command, void *command_ctx,
    qa_app_q2_match_print_fn print, void *print_ctx) {
  qa_app_q2_match_ui *ui = calloc(1, sizeof *ui);
  if (ui == NULL) qa_range_error("Out of memory");
  ui->actor = actor; ui->controller = controller;
  ui->command = command; ui->command_ctx = command_ctx;
  ui->print = print; ui->print_ctx = print_ctx;
  ui->menu_handle = -1;
  return ui;
}
static void dispose_menu(qa_app_q2_match_ui *ui);
void qa_app_q2_match_ui_destroy(qa_app_q2_match_ui *ui) {
  if (ui == NULL) return;
  dispose_menu(ui);
  for (size_t i = 0; i < ui->name_count; i++) free(ui->names[i].name);
  free(ui->names);
  free(ui->prompts);
  for (size_t i = 0; i < ui->prompt_string_count; i++) free(ui->prompt_strings[i]);
  free(ui->prompt_strings);
  free(ui->menu_title);
  free(ui);
}
void qa_app_q2_match_ui_name(qa_app_q2_match_ui *ui, qa_actor_id actor, const char *name) {
  for (size_t i = 0; i < ui->name_count; i++) {
    if (qa_same_actor(ui->names[i].actor, actor)) {
      free(ui->names[i].name);
      ui->names[i].name = dup_text(name);
      return;
    }
  }
  if (ui->name_count == ui->name_capacity) {
    size_t cap = ui->name_capacity == 0 ? 8 : ui->name_capacity * 2;
    name_entry *next = realloc(ui->names, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    ui->names = next; ui->name_capacity = cap;
  }
  ui->names[ui->name_count].actor = actor;
  ui->names[ui->name_count].name = dup_text(name);
  ui->name_count++;
}
static const char *name_of(qa_app_q2_match_ui *ui, qa_actor_id actor) {
  for (size_t i = 0; i < ui->name_count; i++)
    if (qa_same_actor(ui->names[i].actor, actor)) return ui->names[i].name;
  return NULL;
}
const qa_ui_hud_prompt *qa_app_q2_match_ui_prompts(const qa_app_q2_match_ui *ui, size_t *out_count) {
  *out_count = ui->prompt_count;
  return ui->prompts;
}
static void dispose_menu(qa_app_q2_match_ui *ui) {
  if (ui->menu_handle >= 0) {
    qa_native_ui_controller_unregister(ui->controller, ui->menu_handle);
    ui->menu_handle = -1;
  }
  free(ui->controls); ui->controls = NULL; ui->control_count = 0;
  if (ui->entry_ctxs != NULL) {
    /* entry ctx count tracked by control_count before reset is lost;
     * commands are freed with the factory rebuild below. */
  }
}
static void entry_activate(void *v, qa_seat_id seat) {
  (void)seat;
  menu_entry_ctx *entry = v;
  qa_native_ui_controller_close_all(entry->ui->controller);
  if (entry->command == NULL || entry->command_count == 0) return;
  entry->ui->command(entry->ui->command_ctx, entry->command[0],
      entry->command_count > 1 ? (const char *const *)(entry->command + 1) : NULL,
      entry->command_count - 1);
}
typedef struct setting_ctx { qa_app_q2_match_ui *ui; char *key; } setting_ctx;
static void setting_change_checked(void *v, qa_seat_id seat, bool checked) {
  (void)seat;
  setting_ctx *ctx = v;
  const char *args[] = {ctx->key, checked ? "true" : "false"};
  ctx->ui->command(ctx->ui->command_ctx, "ctf-settings", args, 2);
}
static void setting_change_text(void *v, qa_seat_id seat, const char *text) {
  (void)v; (void)seat; (void)text;
}
static void setting_submit_text(void *v, qa_seat_id seat, const char *text) {
  (void)seat;
  setting_ctx *ctx = v;
  const char *args[] = {ctx->key, text};
  ctx->ui->command(ctx->ui->command_ctx, "ctf-settings", args, 2);
}
static void close_activate(void *v, qa_seat_id seat) {
  (void)seat;
  qa_native_ui_controller_close_all(((qa_app_q2_match_ui *)v)->controller);
}
static void noop_open(void *ctx, qa_seat_id seat) { (void)ctx; (void)seat; }
typedef struct pending_menu {
  qa_app_q2_match_ui *ui;
  char *title;
  struct { char *label; char **command; size_t command_count; } *entries;
  size_t entry_count;
  bool admin;
} pending_menu;
static pending_menu pending;
static qa_ui_menu menu_factory(void *v) {
  (void)v;
  qa_app_q2_match_ui *ui = pending.ui;
  free(ui->controls);
  size_t count = pending.admin ? pending.ui->setting_count + 1 : pending.entry_count;
  ui->controls = calloc(count == 0 ? 1 : count, sizeof *ui->controls);
  if (ui->controls == NULL) qa_range_error("Out of memory");
  ui->control_count = count;
  qa_ui_menu_row_options none = {false, false, false, false, 0, 0, 0, 0};
  if (pending.admin) {
    for (size_t i = 0; i < ui->setting_count; i++) {
      const qa_app_q2_match_setting *setting = &ui->settings[i];
      /* Camel-case label: insert a space before each capital. */
      char label[256];
      size_t at = 0;
      for (const char *p = setting->key; *p != '\0' && at + 2 < sizeof label; p++) {
        if (*p >= 'A' && *p <= 'Z') label[at++] = ' ';
        label[at++] = *p;
      }
      label[at] = '\0';
      if (label[0] >= 'a' && label[0] <= 'z') label[0] = (char)(label[0] - 'a' + 'A');
      setting_ctx *ctx = calloc(1, sizeof *ctx);
      if (ctx == NULL) qa_range_error("Out of memory");
      ctx->ui = ui; ctx->key = dup_text(setting->key);
      qa_ui_control *control = &ui->controls[i];
      char id[256];
      snprintf(id, sizeof id, "ui:match:%s", setting->key);
      control->id = dup_text(id);
      control->label = dup_text(label);
      control->rect = qa_ui_menu_row((double)(i + 1), none);
      control->enabled = true; control->visible = true;
      control->ctx = ctx;
      if (setting->is_boolean) {
        control->kind = "toggle"; control->checked = setting->bool_value;
        control->change_checked = setting_change_checked;
      } else {
        control->kind = "text-entry"; control->text = setting->text_value;
        control->maximum_length = 8;
        control->change_text = setting_change_text;
        control->submit_text = setting_submit_text;
      }
    }
    qa_ui_control *close_control = &ui->controls[ui->setting_count];
    close_control->id = "ui:match:close"; close_control->label = "Close";
    close_control->rect = qa_ui_menu_row(10, none);
    close_control->enabled = true; close_control->visible = true;
    close_control->kind = "button"; close_control->ctx = ui;
    close_control->activate_seat = close_activate;
  } else {
    free(ui->entry_ctxs);
    ui->entry_ctxs = calloc(count == 0 ? 1 : count, sizeof *ui->entry_ctxs);
    if (ui->entry_ctxs == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < count; i++) {
      menu_entry_ctx *ctx = &ui->entry_ctxs[i];
      ctx->ui = ui;
      ctx->command = pending.entries[i].command;
      ctx->command_count = pending.entries[i].command_count;
      qa_ui_control *control = &ui->controls[i];
      char id[64];
      snprintf(id, sizeof id, "ui:match:%zu", i);
      control->id = dup_text(id);
      control->kind = "button";
      control->label = pending.entries[i].label;
      control->rect = qa_ui_menu_row((double)(i + 1), none);
      control->enabled = pending.entries[i].command != NULL;
      control->visible = true;
      control->ctx = ctx;
      control->activate_seat = entry_activate;
    }
  }
  qa_ui_menu menu = {0};
  menu.id = "menu:application:match";
  menu.title = pending.title;
  menu.full_screen = true;
  menu.controls = ui->controls;
  menu.control_count = count;
  menu.ctx = ui;
  menu.open = noop_open;
  menu.close = noop_open;
  return menu;
}
static void show_menu(qa_app_q2_match_ui *ui, const char *title, bool admin) {
  qa_native_ui_controller_close_all(ui->controller);
  dispose_menu(ui);
  free(ui->menu_title);
  ui->menu_title = dup_text(title);
  pending.ui = ui;
  pending.title = ui->menu_title;
  pending.admin = admin;
  ui->menu_handle = qa_native_ui_controller_register(ui->controller, "menu:application:match", menu_factory, ui);
  if (ui->menu_handle < 0) qa_range_error("Out of memory");
  qa_native_ui_controller_open_menu(ui->controller, "menu:application:match");
}
static char **split_command(const char *command, size_t *out_count) {
  if (command == NULL) { *out_count = 0; return NULL; }
  size_t capacity = 4, count = 0;
  char **parts = calloc(capacity, sizeof *parts);
  if (parts == NULL) qa_range_error("Out of memory");
  const char *p = command;
  while (*p != '\0') {
    while (*p != '\0' && isspace((unsigned char)*p)) p++;
    if (*p == '\0') break;
    const char *start = p;
    while (*p != '\0' && !isspace((unsigned char)*p)) p++;
    size_t len = (size_t)(p - start);
    char *part = malloc(len + 1);
    if (part == NULL) qa_range_error("Out of memory");
    memcpy(part, start, len); part[len] = '\0';
    if (count == capacity) {
      capacity *= 2;
      char **next = realloc(parts, capacity * sizeof *next);
      if (next == NULL) qa_range_error("Out of memory");
      parts = next;
    }
    parts[count++] = part;
  }
  *out_count = count;
  return parts;
}
void qa_app_q2_match_ui_receive(qa_app_q2_match_ui *ui, qa_app_q2_match_event event) {
  if (event.has_actor && !qa_same_actor(event.actor, ui->actor)) return;
  if (is(event.kind, "grapple-cable")) return;
  if (is(event.kind, "match-status")) { ui->print(ui->print_ctx, event.text); return; }
  if (is(event.kind, "score-log")) {
    char line[1024];
    if ((fabs(event.amount) < 9.2e18 && event.amount == trunc(event.amount)))
      snprintf(line, sizeof line, "%s: %s%s\n", event.name, event.amount > 0 ? "+" : "", QA_JS_NUMBER(event.amount));
    else snprintf(line, sizeof line, "%s: %s%s\n", event.name, event.amount > 0 ? "+" : "", QA_JS_NUMBER(event.amount));
    ui->print(ui->print_ctx, line);
    return;
  }
  if (is(event.kind, "hud")) {
    for (size_t i = 0; i < ui->prompt_string_count; i++) free(ui->prompt_strings[i]);
    free(ui->prompt_strings); ui->prompt_strings = NULL; ui->prompt_string_count = 0;
    free(ui->prompts); ui->prompts = NULL; ui->prompt_count = 0;
    const char *values[8];
    size_t count = 0;
    char scratch[3][256];
    values[count++] = event.team == 1 ? "Red" : event.team == 2 ? "Blue" : "Spectator";
    if (event.has_captures) {
      snprintf(scratch[0], sizeof scratch[0], "Red %s (%s)", QA_JS_NUMBER(event.captures[0]), event.flag_states[0]);
      snprintf(scratch[1], sizeof scratch[1], "Blue %s (%s)", QA_JS_NUMBER(event.captures[1]), event.flag_states[1]);
      values[count++] = scratch[0];
      values[count++] = scratch[1];
      if (event.tech != NULL) {
        values[count++] = is(event.tech, "item_tech1") ? "Disruptor Shield"
            : is(event.tech, "item_tech2") ? "Power Amplifier"
            : is(event.tech, "item_tech3") ? "Time Accel" : "AutoDoc";
      }
      if (event.match != NULL && event.match[0] != '\0') values[count++] = event.match;
      if (event.has_id_target) {
        const char *name = name_of(ui, event.id_target);
        if (name != NULL) values[count++] = name;
      }
      if (event.has_blink_team) {
        snprintf(scratch[2], sizeof scratch[2], "%s captured the flag", event.blink_team == 1 ? "Red" : "Blue");
        values[count++] = scratch[2];
      }
    } else if (event.rune != NULL) {
      snprintf(scratch[0], sizeof scratch[0], "%s artifact", event.rune);
      values[count++] = scratch[0];
    }
    if (event.carried_flag) values[count++] = "Carrying flag";
    ui->prompts = calloc(count, sizeof *ui->prompts);
    ui->prompt_strings = calloc(count, sizeof *ui->prompt_strings);
    if (ui->prompts == NULL || ui->prompt_strings == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < count; i++) {
      ui->prompt_strings[i] = dup_text(values[i]);
      ui->prompts[i].action = ui->prompt_strings[i];
      ui->prompts[i].binding = "";
      ui->prompts[i].icon = NULL;
    }
    ui->prompt_count = count;
    ui->prompt_string_count = count;
    return;
  }
  if (is(event.kind, "scoreboard")) {
    size_t total = event.has_rows ? event.row_count
        : event.red_count + event.blue_count + event.spectator_count;
    pending.entries = calloc(total + 1, sizeof *pending.entries);
    if (pending.entries == NULL) qa_range_error("Out of memory");
    pending.entry_count = 0;
    const qa_app_q2_match_score_row *groups[3] = {event.rows, event.red, event.blue};
    size_t group_counts[3] = {event.has_rows ? event.row_count : 0,
        event.has_rows ? 0 : event.red_count, event.has_rows ? 0 : event.blue_count};
    const qa_app_q2_match_score_row *spectators = event.has_rows ? NULL : event.spectators;
    size_t spectator_count = event.has_rows ? 0 : event.spectator_count;
    for (size_t g = 0; g < 3; g++) {
      for (size_t i = 0; i < group_counts[g]; i++) {
        char label[1024];
        snprintf(label, sizeof label, "%s   %s   %s ms", groups[g][i].name,
            QA_JS_NUMBER(groups[g][i].score), QA_JS_NUMBER(groups[g][i].ping));
        pending.entries[pending.entry_count].label = dup_text(label);
        pending.entries[pending.entry_count].command = NULL;
        pending.entries[pending.entry_count].command_count = 0;
        pending.entry_count++;
      }
    }
    for (size_t i = 0; i < spectator_count; i++) {
      char label[1024];
      snprintf(label, sizeof label, "%s   %s   %s ms", spectators[i].name,
          QA_JS_NUMBER(spectators[i].score), QA_JS_NUMBER(spectators[i].ping));
      pending.entries[pending.entry_count].label = dup_text(label);
      pending.entries[pending.entry_count].command = NULL;
      pending.entries[pending.entry_count].command_count = 0;
      pending.entry_count++;
    }
    pending.entries[pending.entry_count].label = dup_text("Close");
    size_t command_count = 0;
    pending.entries[pending.entry_count].command = split_command("score", &command_count);
    pending.entries[pending.entry_count].command_count = command_count;
    pending.entry_count++;
    char title[128];
    if (event.has_captures) snprintf(title, sizeof title, "Red %s - Blue %s",
        QA_JS_NUMBER(event.captures[0]), QA_JS_NUMBER(event.captures[1]));
    else snprintf(title, sizeof title, "Scores");
    show_menu(ui, title, false);
    return;
  }
  if (is(event.kind, "menu")) {
    pending.entries = calloc(event.entry_count == 0 ? 1 : event.entry_count, sizeof *pending.entries);
    if (pending.entries == NULL) qa_range_error("Out of memory");
    pending.entry_count = event.entry_count;
    for (size_t i = 0; i < event.entry_count; i++) {
      pending.entries[i].label = dup_text(event.entries[i].label);
      if (event.entries[i].has_action) {
        if (event.entries[i].action == NULL) {
          pending.entries[i].command = NULL; pending.entries[i].command_count = 0;
        } else {
          pending.entries[i].command = calloc(2, sizeof(char *));
          if (pending.entries[i].command == NULL) qa_range_error("Out of memory");
          pending.entries[i].command[0] = dup_text("ctf-menu");
          pending.entries[i].command[1] = dup_text(event.entries[i].action);
          pending.entries[i].command_count = 2;
        }
      } else {
        size_t command_count = 0;
        pending.entries[i].command = split_command(event.entries[i].command, &command_count);
        pending.entries[i].command_count = command_count;
      }
    }
    show_menu(ui, event.title, false);
    return;
  }
  if (is(event.kind, "admin-settings")) {
    free(ui->settings);
    ui->settings = calloc(event.setting_count == 0 ? 1 : event.setting_count, sizeof *ui->settings);
    if (ui->settings == NULL) qa_range_error("Out of memory");
    memcpy(ui->settings, event.settings, event.setting_count * sizeof *ui->settings);
    ui->setting_count = event.setting_count;
    show_menu(ui, "Match settings", true);
    return;
  }
  qa_range_error("Unknown match UI event: %s", event.kind);
}
void qa_app_q2_match_ui_close(qa_app_q2_match_ui *ui) { dispose_menu(ui); }
