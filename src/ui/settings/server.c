/* Port of src/ui/settings/server.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/server.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/settings/server/profile.h"
#include "qa/ui/common/controls.h"
#include "qa/ui/common/layout.h"

typedef struct server_state server_state;
typedef struct sctx { server_state *state; int action; size_t slot; } sctx;
struct server_state {
  qa_native_ui_controller *controller;
  qa_ui_host_server_settings host;
  qa_ui_settings_menus rotation;
  long page;
  char *selected; /* NULL is TS null */
  char *draft, *error, *profile_name, *profile_message;
  bool busy;
  int handles[3];
  qa_ui_control controls[16];
  sctx ctxs[16];
  char *labels[16];
  char *ids[16];
  char *targets[16];
  char title[512];
  char scratch[64];
  /* Detail-menu settingControl binding (one live control). */
  qa_ui_setting_binding detail_binding;
  qa_ui_setting_control_storage detail_storage;
  const qa_ui_choice *detail_choices;
  size_t detail_choice_count;
  char *detail_default;
};

enum { S_NONE, S_ITEM, S_PREV, S_NEXT, S_PROFILES, S_BACK, S_APPLY,
       S_DEFAULT, S_SAVE, S_LOAD, S_DETAIL_TEXT, S_PROFILE_TEXT };

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
static void clear_pool(server_state *s) {
  for (size_t i = 0; i < 16; i++) {
    free(s->labels[i]);
    free(s->ids[i]);
    free(s->targets[i]);
    s->labels[i] = NULL;
    s->ids[i] = NULL;
    s->targets[i] = NULL;
  }
}

static const qa_bound_server_setting *find_binding(server_state *s,
                                                   const char *id,
                                                   size_t *out_count) {
  size_t count = 0;
  const qa_bound_server_setting *bindings =
      s->host.bindings(s->host.bindings_ctx, &count);
  if (out_count != NULL) *out_count = count;
  if (id == NULL) return NULL;
  for (size_t i = 0; i < count; i++)
    if (bindings[i].definition != NULL && bindings[i].definition->id != NULL
        && strcmp(bindings[i].definition->id, id) == 0)
      return &bindings[i];
  return NULL;
}

/* TS lines(): wrapWords(text, 60). Stores at most max_lines pooled lines. */
static size_t wrap_lines(server_state *s, const char *text, size_t first_slot,
                         size_t max_lines) {
  size_t count = 0;
  char **lines = qa_ui_wrap_words(text, 60, &count);
  size_t made = count < max_lines ? count : max_lines;
  for (size_t i = 0; i < made; i++)
    if (first_slot + i < 16) pool(&s->labels[first_slot + i], lines[i]);
  qa_ui_wrapped_lines_free(lines, count);
  return made;
}

/* parseServerSetting's RangeError messages, checked non-fatally so
 * Apply can show them (the landed qa_write_server_setting aborts). */
static bool validate_value(const qa_server_setting_definition *def,
                           const char *input, char *message, size_t size) {
  switch (def->kind) {
    case QA_SERVER_SETTING_TOGGLE:
      if (strcmp(input, "1") == 0 || strcmp(input, "true") == 0
          || strcmp(input, "0") == 0 || strcmp(input, "false") == 0)
        return true;
      snprintf(message, size, "%s requires a boolean", def->label);
      return false;
    case QA_SERVER_SETTING_SLIDER: {
      char *trimmed = qa_input_js_trim(input);
      if (trimmed == NULL) qa_range_error("Out of memory");
      bool empty = trimmed[0] == '\0';
      free(trimmed);
      double value = qa_input_js_parse_number(input);
      bool safe = value == trunc(value) && fabs(value) <= 9007199254740991.0;
      if (empty || !isfinite(value) || (def->integer && !safe)
          || value < def->minimum || value > def->maximum) {
        snprintf(message, size, "%s is outside its allowed range",
                 def->label);
        return false;
      }
      return true;
    }
    case QA_SERVER_SETTING_CHOICE:
      for (size_t i = 0; i < def->choice_count; i++)
        if (strcmp(def->choices[i].id, input) == 0) return true;
      snprintf(message, size, "Unknown %s choice", def->label);
      return false;
    case QA_SERVER_SETTING_TEXT_ENTRY:
      if ((double)strlen(input) > def->maximum_length
          || strchr(input, '\r') != NULL || strchr(input, '\n') != NULL) {
        snprintf(message, size, "%s contains invalid text", def->label);
        return false;
      }
      return true;
  }
  return true;
}

static void apply_draft(server_state *s) {
  const qa_bound_server_setting *current = find_binding(s, s->selected, NULL);
  if (current == NULL) {
    set_str(&s->error, "This setting is no longer available.");
    return;
  }
  char message[512];
  if (!validate_value(current->definition, s->draft, message,
                      sizeof message)) {
    set_str(&s->error, message);
    return;
  }
  qa_server_setting_status status = qa_write_server_setting(current, s->draft);
  set_str(&s->draft, status.desired != NULL ? status.desired : "");
  qa_server_setting_status_free(&status);
  set_str(&s->error, "");
}

static bool profile_name_valid(const char *name) {
  size_t length = strlen(name);
  if (length < 1 || length > 32) return false;
  if (!isalnum((unsigned char)name[0])) return false;
  for (size_t i = 1; i < length; i++) {
    unsigned char ch = (unsigned char)name[i];
    if (!isalnum(ch) && ch != '-' && ch != '_') return false;
  }
  return true;
}

static void profile_action(server_state *s, bool save) {
  if (s->busy) return;
  if (!profile_name_valid(s->profile_name)) {
    set_str(&s->profile_message,
            "Use 1\xe2\x80\x93" "32 letters, digits, hyphens or underscores.");
    return;
  }
  s->busy = true;
  set_str(&s->profile_message, "");
  char *name = qa_input_dup(s->profile_name);
  if (name == NULL) qa_range_error("Out of memory");
  char path[512];
  snprintf(path, sizeof path, "servers/%s.json", name);
  size_t count = 0;
  const qa_bound_server_setting *bindings =
      s->host.bindings(s->host.bindings_ctx, &count);
  const qa_server_setting_definition **defs = calloc(
      count == 0 ? 1 : count, sizeof *defs);
  if (defs == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) defs[i] = bindings[i].definition;
  if (save) {
    qa_server_profile captured;
    memset(&captured, 0, sizeof captured);
    qa_capture_server_profile(bindings, count, &captured);
    bool ok = qa_save_server_profile(s->host.store, path, &captured, defs,
                                     count);
    qa_server_profile_free(&captured);
    if (ok) {
      char text[600];
      snprintf(text, sizeof text, "Saved %s.", name);
      set_str(&s->profile_message, text);
    } else {
      set_str(&s->profile_message, "Profile operation failed.");
    }
  } else {
    qa_server_profile loaded;
    memset(&loaded, 0, sizeof loaded);
    if (!qa_load_server_profile(s->host.store, path, defs, count, &loaded)) {
      char text[600];
      snprintf(text, sizeof text, "Profile %s does not exist.", name);
      set_str(&s->profile_message, text);
    } else {
      size_t fresh_count = 0;
      const qa_bound_server_setting *fresh =
          s->host.bindings(s->host.bindings_ctx, &fresh_count);
      qa_apply_server_profile(&loaded, fresh, fresh_count);
      qa_server_profile_free(&loaded);
      char text[600];
      snprintf(text, sizeof text, "Loaded %s.", name);
      set_str(&s->profile_message, text);
    }
  }
  free(defs);
  free(name);
  s->busy = false;
}

static bool detail_enabled(void *ctx) { (void)ctx; return true; }
static bool detail_read_bool(void *v) {
  return strcmp(((server_state *)v)->draft, "1") == 0;
}
static void detail_write_bool(void *v, bool value) {
  set_str(&((server_state *)v)->draft, value ? "1" : "0");
}
static const char *detail_read_str(void *v) {
  return ((server_state *)v)->draft;
}
static void detail_write_str(void *v, const char *value) {
  set_str(&((server_state *)v)->draft, value);
}
static const qa_ui_choice *detail_choices(void *v, size_t *out_count) {
  server_state *s = v;
  *out_count = s->detail_choice_count;
  return s->detail_choices;
}

static void on_item(server_state *s, const char *id);
static void on_activate(void *v, qa_seat_id seat) {
  (void)seat;
  sctx *c = v;
  server_state *s = c->state;
  switch (c->action) {
    case S_ITEM:
      if (s->targets[c->slot] != NULL) on_item(s, s->targets[c->slot]);
      break;
    case S_PREV: s->page--; break;
    case S_NEXT: s->page++; break;
    case S_PROFILES:
      qa_native_ui_controller_open_menu(s->controller,
                                        "menu:server:profiles");
      break;
    case S_BACK:
      qa_native_ui_controller_close_menu(s->controller);
      break;
    case S_APPLY: apply_draft(s); break;
    case S_DEFAULT:
      if (s->detail_default != NULL) set_str(&s->draft, s->detail_default);
      set_str(&s->error, "");
      break;
    case S_SAVE: profile_action(s, true); break;
    case S_LOAD: profile_action(s, false); break;
    default: break;
  }
}
static void on_item(server_state *s, const char *id) {
  set_str(&s->selected, id);
  const qa_bound_server_setting *current = find_binding(s, id, NULL);
  if (current != NULL) {
    qa_server_setting_status status = qa_read_server_setting(current);
    set_str(&s->draft, status.desired != NULL ? status.desired : "");
    qa_server_setting_status_free(&status);
  }
  set_str(&s->error, "");
  qa_native_ui_controller_open_menu(
      s->controller,
      strcmp(id, "server:map-rotation") == 0 ? s->rotation.root
                                             : "menu:server:detail");
}
static void on_change_text(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  sctx *c = v;
  if (c->action == S_DETAIL_TEXT) set_str(&c->state->draft, value);
  else if (c->action == S_PROFILE_TEXT)
    set_str(&c->state->profile_name, value);
}
static void on_submit_detail(void *v, qa_seat_id seat, const char *value) {
  (void)seat;
  (void)value;
  apply_draft(((sctx *)v)->state);
}
static void on_submit_noop(void *v, qa_seat_id seat, const char *value) {
  (void)v;
  (void)seat;
  (void)value;
}
static void no_op_open(void *v, qa_seat_id seat) { (void)v; (void)seat; }

static qa_ui_control button(server_state *s, size_t slot, const char *id,
                            const char *label, double row, int action,
                            bool enabled) {
  qa_ui_control control;
  memset(&control, 0, sizeof control);
  control.id = id;
  control.kind = "button";
  control.label = label;
  control.rect = qa_ui_menu_row_default(row);
  control.enabled = enabled;
  control.visible = true;
  s->ctxs[slot].state = s;
  s->ctxs[slot].action = action;
  s->ctxs[slot].slot = slot;
  control.ctx = &s->ctxs[slot];
  control.activate_seat = on_activate;
  return control;
}
static qa_ui_control info(server_state *s, size_t slot, char *id,
                          const char *label, double row) {
  return button(s, slot, id, label, row, S_NONE, false);
}
static qa_ui_control back_control(server_state *s, size_t slot) {
  return button(s, slot, "ui:server:back", "Back", 11, S_BACK, true);
}

static qa_ui_menu make_menu(server_state *s, const char *id, size_t count) {
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = id;
  menu.title = s->title;
  menu.full_screen = false;
  menu.controls = s->controls;
  menu.control_count = count;
  menu.ctx = s;
  menu.open = no_op_open;
  menu.close = no_op_open;
  return menu;
}

static qa_ui_menu root_factory(void *v) {
  server_state *s = v;
  clear_pool(s);
  size_t count = 0;
  const qa_bound_server_setting *available =
      s->host.bindings(s->host.bindings_ctx, &count);
  long pages = (long)((count + 6) / 7);
  if (pages < 1) pages = 1;
  if (s->page > pages - 1) s->page = pages - 1;
  if (s->page < 0) s->page = 0;
  size_t at = 0;
  size_t first = (size_t)s->page * 7;
  for (size_t i = first; i < count && i < first + 7; i++) {
    const qa_server_setting_definition *def = available[i].definition;
    qa_server_setting_status status = qa_read_server_setting(&available[i]);
    char label[1024], id[512];
    snprintf(label, sizeof label, "%s: %s%s", def->label,
             status.desired != NULL ? status.desired : "",
             status.pending ? " (pending)" : "");
    snprintf(id, sizeof id, "ui:server:%s", def->id);
    qa_server_setting_status_free(&status);
    pool(&s->targets[at], def->id);
    server_state * s2 = s;
    size_t slot = at;
    const char * id2 = pool(&s->ids[at], id);
    const char * label2 = pool(&s->labels[at], label);
    s->controls[at] = button(s2, slot, id2,
                             label2,
                             (double)(i - first), S_ITEM, true);
    at++;
  }
  s->controls[at] = button(s, at, "ui:server:previous", "Previous page", 7,
                           S_PREV, s->page > 0);
  at++;
  s->controls[at] = button(s, at, "ui:server:next", "Next page", 8, S_NEXT,
                           s->page + 1 < pages);
  at++;
  s->controls[at] = button(s, at, "ui:server:profiles",
                           "Save or load profile", 9, S_PROFILES, true);
  at++;
  s->controls[at] = back_control(s, at);
  at++;
  snprintf(s->title, sizeof s->title, "Server settings %ld/%ld",
           s->page + 1, pages);
  return make_menu(s, "menu:server:settings", at);
}

static const char *apply_when(qa_server_apply_at apply) {
  switch (apply) {
    case QA_SERVER_APPLY_LIVE: return "next source update";
    case QA_SERVER_APPLY_NEXT_MATCH: return "next match";
    case QA_SERVER_APPLY_NEXT_MAP: return "next map";
    default: return "server restart";
  }
}

static qa_ui_menu detail_factory(void *v) {
  server_state *s = v;
  clear_pool(s);
  size_t at = 0;
  const qa_bound_server_setting *current = find_binding(s, s->selected, NULL);
  if (current != NULL) {
    const qa_server_setting_definition *def = current->definition;
    snprintf(s->title, sizeof s->title, "%s", def->label);
    set_str(&s->detail_default,
            def->default_value != NULL ? def->default_value : "");
    s->detail_choices = def->choices;
    s->detail_choice_count = def->choice_count;
    qa_server_setting_status status = qa_read_server_setting(current);
    if (def->kind == QA_SERVER_SETTING_TOGGLE
        || def->kind == QA_SERVER_SETTING_CHOICE) {
      qa_ui_setting_binding *b = &s->detail_binding;
      memset(b, 0, sizeof *b);
      b->id = "ui:server:value";
      b->label = "Requested value";
      b->category = "network";
      b->kind = def->kind == QA_SERVER_SETTING_TOGGLE ? "toggle" : "choice";
      b->ctx = s;
      b->enabled = detail_enabled;
      b->read_bool = detail_read_bool;
      b->write_bool = detail_write_bool;
      b->read_str = detail_read_str;
      b->write_str = detail_write_str;
      b->choices = detail_choices;
      s->controls[at++] = qa_ui_setting_control(
          b, qa_ui_menu_row_default(0),
          qa_native_ui_controller_get_seat(s->controller), &s->detail_storage);
    } else {
      qa_ui_control entry;
      memset(&entry, 0, sizeof entry);
      entry.id = "ui:server:value";
      entry.kind = "text-entry";
      entry.label = "Requested value";
      entry.rect = qa_ui_menu_row_default(0);
      entry.visible = true;
      entry.enabled = true;
      entry.text = s->draft;
      entry.maximum_length = def->kind == QA_SERVER_SETTING_TEXT_ENTRY
                                 ? def->maximum_length
                                 : 24;
      s->ctxs[at].state = s;
      s->ctxs[at].action = S_DETAIL_TEXT;
      s->ctxs[at].slot = at;
      entry.ctx = &s->ctxs[at];
      entry.change_text = on_change_text;
      entry.submit_text = on_submit_detail;
      s->controls[at++] = entry;
    }
    char text[1024];
    snprintf(text, sizeof text, "Desired: %s   Effective: %s",
             status.desired != NULL ? status.desired : "",
             status.effective != NULL ? status.effective : "");
    server_state * s5 = s;
    size_t slot5 = at;
    char * id5 = pool(&s->ids[at], "ui:server:info-desired");
    const char * label5 = pool(&s->labels[at], text);
    s->controls[at] = info(s5, slot5, id5,
                           label5, 1);
    at++;
    snprintf(text, sizeof text, "Default: %s",
             def->default_value != NULL ? def->default_value : "");
    server_state * s4 = s;
    size_t slot4 = at;
    char * id4 = pool(&s->ids[at], "ui:server:info-default");
    const char * label4 = pool(&s->labels[at], text);
    s->controls[at] = info(s4, slot4, id4,
                           label4, 2);
    at++;
    if (status.pending)
      snprintf(text, sizeof text, "Pending: applies on %s",
               apply_when(status.apply_at));
    else if (status.apply_at == QA_SERVER_APPLY_LIVE)
      snprintf(text, sizeof text, "Applies during play");
    else
      snprintf(text, sizeof text, "Changes apply on %s",
               apply_when(status.apply_at));
    server_state * s3 = s;
    size_t slot3 = at;
    char * id3 = pool(&s->ids[at], "ui:server:info-timing");
    const char * label3 = pool(&s->labels[at], text);
    s->controls[at] = info(s3, slot3, id3,
                           label3, 3);
    at++;
    qa_server_setting_status_free(&status);
    size_t base = at;
    size_t lines = wrap_lines(s, def->description != NULL ? def->description
                                                          : "",
                              base, 2);
    for (size_t i = 0; i < lines && at < 13; i++) {
      char id[64];
      snprintf(id, sizeof id, "ui:server:info-description-%zu", i);
      /* wrap_lines pooled into labels[base+i]; controls reuse slots. */
      char *label = s->labels[base + i];
      s->labels[base + i] = NULL;
      s->controls[at] = info(s, at, pool(&s->ids[at], id), label, 4 + (double)i);
      s->labels[at] = label;
      at++;
    }
    if (def->kind == QA_SERVER_SETTING_SLIDER) {
      char *low = qa_input_js_number(def->minimum);
      char *high = qa_input_js_number(def->maximum);
      if (low == NULL || high == NULL) qa_range_error("Out of memory");
      snprintf(text, sizeof text, "%s from %s to %s",
               def->integer ? "Whole numbers" : "Numbers", low, high);
      free(low);
      free(high);
      server_state * s2 = s;
      size_t slot2 = at;
      char * id2 = pool(&s->ids[at], "ui:server:info-range");
      const char * label2 = pool(&s->labels[at], text);
      s->controls[at] = info(s2, slot2, id2,
                             label2, 6);
      at++;
    }
    s->controls[at] = button(s, at, "ui:server:apply", "Apply", 7, S_APPLY,
                             true);
    at++;
    s->controls[at] = button(s, at, "ui:server:default", "Use default", 8,
                             S_DEFAULT, true);
    at++;
  } else {
    snprintf(s->title, sizeof s->title, "Server setting");
    s->controls[at] = info(s, at, pool(&s->ids[at], "ui:server:info-missing"),
                           "This setting is no longer available.", 0);
    at++;
  }
  size_t base = at;
  size_t lines = wrap_lines(s, s->error, base, 2);
  for (size_t i = 0; i < lines && at < 15; i++) {
    char id[64];
    snprintf(id, sizeof id, "ui:server:info-error-%zu", i);
    char *label = s->labels[base + i];
    s->labels[base + i] = NULL;
    s->controls[at] = info(s, at, pool(&s->ids[at], id), label, 9 + (double)i);
    s->labels[at] = label;
    at++;
  }
  s->controls[at] = back_control(s, at);
  at++;
  return make_menu(s, "menu:server:detail", at);
}

static qa_ui_menu profiles_factory(void *v) {
  server_state *s = v;
  clear_pool(s);
  size_t at = 0;
  qa_ui_control entry;
  memset(&entry, 0, sizeof entry);
  entry.id = "ui:server:profile-name";
  entry.kind = "text-entry";
  entry.label = "Profile name";
  entry.text = s->profile_name;
  entry.maximum_length = 32;
  entry.rect = qa_ui_menu_row_default(0);
  entry.enabled = !s->busy;
  entry.visible = true;
  s->ctxs[at].state = s;
  s->ctxs[at].action = S_PROFILE_TEXT;
  s->ctxs[at].slot = at;
  entry.ctx = &s->ctxs[at];
  entry.change_text = on_change_text;
  entry.submit_text = on_submit_noop;
  s->controls[at++] = entry;
  s->controls[at] = info(s, at, pool(&s->ids[at], "ui:server:info-profile-help"),
                         "A short name, such as league-night. No file paths.",
                         1);
  at++;
  s->controls[at] = button(s, at, "ui:server:save",
                           "Save current desired settings", 3, S_SAVE,
                           !s->busy);
  at++;
  s->controls[at] = button(s, at, "ui:server:load", "Load and apply profile",
                           4, S_LOAD, !s->busy);
  at++;
  s->controls[at] =
      info(s, at, pool(&s->ids[at], "ui:server:info-profile-timing"),
           "Loaded settings keep their normal apply timing.", 5);
  at++;
  size_t base = at;
  size_t lines = wrap_lines(s, s->busy ? "Working\xe2\x80\xa6"
                                       : s->profile_message,
                            base, 3);
  for (size_t i = 0; i < lines && at < 15; i++) {
    char id[64];
    snprintf(id, sizeof id, "ui:server:info-profile-message-%zu", i);
    char *label = s->labels[base + i];
    s->labels[base + i] = NULL;
    s->controls[at] = info(s, at, pool(&s->ids[at], id), label, 7 + (double)i);
    s->labels[at] = label;
    at++;
  }
  s->controls[at] = back_control(s, at);
  at++;
  snprintf(s->title, sizeof s->title, "Server profiles");
  return make_menu(s, "menu:server:profiles", at);
}

static void server_dispose(qa_ui_settings_menus *menus) {
  server_state *s = menus->state;
  if (s == NULL) return;
  qa_ui_settings_menus_dispose(&s->rotation);
  for (int i = 2; i >= 0; i--)
    qa_native_ui_controller_unregister(s->controller, s->handles[i]);
  clear_pool(s);
  free(s->selected);
  free(s->draft);
  free(s->error);
  free(s->profile_name);
  free(s->profile_message);
  free(s->detail_default);
  free(s);
  menus->state = NULL;
}

qa_ui_settings_menus qa_ui_register_server_settings_menu(
    qa_native_ui_controller *controller,
    const qa_ui_host_server_settings *host) {
  server_state *s = calloc(1, sizeof *s);
  if (s == NULL) qa_range_error("Out of memory");
  s->controller = controller;
  s->host = *host;
  s->rotation = qa_ui_register_map_rotation_menu(controller, host->bindings,
                                                 host->bindings_ctx);
  set_str(&s->draft, "");
  set_str(&s->error, "");
  set_str(&s->profile_name, "default");
  set_str(&s->profile_message, "");
  set_str(&s->detail_default, "");
  static const struct { const char *id; qa_ui_menu (*factory)(void *); }
      menus[3] = {
        { "menu:server:settings", root_factory },
        { "menu:server:detail", detail_factory },
        { "menu:server:profiles", profiles_factory },
      };
  for (int i = 0; i < 3; i++) {
    int handle = qa_native_ui_controller_register(
        controller, menus[i].id, (qa_ui_menu_factory_fn)menus[i].factory, s);
    if (handle < 0) qa_range_error("Out of memory");
    s->handles[i] = handle;
  }
  qa_ui_settings_menus out = { "menu:server:settings", s, server_dispose };
  return out;
}

