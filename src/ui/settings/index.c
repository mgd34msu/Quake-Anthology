/* Port of src/ui/settings/index.ts. GPL-2.0-or-later. */
#include "qa/ui/settings/index.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/ui/common/layout.h"
#include "qa/ui/settings/accessibility.h"

static void oom(void) { qa_range_error("Out of memory"); }
static void *xmalloc(size_t size) {
  void *ptr = malloc(size == 0 ? 1 : size);
  if (ptr == NULL) oom();
  return ptr;
}
static void *xcalloc(size_t count, size_t size) {
  void *ptr = calloc(count == 0 ? 1 : count, size);
  if (ptr == NULL) oom();
  return ptr;
}
static char *xstrdup(const char *text) {
  char *copy = qa_input_dup(text);
  if (copy == NULL) oom();
  return copy;
}
static char *xnumber(double value) {
  char *text = qa_input_js_number(value);
  if (text == NULL) oom();
  return text;
}
static bool kind_is(const char *kind, const char *wanted) {
  return kind != NULL && strcmp(kind, wanted) == 0;
}
/* JS value.toFixed(2) through Number(): shortest of the 2dp value. */
static void format_percent(char *out, size_t size, double value) {
  char fixed[64];
  snprintf(fixed, sizeof fixed, "%.2f", value);
  char *shortest = xnumber(qa_input_js_parse_number(fixed));
  snprintf(out, size, "%s%%", shortest);
  free(shortest);
}

static qa_ui_setting_bindings bindings_of(qa_ui_setting_binding *items,
                                          size_t count, void *state,
                                          void (*teardown)(void *));
typedef struct bindings_owner {
  void (*teardown)(void *);
  void *state;
} bindings_owner;

void qa_ui_setting_bindings_free(qa_ui_setting_bindings *bindings) {
  if (bindings == NULL) return;
  if (bindings->owner != NULL) {
    bindings_owner *owner = bindings->owner;
    if (owner->teardown != NULL) owner->teardown(owner->state);
    free(owner);
  }
  bindings->items = NULL;
  bindings->count = 0;
  bindings->owner = NULL;
}

/* Allocates a bindings result whose items live in state; teardown
 * frees state (and thereby the items). */
qa_ui_setting_bindings qa_ui_setting_bindings_own(
    qa_ui_setting_binding *items, size_t count, void *state,
    void (*teardown)(void *)) {
  return bindings_of(items, count, state, teardown);
}
static qa_ui_setting_bindings bindings_of(qa_ui_setting_binding *items,
                                          size_t count, void *state,
                                          void (*teardown)(void *)) {
  bindings_owner *owner = xcalloc(1, sizeof *owner);
  owner->teardown = teardown;
  owner->state = state;
  qa_ui_setting_bindings out = { items, count, owner };
  return out;
}
static void free_state(void *state) { free(state); }

/* ---- settingControl -------------------------------------------------- */

typedef struct control_ctx {
  const qa_ui_setting_binding *binding;
  qa_seat_id seat;
} control_ctx;

static void require_seat(control_ctx *ctx, qa_seat_id actual) {
  if (!qa_same_seat(actual, ctx->seat))
    qa_range_error("Settings control belongs to another seat");
}
static void ctl_activate(void *v, qa_seat_id seat) {
  control_ctx *ctx = v;
  require_seat(ctx, seat);
  ctx->binding->activate(ctx->binding->ctx);
}
static void ctl_change_checked(void *v, qa_seat_id seat, bool value) {
  control_ctx *ctx = v;
  require_seat(ctx, seat);
  ctx->binding->write_bool(ctx->binding->ctx, value);
}
static void ctl_change_value(void *v, qa_seat_id seat, double value) {
  control_ctx *ctx = v;
  require_seat(ctx, seat);
  ctx->binding->write_num(ctx->binding->ctx, value);
}
static void ctl_select(void *v, qa_seat_id seat, const char *value) {
  control_ctx *ctx = v;
  require_seat(ctx, seat);
  ctx->binding->write_str(ctx->binding->ctx, value);
}
static void ctl_change_text(void *v, qa_seat_id seat, const char *value) {
  control_ctx *ctx = v;
  require_seat(ctx, seat);
  ctx->binding->write_str(ctx->binding->ctx, value);
}
static void ctl_submit_text(void *v, qa_seat_id seat, const char *value) {
  control_ctx *ctx = v;
  require_seat(ctx, seat);
  if (!ctx->binding->has_commit)
    ctx->binding->write_str(ctx->binding->ctx, value);
  else
    ctx->binding->commit_submit(ctx->binding->ctx, value);
}

/* The control_ctx is leaked per control (the TS closure lives as
 * long as the control object the factory returned; factories here
 * rebuild controls per call and the leak is one small struct per
 * rebuild — noted accommodation, matching retained closures). */
qa_ui_control qa_ui_setting_control(const qa_ui_setting_binding *binding,
                                    qa_rect rect, qa_seat_id seat,
                                    char *label_scratch, size_t scratch_size) {
  qa_ui_control control;
  memset(&control, 0, sizeof control);
  control.id = binding->id;
  control.label = binding->label;
  control.rect = rect;
  control.visible = true;
  control.enabled = binding->enabled(binding->ctx);
  control.kind = binding->kind;
  control_ctx *ctx = xmalloc(sizeof *ctx);
  ctx->binding = binding;
  ctx->seat = seat;
  control.ctx = ctx;
  if (kind_is(binding->kind, "toggle")) {
    control.checked = binding->read_bool(binding->ctx);
    control.change_checked = ctl_change_checked;
  } else if (kind_is(binding->kind, "slider")) {
    control.value = binding->read_num(binding->ctx);
    if (binding->format_value != NULL && label_scratch != NULL) {
      const char *label =
          binding->format_value(binding->ctx, binding->read_num(binding->ctx));
      snprintf(label_scratch, scratch_size, "%s", label);
      control.value_label = label_scratch;
    }
    control.minimum = binding->minimum;
    control.maximum = binding->maximum;
    control.step = binding->step;
    control.change_value = ctl_change_value;
  } else if (kind_is(binding->kind, "choice")) {
    control.selected = binding->read_str(binding->ctx);
    size_t count = 0;
    control.choices = binding->choices(binding->ctx, &count);
    control.choice_count = count;
    control.select_choice = ctl_select;
  } else if (kind_is(binding->kind, "text-entry")) {
    control.text = binding->read_str(binding->ctx);
    control.maximum_length = binding->maximum_length;
    control.change_text = ctl_change_text;
    control.submit_text = ctl_submit_text;
  } else if (kind_is(binding->kind, "button")) {
    control.activate_seat = ctl_activate;
  }
  return control;
}

/* ---- bindCvarSetting ------------------------------------------------- */

typedef struct cvar_setting {
  qa_cvar_registry *registry;
  qa_restart_controls *restarts;
  qa_ui_cvar_setting_spec spec;
  char *id;
  char *draft;      /* submit-only draft, NULL = TS null */
  char *scratch;    /* borrowed read_str storage */
  qa_ui_choice *choices;
} cvar_setting;

static void cvar_read_into(cvar_setting *s, char **out) {
  qa_cvar_read read;
  if (!qa_cvar_registry_find(s->registry, s->spec.name, &read))
    qa_range_error("Settings cvar was unregistered: %s", s->spec.name);
  const char *value = read.latched_value != NULL ? read.latched_value
                                                 : read.value;
  free(*out);
  *out = xstrdup(value);
  qa_cvar_read_free(&read);
}
static void cvar_write(cvar_setting *s, const char *value) {
  qa_cvar_snapshot out;
  if (qa_cvar_registry_set(s->registry, s->spec.name, value, false, &out))
    qa_cvar_read_free(&out);
  if (s->spec.has_restart)
    qa_restart_controls_request(s->restarts, s->spec.restart);
}
static bool cvar_enabled(void *v) {
  cvar_setting *s = v;
  qa_cvar_read read;
  if (!qa_cvar_registry_find(s->registry, s->spec.name, &read)) return false;
  qa_command_dialect dialect = qa_cvar_registry_dialect(s->registry);
  int32_t readonly_flags =
      dialect == QA_COMMAND_DIALECT_Q3
          ? QA_CVAR_FLAG_READ_ONLY | QA_CVAR_FLAG_INIT
          : dialect == QA_COMMAND_DIALECT_Q2_CLASSIC
                    || dialect == QA_COMMAND_DIALECT_Q2_RERELEASE
                ? QA_Q2_CVAR_FLAG_NO_SET
                : 0;
  bool ok = (read.flags & readonly_flags) == 0;
  qa_cvar_read_free(&read);
  return ok;
}
static bool cvar_read_bool(void *v) {
  cvar_setting *s = v;
  cvar_read_into(s, &s->scratch);
  return qa_input_js_parse_number(s->scratch) != 0;
}
static void cvar_write_bool(void *v, bool value) {
  cvar_write(v, value ? "1" : "0");
}
static double cvar_read_num(void *v) {
  cvar_setting *s = v;
  cvar_read_into(s, &s->scratch);
  return qa_input_js_parse_number(s->scratch);
}
static void cvar_write_num(void *v, double value) {
  char *text = xnumber(value);
  cvar_write(v, text);
  free(text);
}
static const char *cvar_read_str(void *v) {
  cvar_setting *s = v;
  cvar_read_into(s, &s->scratch);
  return s->scratch;
}
static void cvar_write_choice(void *v, const char *value) {
  cvar_setting *s = v;
  bool known = false;
  for (size_t i = 0; i < s->spec.choice_count; i++)
    if (strcmp(s->spec.choices[i].id, value) == 0) known = true;
  if (!known) qa_range_error("Unknown cvar setting choice");
  cvar_write(s, value);
}
static const qa_ui_choice *cvar_choices(void *v, size_t *out_count) {
  cvar_setting *s = v;
  *out_count = s->spec.choice_count;
  return s->choices;
}
static void cvar_write_str(void *v, const char *value) { cvar_write(v, value); }
static const char *cvar_read_draft(void *v) {
  cvar_setting *s = v;
  if (s->draft != NULL) return s->draft;
  return cvar_read_str(v);
}
static void cvar_write_draft(void *v, const char *value) {
  cvar_setting *s = v;
  free(s->draft);
  s->draft = xstrdup(value);
}
static void cvar_commit_submit(void *v, const char *value) {
  cvar_setting *s = v;
  cvar_write(s, value);
  free(s->draft);
  s->draft = NULL;
}
static void cvar_commit_cancel(void *v) {
  cvar_setting *s = v;
  free(s->draft);
  s->draft = NULL;
}
static const char *restart_name(qa_restart_kind kind) {
  switch (kind) {
    case QA_RESTART_VIDEO: return "video";
    case QA_RESTART_INPUT: return "input";
    default: return "audio";
  }
}

bool qa_ui_bind_cvar_setting(qa_cvar_registry *registry,
                             const qa_ui_cvar_setting_spec *spec,
                             qa_restart_controls *restarts,
                             qa_ui_setting_binding *out) {
  qa_cvar_read probe;
  bool found = qa_cvar_registry_find(registry, spec->name, &probe);
  if (found) qa_cvar_read_free(&probe);
  if (!found) qa_range_error("Settings cvar has no owner: %s", spec->name);
  if (spec->has_restart && restarts == NULL)
    qa_range_error("Settings cvar needs a %s restart owner: %s",
                   restart_name(spec->restart), spec->name);
  cvar_setting *s = calloc(1, sizeof *s);
  if (s == NULL) return false;
  s->registry = registry;
  s->restarts = restarts;
  s->spec = *spec;
  size_t id_size = strlen("ui:settings:") + strlen(spec->name) + 1;
  s->id = malloc(id_size);
  if (s->id == NULL) { free(s); return false; }
  snprintf(s->id, id_size, "ui:settings:%s", spec->name);
  if (spec->choice_count > 0) {
    s->choices = malloc(spec->choice_count * sizeof *s->choices);
    if (s->choices == NULL) { free(s->id); free(s); return false; }
    memcpy(s->choices, spec->choices,
           spec->choice_count * sizeof *s->choices);
  }
  memset(out, 0, sizeof *out);
  out->id = s->id;
  out->label = spec->label;
  out->category = spec->category;
  out->group = spec->group;
  out->kind = spec->kind;
  out->ctx = s;
  out->enabled = cvar_enabled;
  if (kind_is(spec->kind, "toggle")) {
    out->read_bool = cvar_read_bool;
    out->write_bool = cvar_write_bool;
  } else if (kind_is(spec->kind, "slider")) {
    out->minimum = spec->minimum;
    out->maximum = spec->maximum;
    out->step = spec->step;
    out->read_num = cvar_read_num;
    out->write_num = cvar_write_num;
  } else if (kind_is(spec->kind, "choice")) {
    out->read_str = cvar_read_str;
    out->write_str = cvar_write_choice;
    out->choices = cvar_choices;
  } else if (kind_is(spec->kind, "text-entry")) {
    out->maximum_length = spec->maximum_length;
    if (!spec->submit_only) {
      out->read_str = cvar_read_str;
      out->write_str = cvar_write_str;
    } else {
      out->read_str = cvar_read_draft;
      out->write_str = cvar_write_draft;
      out->has_commit = true;
      out->commit_submit = cvar_commit_submit;
      out->commit_cancel = cvar_commit_cancel;
    }
  }
  return true;
}

void qa_ui_cvar_setting_binding_dispose(qa_ui_setting_binding *binding) {
  if (binding == NULL || binding->ctx == NULL) return;
  cvar_setting *s = binding->ctx;
  free(s->id);
  free(s->draft);
  free(s->scratch);
  free(s->choices);
  free(s);
  binding->ctx = NULL;
}

/* ---- registerSettingsMenus ------------------------------------------- */

typedef struct menus_state menus_state;
typedef struct page_desc {
  menus_state *menus;
  char *id;
  char *title;
  const qa_ui_setting_binding **selected;
  size_t selected_count;
  bool is_group_index;
  char *category_id;
  char **groups;
  size_t group_count;
} page_desc;

struct menus_state {
  qa_native_ui_controller *controller;
  const qa_ui_setting_binding *bindings;
  size_t binding_count;
  qa_ui_menu_id llm_root;
  void *llm_menus;
  void (*llm_dispose)(void *);
  bool has_reset;
  qa_ui_settings_reset reset;
  int *handles;
  size_t handle_count, handle_capacity;
  page_desc **pages; /* individually allocated: factory ctxs */
  size_t page_count, page_capacity;
  /* Rebuild pool (one factory result at a time). */
  qa_ui_control *controls;
  size_t control_capacity;
  char **strings;
  size_t string_count, string_capacity;
  qa_ui_menu_scroll scroll;
  const qa_ui_control_id *scroll_ids;
  char *label_pool;
  size_t label_capacity;
  struct nav_slot { menus_state *menus; char *target; } *navs;
  size_t nav_count, nav_capacity;
};

static char *pool_dup(menus_state *m, const char *text) {
  if (m->string_count == m->string_capacity) {
    size_t cap = m->string_capacity == 0 ? 16 : m->string_capacity * 2;
    char **next = realloc(m->strings, cap * sizeof *next);
    if (next == NULL) oom();
    m->strings = next;
    m->string_capacity = cap;
  }
  char *copy = xstrdup(text);
  m->strings[m->string_count++] = copy;
  return copy;
}
static char *pool_fmt(menus_state *m, const char *format, const char *a) {
  char buffer[256];
  snprintf(buffer, sizeof buffer, format, a);
  return pool_dup(m, buffer);
}
static void pool_clear(menus_state *m) {
  for (size_t i = 0; i < m->string_count; i++) free(m->strings[i]);
  m->string_count = 0;
  m->nav_count = 0;
}
static qa_ui_control *pool_controls(menus_state *m, size_t count) {
  if (m->control_capacity < count) {
    qa_ui_control *next = realloc(m->controls, count * sizeof *next);
    if (next == NULL) oom();
    m->controls = next;
    m->control_capacity = count;
  }
  return m->controls;
}
static char *pool_labels(menus_state *m, size_t count) {
  if (m->label_capacity < count * 64) {
    char *next = realloc(m->label_pool, count * 64);
    if (next == NULL) oom();
    m->label_pool = next;
    m->label_capacity = count * 64;
  }
  return m->label_pool;
}
static void nav_activate(void *v, qa_seat_id seat) {
  (void)seat;
  struct nav_slot *slot = v;
  if (slot->target == NULL)
    qa_native_ui_controller_close_menu(slot->menus->controller);
  else
    qa_native_ui_controller_open_menu(slot->menus->controller, slot->target);
}
static void *nav_ctx(menus_state *m, char *target) {
  if (m->nav_count == m->nav_capacity) {
    size_t cap = m->nav_capacity == 0 ? 8 : m->nav_capacity * 2;
    struct nav_slot *next = realloc(m->navs, cap * sizeof *next);
    if (next == NULL) oom();
    m->navs = next;
    m->nav_capacity = cap;
  }
  struct nav_slot *slot = &m->navs[m->nav_count++];
  slot->menus = m;
  slot->target = target;
  return slot;
}
static qa_ui_control nav_button(menus_state *m, const char *id,
                                const char *label, qa_rect rect,
                                char *target) {
  qa_ui_control control;
  memset(&control, 0, sizeof control);
  control.id = id;
  control.label = label;
  control.kind = "button";
  control.rect = rect;
  control.enabled = true;
  control.visible = true;
  control.ctx = nav_ctx(m, target);
  control.activate_seat = nav_activate;
  return control;
}
static qa_ui_control back_button(menus_state *m) {
  qa_ui_menu_row_options none = { false, false, false, false, 0, 0, 0, 0 };
  return nav_button(m, "ui:settings:back", "Back",
                    qa_ui_menu_row(11, none), NULL);
}
static void page_close_cancel(void *v, qa_seat_id seat) {
  (void)seat;
  page_desc *page = v;
  for (size_t i = 0; i < page->selected_count; i++) {
    const qa_ui_setting_binding *b = page->selected[i];
    if (kind_is(b->kind, "text-entry") && b->has_commit
        && b->commit_cancel != NULL)
      b->commit_cancel(b->ctx);
  }
}
static qa_rect row496(double index) {
  qa_ui_menu_row_options options = { false, false, true, false, 0, 0, 496, 0 };
  return qa_ui_menu_row(index, options);
}

static qa_ui_menu page_factory(void *v) {
  page_desc *page = v;
  menus_state *m = page->menus;
  pool_clear(m);
  size_t setting_count = page->selected_count;
  size_t group_buttons = page->is_group_index ? page->group_count : 0;
  size_t total = group_buttons + setting_count + 1;
  qa_ui_control *controls = pool_controls(m, total);
  char *labels = pool_labels(m, setting_count == 0 ? 1 : setting_count);
  qa_seat_id seat = qa_native_ui_controller_get_seat(m->controller);
  size_t at = 0;
  if (page->is_group_index) {
    for (size_t g = 0; g < page->group_count; g++) {
      char slug[256];
      const char *group = page->groups[g];
      size_t o = 0;
      bool dash = false;
      for (const char *p = group; *p != '\0' && o + 1 < sizeof slug; p++) {
        char ch = *p;
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
          slug[o++] = ch;
          dash = false;
        } else if (!dash) {
          slug[o++] = '-';
          dash = true;
        }
      }
      while (o > 0 && slug[o - 1] == '-') o--;
      size_t first = 0;
      while (first < o && slug[first] == '-') first++;
      slug[o] = '\0';
      char id_buffer[512], target_buffer[512];
      snprintf(id_buffer, sizeof id_buffer, "ui:settings:group:%s:%s",
               page->category_id, slug + first);
      snprintf(target_buffer, sizeof target_buffer, "menu:settings:%s:%s",
               page->category_id, slug + first);
      controls[at++] = nav_button(m, pool_dup(m, id_buffer),
                                  pool_dup(m, group), row496((double)g),
                                  pool_dup(m, target_buffer));
    }
  }
  for (size_t i = 0; i < setting_count; i++) {
    controls[at] = qa_ui_setting_control(page->selected[i],
                                         row496((double)(group_buttons + i)),
                                         seat, labels + i * 64, 64);
    at++;
  }
  size_t scrolled = at;
  controls[at++] = back_button(m);
  const qa_ui_control_id *ids = NULL;
  if (scrolled > 0) {
    qa_ui_control_id *array = xmalloc(scrolled * sizeof *array);
    for (size_t i = 0; i < scrolled; i++) array[i] = controls[i].id;
    /* Leaked with the pool's lifetime intent; stored for the menu. */
    m->scroll_ids = array;
    ids = array;
  }
  m->scroll.rect.x = 64;
  m->scroll.rect.y = 92;
  m->scroll.rect.width = 512;
  m->scroll.rect.height = 308;
  m->scroll.content_height = (double)scrolled * 28;
  m->scroll.controls = ids;
  m->scroll.control_count = scrolled;
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = page->id;
  menu.title = page->title;
  menu.full_screen = false;
  menu.scroll = &m->scroll;
  menu.controls = controls;
  menu.control_count = at;
  menu.ctx = page;
  menu.close = page_close_cancel;
  return menu;
}

static qa_ui_menu root_factory(void *v) {
  menus_state *m = v;
  pool_clear(m);
  static const struct { const char *id; const char *label; } categories[7] = {
    { "display", "Display" }, { "video", "Graphics" },
    { "audio", "Audio" }, { "input", "Controls" },
    { "network", "Network" }, { "accessibility", "Accessibility" },
    { "language", "Language" }
  };
  size_t present = 0;
  for (size_t c = 0; c < 7; c++)
    for (size_t i = 0; i < m->binding_count; i++)
      if (strcmp(m->bindings[i].category, categories[c].id) == 0) {
        present++;
        break;
      }
  size_t total = present + (m->llm_root != NULL ? 1 : 0)
               + (m->has_reset ? 1 : 0) + 1;
  qa_ui_control *controls = pool_controls(m, total);
  qa_ui_menu_row_options none = { false, false, false, false, 0, 0, 0, 0 };
  size_t at = 0;
  for (size_t c = 0; c < 7; c++) {
    bool any = false;
    for (size_t i = 0; i < m->binding_count; i++)
      if (strcmp(m->bindings[i].category, categories[c].id) == 0) any = true;
    if (!any) continue;
    char target[160];
    snprintf(target, sizeof target, "menu:settings:%s:0", categories[c].id);
    controls[at] = nav_button(
        m, pool_fmt(m, "ui:settings:category:%s", categories[c].id),
        categories[c].label, qa_ui_menu_row((double)at, none),
        pool_dup(m, target));
    at++;
  }
  if (m->llm_root != NULL) {
    controls[at] = nav_button(m, "ui:settings:llm", "LLM options",
                              qa_ui_menu_row((double)at, none),
                              pool_dup(m, m->llm_root));
    at++;
  }
  if (m->has_reset) {
    controls[at] = nav_button(m, "ui:settings:reset", m->reset.label,
                              qa_ui_menu_row((double)at, none),
                              pool_dup(m, "menu:settings:reset-confirm"));
    at++;
  }
  controls[at++] = back_button(m);
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = "menu:settings:root";
  menu.title = "Options";
  menu.full_screen = false;
  menu.controls = controls;
  menu.control_count = at;
  return menu;
}

static void reset_apply_activate(void *v, qa_seat_id seat) {
  (void)seat;
  menus_state *m = v;
  m->reset.apply(m->reset.ctx);
  qa_native_ui_controller_close_menu(m->controller);
}
static qa_ui_menu reset_factory(void *v) {
  menus_state *m = v;
  pool_clear(m);
  qa_ui_control *controls = pool_controls(m, 2);
  qa_ui_menu_row_options none = { false, false, false, false, 0, 0, 0, 0 };
  controls[0] = nav_button(m, "ui:settings:keep", "Keep current settings",
                           qa_ui_menu_row(3, none), NULL);
  qa_ui_control apply;
  memset(&apply, 0, sizeof apply);
  apply.id = "ui:settings:reset-apply";
  apply.label = "Restore defaults";
  apply.kind = "button";
  apply.rect = qa_ui_menu_row(5, none);
  apply.enabled = true;
  apply.visible = true;
  apply.ctx = m;
  apply.activate_seat = reset_apply_activate;
  controls[1] = apply;
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = "menu:settings:reset-confirm";
  menu.title = m->reset.label;
  menu.full_screen = true;
  menu.controls = controls;
  menu.control_count = 2;
  return menu;
}

static void menus_register(menus_state *m, const char *id,
                           qa_ui_menu (*factory)(void *), void *ctx) {
  int handle = qa_native_ui_controller_register(
      m->controller, id, (qa_ui_menu_factory_fn)factory, ctx);
  if (handle < 0) oom();
  if (m->handle_count == m->handle_capacity) {
    size_t cap = m->handle_capacity == 0 ? 8 : m->handle_capacity * 2;
    int *next = realloc(m->handles, cap * sizeof *next);
    if (next == NULL) oom();
    m->handles = next;
    m->handle_capacity = cap;
  }
  m->handles[m->handle_count++] = handle;
}
static page_desc *menus_page(menus_state *m) {
  if (m->page_count == m->page_capacity) {
    size_t cap = m->page_capacity == 0 ? 8 : m->page_capacity * 2;
    page_desc **next = realloc(m->pages, cap * sizeof *next);
    if (next == NULL) oom();
    m->pages = next;
    m->page_capacity = cap;
  }
  page_desc *page = xcalloc(1, sizeof *page);
  page->menus = m;
  m->pages[m->page_count++] = page;
  return page;
}
static char *slug_of(const char *group) {
  char *out = xmalloc(strlen(group) + 1);
  size_t o = 0;
  bool dash = false;
  for (const char *p = group; *p != '\0'; p++) {
    char ch = *p;
    if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
      out[o++] = ch;
      dash = false;
    } else if (!dash) {
      out[o++] = '-';
      dash = true;
    }
  }
  while (o > 0 && out[o - 1] == '-') o--;
  out[o] = '\0';
  size_t first = 0;
  while (out[first] == '-') first++;
  if (first > 0) memmove(out, out + first, strlen(out + first) + 1);
  return out;
}

qa_ui_settings_menus qa_ui_register_settings_menus(
    qa_native_ui_controller *controller,
    const qa_ui_setting_binding *bindings, size_t binding_count,
    qa_ui_menu_id llm_root, void *llm_menus,
    void (*llm_dispose)(void *llm_menus),
    const qa_ui_settings_reset *reset) {
  static const struct { const char *id; const char *label; } categories[7] = {
    { "display", "Display" }, { "video", "Graphics" },
    { "audio", "Audio" }, { "input", "Controls" },
    { "network", "Network" }, { "accessibility", "Accessibility" },
    { "language", "Language" }
  };
  static const char *const group_order[9] = {
    "Mouse", "Movement", "Weapons", "Gamepad", "Joystick", "MIDI",
    "Player assignment", "Output", "Music"
  };
  menus_state *m = xcalloc(1, sizeof *m);
  m->controller = controller;
  m->bindings = bindings;
  m->binding_count = binding_count;
  m->llm_root = llm_root;
  m->llm_menus = llm_menus;
  m->llm_dispose = llm_dispose;
  if (reset != NULL) {
    m->has_reset = true;
    m->reset = *reset;
    menus_register(m, "menu:settings:reset-confirm", reset_factory, m);
  }
  for (size_t c = 0; c < 7; c++) {
    const qa_ui_setting_binding **selected =
        xmalloc((binding_count + 1) * sizeof *selected);
    size_t count = 0;
    for (size_t i = 0; i < binding_count; i++)
      if (strcmp(bindings[i].category, categories[c].id) == 0)
        selected[count++] = &bindings[i];
    if (count == 0) { free(selected); continue; }
    char **groups = xmalloc((count + 1) * sizeof *groups);
    size_t group_count = 0;
    for (size_t i = 0; i < count; i++) {
      const char *group = selected[i]->group;
      if (group == NULL) continue;
      bool seen = false;
      for (size_t g = 0; g < group_count; g++)
        if (strcmp(groups[g], group) == 0) seen = true;
      if (!seen) groups[group_count++] = xstrdup(group);
    }
    /* Stable sort by the TS group-order rank. */
    for (size_t i = 1; i < group_count; i++) {
      char *key = groups[i];
      size_t key_rank = 9;
      for (size_t r = 0; r < 9; r++)
        if (strcmp(group_order[r], key) == 0) key_rank = r;
      size_t j = i;
      while (j > 0) {
        size_t rank = 9;
        for (size_t r = 0; r < 9; r++)
          if (strcmp(group_order[r], groups[j - 1]) == 0) rank = r;
        if (rank <= key_rank) break;
        groups[j] = groups[j - 1];
        j--;
      }
      groups[j] = key;
    }
    char id[512];
    snprintf(id, sizeof id, "menu:settings:%s:0", categories[c].id);
    if (group_count == 0) {
      page_desc *page = menus_page(m);
      page->id = xstrdup(id);
      page->title = xstrdup(categories[c].label);
      page->selected = selected;
      page->selected_count = count;
      free(groups);
      menus_register(m, page->id, page_factory, page);
      continue;
    }
    for (size_t g = 0; g < group_count; g++) {
      const qa_ui_setting_binding **in_group =
          xmalloc((count + 1) * sizeof *in_group);
      size_t in_count = 0;
      for (size_t i = 0; i < count; i++)
        if (selected[i]->group != NULL
            && strcmp(selected[i]->group, groups[g]) == 0)
          in_group[in_count++] = selected[i];
      char *slug = slug_of(groups[g]);
      char page_id[512];
      snprintf(page_id, sizeof page_id, "menu:settings:%s:%s",
               categories[c].id, slug);
      free(slug);
      page_desc *page = menus_page(m);
      page->id = xstrdup(page_id);
      page->title = xstrdup(groups[g]);
      page->selected = in_group;
      page->selected_count = in_count;
      menus_register(m, page->id, page_factory, page);
    }
    const qa_ui_setting_binding **direct =
        xmalloc((count + 1) * sizeof *direct);
    size_t direct_count = 0;
    for (size_t i = 0; i < count; i++)
      if (selected[i]->group == NULL) direct[direct_count++] = selected[i];
    free(selected);
    page_desc *page = menus_page(m);
    page->id = xstrdup(id);
    page->title = xstrdup(categories[c].label);
    page->selected = direct;
    page->selected_count = direct_count;
    page->is_group_index = true;
    page->category_id = xstrdup(categories[c].id);
    page->groups = groups;
    page->group_count = group_count;
    menus_register(m, page->id, page_factory, page);
  }
  menus_register(m, "menu:settings:root", root_factory, m);
  qa_ui_settings_menus out = { "menu:settings:root", m,
                               qa_ui_settings_menus_dispose };
  return out;
}

void qa_ui_settings_menus_dispose(qa_ui_settings_menus *menus) {
  if (menus == NULL || menus->state == NULL) return;
  if (menus->dispose_fn != NULL
      && menus->dispose_fn != qa_ui_settings_menus_dispose) {
    menus->dispose_fn(menus);
    return;
  }
  menus_state *m = menus->state;
  for (size_t i = m->handle_count; i-- > 0;)
    qa_native_ui_controller_unregister(m->controller, m->handles[i]);
  if (m->llm_dispose != NULL && m->llm_menus != NULL)
    m->llm_dispose(m->llm_menus);
  for (size_t i = 0; i < m->page_count; i++) {
    free(m->pages[i]->id);
    free(m->pages[i]->title);
    free((void *)m->pages[i]->selected);
    free(m->pages[i]->category_id);
    for (size_t g = 0; g < m->pages[i]->group_count; g++)
      free(m->pages[i]->groups[g]);
    free(m->pages[i]->groups);
    free(m->pages[i]);
  }
  pool_clear(m);
  free((void *)m->scroll_ids);
  free(m->handles);
  free(m->pages);
  free(m->controls);
  free(m->strings);
  free(m->label_pool);
  free(m->navs);
  free(m);
  menus->state = NULL;
}

/* ---- service binding factories --------------------------------------- */

static bool always_enabled(void *ctx) { (void)ctx; return true; }

/* vibration */
typedef struct vibration_state {
  qa_ui_controller_vibration_service service;
  qa_ui_setting_binding items[2];
  struct vibration_state *self;
} vibration_state;
static bool vib_read_bool(void *v) {
  vibration_state *s = v;
  return s->service.read(s->service.ctx).controller_vibration;
}
static void vib_write_bool(void *v, bool value) {
  vibration_state *s = v;
  qa_ui_controller_vibration_settings values = { value, 0 };
  s->service.write(s->service.ctx, values, QA_UI_VIBRATION_ON);
}
static double vib_read_num(void *v) {
  vibration_state *s = v;
  return s->service.read(s->service.ctx).controller_vibration_strength;
}
static void vib_write_num(void *v, double value) {
  vibration_state *s = v;
  qa_ui_controller_vibration_settings values = { false, value };
  s->service.write(s->service.ctx, values, QA_UI_VIBRATION_STRENGTH);
}
qa_ui_setting_bindings qa_ui_bind_controller_vibration(
    const qa_ui_controller_vibration_service *service) {
  vibration_state *s = xcalloc(1, sizeof *s);
  s->service = *service;
  s->self = s;
  qa_ui_setting_binding *b = s->items;
  b[0].id = "ui:input:controller-vibration";
  b[0].label = "Controller vibration";
  b[0].category = "input";
  b[0].group = "Gamepad";
  b[0].kind = "toggle";
  b[0].ctx = s;
  b[0].enabled = always_enabled;
  b[0].read_bool = vib_read_bool;
  b[0].write_bool = vib_write_bool;
  b[1].id = "ui:input:controller-vibration-strength";
  b[1].label = "Vibration strength";
  b[1].category = "input";
  b[1].group = "Gamepad";
  b[1].kind = "slider";
  b[1].minimum = 0;
  b[1].maximum = 1;
  b[1].step = 0.05;
  b[1].ctx = s;
  b[1].enabled = always_enabled;
  b[1].read_num = vib_read_num;
  b[1].write_num = vib_write_num;
  return bindings_of(s->items, 2, s, free_state);
}

/* primary input + mouse axes */
typedef struct primary_state {
  qa_ui_primary_input_service service;
  qa_ui_setting_binding items[5];
  char percent[64];
} primary_state;
static double prim_read_sensitivity(void *v) {
  primary_state *s = v;
  return s->service.read(s->service.ctx).sensitivity;
}
static void prim_write_sensitivity(void *v, double value) {
  primary_state *s = v;
  qa_ui_primary_input_settings values = { value, 0, 0, false, false };
  s->service.write(s->service.ctx, values, QA_UI_PRIMARY_SENSITIVITY);
}
static bool prim_read_invert(void *v) {
  primary_state *s = v;
  return s->service.read(s->service.ctx).invert_mouse;
}
static void prim_write_invert(void *v, bool value) {
  primary_state *s = v;
  qa_ui_primary_input_settings values = { 0, 0, 0, value, false };
  s->service.write(s->service.ctx, values, QA_UI_PRIMARY_INVERT);
}
static bool prim_read_run(void *v) {
  primary_state *s = v;
  return s->service.read(s->service.ctx).always_run;
}
static void prim_write_run(void *v, bool value) {
  primary_state *s = v;
  qa_ui_primary_input_settings values = { 0, 0, 0, false, value };
  s->service.write(s->service.ctx, values, QA_UI_PRIMARY_ALWAYS_RUN);
}
/* TS mouseAxis: yaw is index 2 in the array, pitch index 3; the axis
 * discriminator rides in a per-binding ctx copy. */
typedef struct axis_ctx { primary_state *state; bool pitch; } axis_ctx;
static double axis_read(void *v) {
  axis_ctx *a = v;
  qa_ui_primary_input_settings cur = a->state->service.read(
      a->state->service.ctx);
  double current = a->pitch ? cur.pitch : cur.yaw;
  double base = a->pitch ? qa_default_mouse_tuning.pitch
                         : qa_default_mouse_tuning.yaw;
  return fabs(current) / base * 100;
}
static void axis_write(void *v, double value) {
  axis_ctx *a = v;
  qa_ui_primary_input_settings cur = a->state->service.read(
      a->state->service.ctx);
  double current = a->pitch ? cur.pitch : cur.yaw;
  double base = a->pitch ? qa_default_mouse_tuning.pitch
                         : qa_default_mouse_tuning.yaw;
  double direction = current < 0 || (current == 0 && signbit(current)) ? -1 : 1;
  double next = direction * base * fmin(200, fmax(0, value)) / 100;
  qa_ui_primary_input_settings values = cur;
  if (a->pitch) {
    values.pitch = next;
    a->state->service.write(a->state->service.ctx, values,
                            QA_UI_PRIMARY_PITCH);
  } else {
    values.yaw = next;
    a->state->service.write(a->state->service.ctx, values, QA_UI_PRIMARY_YAW);
  }
}
static const char *axis_format(void *v, double value) {
  axis_ctx *a = v;
  format_percent(a->state->percent, sizeof a->state->percent, value);
  return a->state->percent;
}
typedef struct primary_owner { primary_state state; axis_ctx axes[2]; } primary_owner;
qa_ui_setting_bindings qa_ui_bind_primary_input_settings(
    const qa_ui_primary_input_service *service) {
  primary_owner *o = xcalloc(1, sizeof *o);
  o->state.service = *service;
  o->axes[0].state = &o->state;
  o->axes[0].pitch = false;
  o->axes[1].state = &o->state;
  o->axes[1].pitch = true;
  qa_ui_setting_binding *b = o->state.items;
  b[0].id = "ui:input:sensitivity";
  b[0].label = "Mouse sensitivity";
  b[0].category = "input";
  b[0].group = "Mouse";
  b[0].kind = "slider";
  b[0].minimum = 0.1;
  b[0].maximum = 20;
  b[0].step = 0.1;
  b[0].ctx = &o->state;
  b[0].enabled = always_enabled;
  b[0].read_num = prim_read_sensitivity;
  b[0].write_num = prim_write_sensitivity;
  for (int i = 0; i < 2; i++) {
    qa_ui_setting_binding *axis = &b[1 + i];
    axis->id = i == 0 ? "ui:input:mouse-yaw" : "ui:input:mouse-pitch";
    axis->label = i == 0 ? "Horizontal sensitivity"
                         : "Vertical sensitivity";
    axis->category = "input";
    axis->group = "Mouse";
    axis->kind = "slider";
    axis->minimum = 0;
    axis->maximum = 200;
    axis->step = 1;
    axis->ctx = &o->axes[i];
    axis->enabled = always_enabled;
    axis->read_num = axis_read;
    axis->write_num = axis_write;
    axis->format_value = axis_format;
  }
  b[3].id = "ui:input:invert-mouse";
  b[3].label = "Invert mouse";
  b[3].category = "input";
  b[3].group = "Mouse";
  b[3].kind = "toggle";
  b[3].ctx = &o->state;
  b[3].enabled = always_enabled;
  b[3].read_bool = prim_read_invert;
  b[3].write_bool = prim_write_invert;
  b[4].id = "ui:input:always-run";
  b[4].label = "Always run";
  b[4].category = "input";
  b[4].group = "Movement";
  b[4].kind = "toggle";
  b[4].ctx = &o->state;
  b[4].enabled = always_enabled;
  b[4].read_bool = prim_read_run;
  b[4].write_bool = prim_write_run;
  return bindings_of(o->state.items, 5, o, free_state);
}

/* mouse motion */
typedef struct motion_state {
  qa_ui_mouse_motion_service service;
  bool (*spring_available)(void *);
  void *spring_ctx;
  qa_ui_setting_binding items[5];
} motion_state;
static double mot_read_accel(void *v) {
  motion_state *s = v;
  return s->service.read(s->service.ctx).acceleration;
}
static void mot_write_accel(void *v, double value) {
  motion_state *s = v;
  qa_ui_mouse_motion_settings values = { value, false, false, false, false };
  s->service.write(s->service.ctx, values, QA_UI_MOTION_ACCELERATION);
}
static bool mot_read_filter(void *v) {
  motion_state *s = v;
  return s->service.read(s->service.ctx).filter;
}
static void mot_write_filter(void *v, bool value) {
  motion_state *s = v;
  qa_ui_mouse_motion_settings values = { 0, value, false, false, false };
  s->service.write(s->service.ctx, values, QA_UI_MOTION_FILTER);
}
static bool mot_read_spring(void *v) {
  motion_state *s = v;
  return s->service.read(s->service.ctx).look_spring == true;
}
static void mot_write_spring(void *v, bool value) {
  motion_state *s = v;
  qa_ui_mouse_motion_settings values = { 0, false, false, value, false };
  s->service.write(s->service.ctx, values, QA_UI_MOTION_LOOK_SPRING);
}
static bool mot_spring_enabled(void *v) {
  motion_state *s = v;
  bool available = s->spring_available == NULL
                       ? true
                       : s->spring_available(s->spring_ctx);
  return available && !s->service.read(s->service.ctx).free_look;
}
static bool mot_read_strafe(void *v) {
  motion_state *s = v;
  return s->service.read(s->service.ctx).look_strafe == true;
}
static void mot_write_strafe(void *v, bool value) {
  motion_state *s = v;
  qa_ui_mouse_motion_settings values = { 0, false, false, false, value };
  s->service.write(s->service.ctx, values, QA_UI_MOTION_LOOK_STRAFE);
}
static bool mot_read_free(void *v) {
  motion_state *s = v;
  return s->service.read(s->service.ctx).free_look;
}
static void mot_write_free(void *v, bool value) {
  motion_state *s = v;
  qa_ui_mouse_motion_settings values = { 0, false, value, false, false };
  s->service.write(s->service.ctx, values, QA_UI_MOTION_FREE_LOOK);
}
qa_ui_setting_bindings qa_ui_bind_mouse_motion_settings(
    const qa_ui_mouse_motion_service *service,
    bool (*spring_available)(void *ctx), void *spring_ctx) {
  motion_state *s = xcalloc(1, sizeof *s);
  s->service = *service;
  s->spring_available = spring_available;
  s->spring_ctx = spring_ctx;
  qa_ui_setting_binding *b = s->items;
  b[0].id = "ui:input:acceleration";
  b[0].label = "Mouse acceleration";
  b[0].category = "input";
  b[0].group = "Mouse";
  b[0].kind = "slider";
  b[0].minimum = 0;
  b[0].maximum = 2;
  b[0].step = 0.05;
  b[0].ctx = s;
  b[0].enabled = always_enabled;
  b[0].read_num = mot_read_accel;
  b[0].write_num = mot_write_accel;
  b[1].id = "ui:input:filter";
  b[1].label = "Mouse smoothing";
  b[1].category = "input";
  b[1].group = "Mouse";
  b[1].kind = "toggle";
  b[1].ctx = s;
  b[1].enabled = always_enabled;
  b[1].read_bool = mot_read_filter;
  b[1].write_bool = mot_write_filter;
  b[2].id = "ui:input:lookspring";
  b[2].label = "Look spring";
  b[2].category = "input";
  b[2].group = "Movement";
  b[2].kind = "toggle";
  b[2].ctx = s;
  b[2].enabled = mot_spring_enabled;
  b[2].read_bool = mot_read_spring;
  b[2].write_bool = mot_write_spring;
  b[3].id = "ui:input:lookstrafe";
  b[3].label = "Look strafe";
  b[3].category = "input";
  b[3].group = "Movement";
  b[3].kind = "toggle";
  b[3].ctx = s;
  b[3].enabled = always_enabled;
  b[3].read_bool = mot_read_strafe;
  b[3].write_bool = mot_write_strafe;
  b[4].id = "ui:input:freelook";
  b[4].label = "Free look";
  b[4].category = "input";
  b[4].group = "Movement";
  b[4].kind = "toggle";
  b[4].ctx = s;
  b[4].enabled = always_enabled;
  b[4].read_bool = mot_read_free;
  b[4].write_bool = mot_write_free;
  return bindings_of(s->items, 5, s, free_state);
}

/* music playlist / geometry (direct cvar reads, not bindCvarSetting) */
typedef struct music_state {
  qa_cvar_registry *registry;
  const char *const *(*tracks)(void *, size_t *);
  void *tracks_ctx;
  qa_ui_setting_binding items[2];
  qa_ui_choice *choices;
  size_t choice_capacity;
  char *scratch;
} music_state;
static void registry_set(qa_cvar_registry *registry, const char *name,
                         const char *value) {
  qa_cvar_snapshot out;
  if (qa_cvar_registry_set(registry, name, value, false, &out))
    qa_cvar_read_free(&out);
}
static const char *registry_value(music_state *s, const char *name,
                                  const char *fallback) {
  qa_cvar_read read;
  free(s->scratch);
  if (qa_cvar_registry_find(s->registry, name, &read)) {
    s->scratch = xstrdup(read.value);
    qa_cvar_read_free(&read);
  } else {
    s->scratch = xstrdup(fallback);
  }
  return s->scratch;
}
static bool music_read_shuffle(void *v) {
  music_state *s = v;
  return qa_cvar_registry_variable_value(s->registry, "music_shuffle") != 0;
}
static void music_write_shuffle(void *v, bool value) {
  music_state *s = v;
  registry_set(s->registry, "music_shuffle", value ? "1" : "0");
}
static const char *music_read_track(void *v) {
  return registry_value(v, "music_menu_track", "auto");
}
static void music_write_track(void *v, const char *value) {
  music_state *s = v;
  registry_set(s->registry, "music_menu_track", value);
}
static const qa_ui_choice *music_choices(void *v, size_t *out_count) {
  music_state *s = v;
  size_t track_count = 0;
  const char *const *tracks = s->tracks(s->tracks_ctx, &track_count);
  char *current = xstrdup(registry_value(s, "music_menu_track", "auto"));
  /* Set semantics: tracks deduped in insertion order, then current. */
  size_t wanted = track_count + 3;
  if (s->choice_capacity < wanted) {
    qa_ui_choice *next = realloc(s->choices, wanted * sizeof *next);
    if (next == NULL) oom();
    s->choices = next;
    s->choice_capacity = wanted;
  }
  size_t at = 0;
  s->choices[at++] = (qa_ui_choice){ "auto", "Automatic" };
  s->choices[at++] = (qa_ui_choice){ "0", "Off" };
  for (size_t i = 0; i < track_count; i++) {
    bool seen = false;
    for (size_t j = 2; j < at; j++)
      if (strcmp(s->choices[j].id, tracks[i]) == 0) seen = true;
    if (!seen) s->choices[at++] = (qa_ui_choice){ tracks[i], tracks[i] };
  }
  if (strcmp(current, "auto") != 0 && strcmp(current, "0") != 0) {
    bool seen = false;
    for (size_t j = 2; j < at; j++)
      if (strcmp(s->choices[j].id, current) == 0) seen = true;
    if (!seen) {
      /* The id/label borrow the registry scratch copy; keep a
       * dedicated copy alive in the scratch slot. */
      free(s->scratch);
      s->scratch = current;
      s->choices[at++] = (qa_ui_choice){ s->scratch, s->scratch };
      current = NULL;
    }
  }
  free(current);
  *out_count = at;
  return s->choices;
}
static void music_teardown(void *v) {
  music_state *s = v;
  free(s->choices);
  free(s->scratch);
  free(s);
}
qa_ui_setting_bindings qa_ui_bind_music_playlist_settings(
    qa_cvar_registry *registry,
    const char *const *(*tracks)(void *ctx, size_t *out_count),
    void *tracks_ctx) {
  if (registry == NULL) {
    qa_ui_setting_bindings empty = { NULL, 0, NULL };
    return empty;
  }
  music_state *s = xcalloc(1, sizeof *s);
  s->registry = registry;
  s->tracks = tracks;
  s->tracks_ctx = tracks_ctx;
  qa_ui_setting_binding *b = s->items;
  b[0].id = "ui:audio:shuffle";
  b[0].label = "Shuffle Quake II gameplay music";
  b[0].category = "audio";
  b[0].group = "Music";
  b[0].kind = "toggle";
  b[0].ctx = s;
  b[0].enabled = always_enabled;
  b[0].read_bool = music_read_shuffle;
  b[0].write_bool = music_write_shuffle;
  size_t count = 1;
  if (tracks != NULL) {
    b[1].id = "ui:audio:menu-track";
    b[1].label = "Menu music";
    b[1].category = "audio";
    b[1].group = "Music";
    b[1].kind = "choice";
    b[1].ctx = s;
    b[1].enabled = always_enabled;
    b[1].read_str = music_read_track;
    b[1].write_str = music_write_track;
    b[1].choices = music_choices;
    count = 2;
  }
  return bindings_of(s->items, count, s, music_teardown);
}

typedef struct geometry_state {
  qa_cvar_registry *registry;
  qa_ui_setting_binding items[1];
} geometry_state;
static bool geometry_read(void *v) {
  geometry_state *s = v;
  return qa_cvar_registry_variable_value(s->registry,
                                         "s_geometryAcoustics") != 0;
}
static void geometry_write(void *v, bool value) {
  geometry_state *s = v;
  registry_set(s->registry, "s_geometryAcoustics", value ? "1" : "0");
}
qa_ui_setting_bindings qa_ui_bind_audio_geometry_settings(
    qa_cvar_registry *registry) {
  if (registry == NULL) {
    qa_ui_setting_bindings empty = { NULL, 0, NULL };
    return empty;
  }
  geometry_state *s = xcalloc(1, sizeof *s);
  s->registry = registry;
  qa_ui_setting_binding *b = s->items;
  b[0].id = "ui:audio:geometry";
  b[0].label = "Geometry sound obstruction";
  b[0].category = "audio";
  b[0].group = "Output";
  b[0].kind = "toggle";
  b[0].ctx = s;
  b[0].enabled = always_enabled;
  b[0].read_bool = geometry_read;
  b[0].write_bool = geometry_write;
  return bindings_of(s->items, 1, s, free_state);
}

/* audio settings */
typedef struct audio_state {
  qa_ui_audio_settings_service service;
  bool has_output;
  qa_ui_audio_output_settings output;
  qa_ui_setting_binding items[6];
  qa_ui_choice *choices;
  size_t choice_capacity;
  char **strings;
  size_t string_count, string_capacity;
  char read_buffer[512];
} audio_state;
static char *audio_dup(audio_state *s, const char *text) {
  if (s->string_count == s->string_capacity) {
    size_t cap = s->string_capacity == 0 ? 16 : s->string_capacity * 2;
    char **next = realloc(s->strings, cap * sizeof *next);
    if (next == NULL) oom();
    s->strings = next;
    s->string_capacity = cap;
  }
  char *copy = xstrdup(text);
  s->strings[s->string_count++] = copy;
  return copy;
}
static void audio_clear(audio_state *s) {
  for (size_t i = 0; i < s->string_count; i++) free(s->strings[i]);
  s->string_count = 0;
}
static qa_ui_choice *audio_choice_pool(audio_state *s, size_t count) {
  if (s->choice_capacity < count) {
    qa_ui_choice *next = realloc(s->choices, count * sizeof *next);
    if (next == NULL) oom();
    s->choices = next;
    s->choice_capacity = count;
  }
  return s->choices;
}
static const char *device_read(void *v) {
  audio_state *s = v;
  const char *selected = s->output.selected(s->output.ctx);
  if (selected == NULL) return "default";
  snprintf(s->read_buffer, sizeof s->read_buffer, "device:%s", selected);
  return s->read_buffer;
}
static const qa_ui_choice *device_choices(void *v, size_t *out_count) {
  audio_state *s = v;
  audio_clear(s);
  size_t device_count = 0;
  const char *const *devices = s->output.devices(s->output.ctx,
                                                 &device_count);
  const char *current = s->output.selected(s->output.ctx);
  qa_ui_choice *pool = audio_choice_pool(s, device_count + 2);
  size_t at = 0;
  pool[at++] = (qa_ui_choice){ "default", "System default" };
  bool current_seen = false;
  for (size_t i = 0; i < device_count; i++) {
    bool seen = false;
    for (size_t j = 1; j < at; j++) {
      char buffer[512];
      snprintf(buffer, sizeof buffer, "device:%s", devices[i]);
      if (strcmp(pool[j].id, buffer) == 0) seen = true;
    }
    if (seen) continue;
    char buffer[512];
    snprintf(buffer, sizeof buffer, "device:%s", devices[i]);
    char *id = audio_dup(s, buffer);
    if (current != NULL && strcmp(devices[i], current) == 0)
      current_seen = true;
    pool[at++] = (qa_ui_choice){ id, audio_dup(s, devices[i]) };
  }
  if (current != NULL && !current_seen) {
    char buffer[512];
    snprintf(buffer, sizeof buffer, "device:%s", current);
    pool[at++] = (qa_ui_choice){ audio_dup(s, buffer),
                                 audio_dup(s, current) };
  }
  *out_count = at;
  return pool;
}
static void device_write(void *v, const char *value) {
  audio_state *s = v;
  const char *error = NULL;
  if (strcmp(value, "default") != 0 && strncmp(value, "device:", 7) != 0) {
    error = "Unknown audio output choice";
  } else {
    error = s->output.select(s->output.ctx,
                             strcmp(value, "default") == 0 ? NULL
                                                           : value + 7);
  }
  if (error != NULL) {
    char buffer[1024];
    snprintf(buffer, sizeof buffer, "Audio output selection failed: %s",
             error);
    s->output.report(s->output.ctx, buffer);
  }
}
/* Format field discriminator: 0 rate, 1 bits, 2 channels. */
typedef struct format_ctx { audio_state *state; int field; } format_ctx;
static int64_t format_field(qa_audio_output_format format, int field) {
  return field == 0 ? format.sample_rate
         : field == 1 ? format.sample_bits
                      : format.channels;
}
static const char *format_read(void *v) {
  format_ctx *f = v;
  audio_state *s = f->state;
  char *text = xnumber((double)format_field(
      s->output.format_read(s->output.ctx), f->field));
  snprintf(s->read_buffer, sizeof s->read_buffer, "%s", text);
  free(text);
  return s->read_buffer;
}
static const qa_ui_choice *format_choices(void *v, size_t *out_count) {
  format_ctx *f = v;
  audio_state *s = f->state;
  audio_clear(s);
  static const int64_t bits[2] = { 8, 16 };
  static const int64_t channels[2] = { 1, 2 };
  const int64_t *values = f->field == 0 ? qa_audio_output_rates
                          : f->field == 1 ? bits
                                          : channels;
  size_t value_count = f->field == 0 ? 4 : 2;
  int64_t current = format_field(s->output.format_read(s->output.ctx),
                                 f->field);
  qa_ui_choice *pool = audio_choice_pool(s, value_count + 1);
  size_t at = 0;
  for (size_t i = 0; i <= value_count; i++) {
    int64_t value = i < value_count ? values[i] : current;
    char *id_text = xnumber((double)value);
    bool seen = false;
    for (size_t j = 0; j < at; j++)
      if (strcmp(pool[j].id, id_text) == 0) seen = true;
    if (seen) { free(id_text); continue; }
    char label[96];
    if (f->field == 0) snprintf(label, sizeof label, "%s Hz", id_text);
    else if (f->field == 1) snprintf(label, sizeof label, "%s-bit", id_text);
    else snprintf(label, sizeof label, "%s", value == 1 ? "Mono" : "Stereo");
    pool[at++] = (qa_ui_choice){ audio_dup(s, id_text),
                                 audio_dup(s, label) };
    free(id_text);
  }
  *out_count = at;
  return pool;
}
static void format_write(void *v, const char *value) {
  format_ctx *f = v;
  audio_state *s = f->state;
  qa_audio_output_format next = s->output.format_read(s->output.ctx);
  double number = qa_input_js_parse_number(value);
  if (f->field == 0) next.sample_rate = (int64_t)number;
  else if (f->field == 1) next.sample_bits = (int64_t)number;
  else next.channels = (int64_t)number;
  /* TS audioOutputFormat throws inside the try: report, not abort
   * (qa_audio_output_format_validated aborts, so pre-check here). */
  const char *error = NULL;
  if (next.sample_rate < 8000 || next.sample_rate > 192000
      || (next.channels != 1 && next.channels != 2)
      || (next.sample_bits != 8 && next.sample_bits != 16)) {
    s->output.report(
        s->output.ctx,
        "Audio format selection failed: Audio output requires "
        "8000\xe2\x80\x93" "192000 Hz, 1 or 2 channels, and 8 or 16 bits");
    return;
  }
  qa_audio_output_format validated = qa_audio_output_format_validated(
      next.sample_rate, next.channels, next.sample_bits);
  error = s->output.format_select(s->output.ctx, validated);
  if (error != NULL) {
    char buffer[1024];
    snprintf(buffer, sizeof buffer, "Audio format selection failed: %s",
             error);
    s->output.report(s->output.ctx, buffer);
  }
}
static double audio_read_effects(void *v) {
  audio_state *s = v;
  return s->service.read(s->service.ctx).effects_volume;
}
static void audio_write_effects(void *v, double value) {
  audio_state *s = v;
  qa_ui_audio_settings values = { value, 0 };
  s->service.write(s->service.ctx, values, QA_UI_AUDIO_EFFECTS);
}
static double audio_read_music(void *v) {
  audio_state *s = v;
  return s->service.read(s->service.ctx).music_volume;
}
static void audio_write_music(void *v, double value) {
  audio_state *s = v;
  qa_ui_audio_settings values = { 0, value };
  s->service.write(s->service.ctx, values, QA_UI_AUDIO_MUSIC);
}
typedef struct audio_owner { audio_state state; format_ctx formats[3]; } audio_owner;
static void audio_teardown(void *v) {
  audio_owner *o = v;
  audio_clear(&o->state);
  free(o->state.strings);
  free(o->state.choices);
  free(o);
}
qa_ui_setting_bindings qa_ui_bind_audio_settings(
    const qa_ui_audio_settings_service *service,
    const qa_ui_audio_output_settings *output) {
  audio_owner *o = xcalloc(1, sizeof *o);
  audio_state *s = &o->state;
  s->service = *service;
  qa_ui_setting_binding *b = s->items;
  size_t at = 0;
  if (output != NULL) {
    s->has_output = true;
    s->output = *output;
    qa_ui_setting_binding *device = &b[at++];
    device->id = "ui:audio:device";
    device->label = "Output device";
    device->category = "audio";
    device->group = "Output";
    device->kind = "choice";
    device->ctx = s;
    device->enabled = always_enabled;
    device->read_str = device_read;
    device->choices = device_choices;
    device->write_str = device_write;
    if (output->has_format) {
      static const char *const ids[3] = {
        "ui:audio:rate", "ui:audio:bits", "ui:audio:channels"
      };
      static const char *const labels[3] = {
        "Output sample rate", "Output sample bits", "Output channels"
      };
      for (int i = 0; i < 3; i++) {
        o->formats[i].state = s;
        o->formats[i].field = i;
        qa_ui_setting_binding *binding = &b[at++];
        binding->id = ids[i];
        binding->label = labels[i];
        binding->category = "audio";
        binding->group = "Output";
        binding->kind = "choice";
        binding->ctx = &o->formats[i];
        binding->enabled = always_enabled;
        binding->read_str = format_read;
        binding->choices = format_choices;
        binding->write_str = format_write;
      }
    }
  }
  qa_ui_setting_binding *effects = &b[at++];
  effects->id = "ui:audio:effects";
  effects->label = "Effects volume";
  effects->category = "audio";
  effects->group = "Output";
  effects->kind = "slider";
  effects->minimum = 0;
  effects->maximum = 1;
  effects->step = 0.05;
  effects->ctx = s;
  effects->enabled = always_enabled;
  effects->read_num = audio_read_effects;
  effects->write_num = audio_write_effects;
  qa_ui_setting_binding *music = &b[at++];
  music->id = "ui:audio:music";
  music->label = "Music volume";
  music->category = "audio";
  music->group = "Music";
  music->kind = "slider";
  music->minimum = 0;
  music->maximum = 1;
  music->step = 0.05;
  music->ctx = s;
  music->enabled = always_enabled;
  music->read_num = audio_read_music;
  music->write_num = audio_write_music;
  return bindings_of(s->items, at, o, audio_teardown);
}

/* ---- bindGamepadSettings ---------------------------------------------- */

typedef struct gamepad_state {
  qa_seat_input *input;
  qa_seat_input *(*get_input)(void *);
  void *get_ctx;
  qa_ui_setting_binding items[25];
  char format[96];
} gamepad_state;
static qa_gamepad_input *pad_of(gamepad_state *s) {
  qa_seat_input *input = s->get_input != NULL ? s->get_input(s->get_ctx)
                                              : s->input;
  return qa_seat_input_gamepad(input);
}
static void pad_update(gamepad_state *s, qa_gamepad_tuning next) {
  qa_validate_gamepad_tuning(&next);
  pad_of(s)->tuning = next;
}
static void pad_curve(gamepad_state *s, bool look, qa_stick_curve curve) {
  qa_gamepad_tuning next = pad_of(s)->tuning;
  if (look) next.look = curve; else next.move = curve;
  pad_update(s, next);
}
/* Per-binding discriminator ctx. */
typedef struct pad_ctx { gamepad_state *state; int field; } pad_ctx;
enum {
  PAD_INVERT, PAD_SWAP, PAD_LOOK_SPEED, PAD_PITCH_SPEED, PAD_MOVE_DEADZONE,
  PAD_LOOK_DEADZONE, PAD_LOOK_CURVE, PAD_TRIGGER, PAD_MOVE_CURVE,
  PAD_FORWARD_SENS, PAD_SIDE_SENS,
  PAD_MOVE_CURVE_TYPE, PAD_MOVE_OUTER, PAD_MOVE_PREVIEW_X, PAD_MOVE_PREVIEW_Y,
  PAD_LOOK_CURVE_TYPE, PAD_LOOK_OUTER, PAD_LOOK_PREVIEW_X, PAD_LOOK_PREVIEW_Y
};
static bool pad_read_bool(void *v) {
  pad_ctx *c = v;
  qa_gamepad_tuning t = pad_of(c->state)->tuning;
  return c->field == PAD_INVERT ? t.invert_pitch : t.swap_sticks;
}
static void pad_write_bool(void *v, bool value) {
  pad_ctx *c = v;
  qa_gamepad_tuning next = pad_of(c->state)->tuning;
  if (c->field == PAD_INVERT) next.invert_pitch = value;
  else next.swap_sticks = value;
  pad_update(c->state, next);
}
static double pad_read_num(void *v) {
  pad_ctx *c = v;
  gamepad_state *s = c->state;
  qa_gamepad_tuning t = pad_of(s)->tuning;
  switch (c->field) {
    case PAD_LOOK_SPEED: return t.yaw_degrees_per_second;
    case PAD_PITCH_SPEED: return t.pitch_degrees_per_second;
    case PAD_MOVE_DEADZONE: return t.move.deadzone;
    case PAD_LOOK_DEADZONE: return t.look.deadzone;
    case PAD_LOOK_CURVE: return t.look.exponent;
    case PAD_TRIGGER: return t.trigger_threshold;
    case PAD_MOVE_CURVE: return t.move.exponent;
    case PAD_FORWARD_SENS: return t.forward_sensitivity;
    case PAD_SIDE_SENS: return t.side_sensitivity;
    case PAD_MOVE_OUTER:
      return t.move.kind == QA_STICK_CURVE_RADIAL ? t.move.outer_threshold
                                                  : 0;
    case PAD_LOOK_OUTER:
      return t.look.kind == QA_STICK_CURVE_RADIAL ? t.look.outer_threshold
                                                  : 0;
    case PAD_MOVE_PREVIEW_X:
      return qa_gamepad_input_preview(pad_of(s)).move_curved.x;
    case PAD_MOVE_PREVIEW_Y:
      return qa_gamepad_input_preview(pad_of(s)).move_curved.y;
    case PAD_LOOK_PREVIEW_X:
      return qa_gamepad_input_preview(pad_of(s)).look_curved.x;
    default:
      return qa_gamepad_input_preview(pad_of(s)).look_curved.y;
  }
}
static void pad_write_num(void *v, double value) {
  pad_ctx *c = v;
  gamepad_state *s = c->state;
  qa_gamepad_tuning t = pad_of(s)->tuning;
  switch (c->field) {
    case PAD_LOOK_SPEED: t.yaw_degrees_per_second = value; pad_update(s, t); break;
    case PAD_PITCH_SPEED: t.pitch_degrees_per_second = value; pad_update(s, t); break;
    case PAD_TRIGGER: t.trigger_threshold = value; pad_update(s, t); break;
    case PAD_FORWARD_SENS: t.forward_sensitivity = value; pad_update(s, t); break;
    case PAD_SIDE_SENS: t.side_sensitivity = value; pad_update(s, t); break;
    case PAD_MOVE_DEADZONE: {
      qa_stick_curve curve = t.move;
      curve.deadzone = curve.kind == QA_STICK_CURVE_RADIAL
                           ? fmin(value, 0.999 - curve.outer_threshold)
                           : value;
      pad_curve(s, false, curve);
      break;
    }
    case PAD_LOOK_DEADZONE: {
      qa_stick_curve curve = t.look;
      curve.deadzone = curve.kind == QA_STICK_CURVE_RADIAL
                           ? fmin(value, 0.999 - curve.outer_threshold)
                           : value;
      pad_curve(s, true, curve);
      break;
    }
    case PAD_LOOK_CURVE: {
      qa_stick_curve curve = t.look;
      curve.exponent = value;
      pad_curve(s, true, curve);
      break;
    }
    case PAD_MOVE_CURVE: {
      qa_stick_curve curve = t.move;
      curve.exponent = value;
      pad_curve(s, false, curve);
      break;
    }
    case PAD_MOVE_OUTER: {
      qa_stick_curve curve = t.move;
      if (curve.kind == QA_STICK_CURVE_RADIAL) {
        curve.outer_threshold = fmin(value, 0.999 - curve.deadzone);
        pad_curve(s, false, curve);
      }
      break;
    }
    case PAD_LOOK_OUTER: {
      qa_stick_curve curve = t.look;
      if (curve.kind == QA_STICK_CURVE_RADIAL) {
        curve.outer_threshold = fmin(value, 0.999 - curve.deadzone);
        pad_curve(s, true, curve);
      }
      break;
    }
    default: break; /* previews: TS write is a no-op */
  }
}
static bool pad_outer_enabled(void *v) {
  pad_ctx *c = v;
  qa_gamepad_tuning t = pad_of(c->state)->tuning;
  qa_stick_curve curve = c->field == PAD_MOVE_OUTER ? t.move : t.look;
  return curve.kind == QA_STICK_CURVE_RADIAL;
}
static bool never_enabled(void *ctx) { (void)ctx; return false; }
static const char *pad_preview_format(void *v, double value) {
  pad_ctx *c = v;
  gamepad_state *s = c->state;
  qa_gamepad_preview preview = qa_gamepad_input_preview(pad_of(s));
  double raw = c->field == PAD_MOVE_PREVIEW_X ? preview.move_raw.x
               : c->field == PAD_MOVE_PREVIEW_Y ? preview.move_raw.y
               : c->field == PAD_LOOK_PREVIEW_X ? preview.look_raw.x
                                                : preview.look_raw.y;
  snprintf(s->format, sizeof s->format, "Raw %.2f / %.2f", raw, value);
  return s->format;
}
static const char *pad_curve_read(void *v) {
  pad_ctx *c = v;
  qa_gamepad_tuning t = pad_of(c->state)->tuning;
  qa_stick_curve curve = c->field == PAD_MOVE_CURVE_TYPE ? t.move : t.look;
  return curve.kind == QA_STICK_CURVE_RADIAL ? "radial" : "axial";
}
static void pad_curve_write(void *v, const char *value) {
  pad_ctx *c = v;
  if (strcmp(value, "radial") != 0 && strcmp(value, "axial") != 0)
    qa_range_error("Unknown controller curve");
  bool look = c->field == PAD_LOOK_CURVE_TYPE;
  qa_gamepad_tuning t = pad_of(c->state)->tuning;
  qa_stick_curve previous = look ? t.look : t.move;
  qa_stick_curve curve = previous;
  if (strcmp(value, "radial") == 0) {
    curve.kind = QA_STICK_CURVE_RADIAL;
    curve.outer_threshold = fmin(0.02, (1 - previous.deadzone) / 2);
  } else {
    curve.kind = QA_STICK_CURVE_AXIAL;
  }
  pad_curve(c->state, look, curve);
}
static const qa_ui_choice *pad_curve_choices(void *ctx, size_t *out_count) {
  (void)ctx;
  static const qa_ui_choice choices[2] = {
    { "radial", "Radial" }, { "axial", "Axial" }
  };
  *out_count = 2;
  return choices;
}
typedef struct gamepad_owner { gamepad_state state; pad_ctx ctxs[25]; } gamepad_owner;
static qa_ui_setting_bindings gamepad_build(gamepad_owner *o) {
  gamepad_state *s = &o->state;
  struct row { const char *id, *label, *kind; double min, max, step; int field; };
  static const struct row fixed[11] = {
    { "ui:input:invert-controller", "Invert controller", "toggle", 0, 0, 0, PAD_INVERT },
    { "ui:input:swap-sticks", "Swap controller sticks", "toggle", 0, 0, 0, PAD_SWAP },
    { "ui:input:look-speed", "Controller turn speed", "slider", 30, 720, 10, PAD_LOOK_SPEED },
    { "ui:input:pitch-speed", "Controller look speed", "slider", 30, 720, 10, PAD_PITCH_SPEED },
    { "ui:input:move-deadzone", "Move stick deadzone", "slider", 0, 0.5, 0.01, PAD_MOVE_DEADZONE },
    { "ui:input:look-deadzone", "Look stick deadzone", "slider", 0, 0.5, 0.01, PAD_LOOK_DEADZONE },
    { "ui:input:look-curve", "Look response curve", "slider", 0.5, 4, 0.1, PAD_LOOK_CURVE },
    { "ui:input:trigger", "Trigger threshold", "slider", 0.05, 0.95, 0.05, PAD_TRIGGER },
    { "ui:input:move-curve", "Move response curve", "slider", 0.5, 4, 0.1, PAD_MOVE_CURVE },
    { "ui:input:forward-sensitivity", "Forward controller sensitivity", "slider", 0, 3, 0.05, PAD_FORWARD_SENS },
    { "ui:input:side-sensitivity", "Side controller sensitivity", "slider", 0, 3, 0.05, PAD_SIDE_SENS },
  };
  size_t at = 0;
  for (size_t i = 0; i < 11; i++) {
    o->ctxs[at].state = s;
    o->ctxs[at].field = fixed[i].field;
    qa_ui_setting_binding *b = &s->items[at];
    b->id = fixed[i].id;
    b->label = fixed[i].label;
    b->category = "input";
    b->group = "Gamepad";
    b->kind = fixed[i].kind;
    b->minimum = fixed[i].min;
    b->maximum = fixed[i].max;
    b->step = fixed[i].step;
    b->ctx = &o->ctxs[at];
    b->enabled = always_enabled;
    b->read_bool = pad_read_bool;
    b->write_bool = pad_write_bool;
    b->read_num = pad_read_num;
    b->write_num = pad_write_num;
    at++;
  }
  static const struct row per_stick[2][4] = {
    { { "ui:input:move-curve-type", "Move deadzone shape", "choice", 0, 0, 0, PAD_MOVE_CURVE_TYPE },
      { "ui:input:move-outer", "Move outer threshold", "slider", 0, 0.49, 0.01, PAD_MOVE_OUTER },
      { "ui:input:move-preview-x", "Move X live", "slider", -1, 1, 0.01, PAD_MOVE_PREVIEW_X },
      { "ui:input:move-preview-y", "Move Y live", "slider", -1, 1, 0.01, PAD_MOVE_PREVIEW_Y } },
    { { "ui:input:look-curve-type", "Look deadzone shape", "choice", 0, 0, 0, PAD_LOOK_CURVE_TYPE },
      { "ui:input:look-outer", "Look outer threshold", "slider", 0, 0.49, 0.01, PAD_LOOK_OUTER },
      { "ui:input:look-preview-x", "Look X live", "slider", -1, 1, 0.01, PAD_LOOK_PREVIEW_X },
      { "ui:input:look-preview-y", "Look Y live", "slider", -1, 1, 0.01, PAD_LOOK_PREVIEW_Y } },
  };
  /* TS order: the per-stick tuning rows are appended after the
   * fixed rows, move stick first. */
  for (int stick = 0; stick < 2; stick++) {
    for (int i = 0; i < 4; i++) {
      const struct row *r = &per_stick[stick][i];
      o->ctxs[at].state = s;
      o->ctxs[at].field = r->field;
      qa_ui_setting_binding *b = &s->items[at];
      b->id = r->id;
      b->label = r->label;
      b->category = "input";
      b->group = "Gamepad";
      b->kind = r->kind;
      b->minimum = r->min;
      b->maximum = r->max;
      b->step = r->step;
      b->ctx = &o->ctxs[at];
      if (i == 0) {
        b->enabled = always_enabled;
        b->read_str = pad_curve_read;
        b->write_str = pad_curve_write;
        b->choices = pad_curve_choices;
      } else if (i == 1) {
        b->enabled = pad_outer_enabled;
        b->read_num = pad_read_num;
        b->write_num = pad_write_num;
      } else {
        b->enabled = never_enabled;
        b->read_num = pad_read_num;
        b->write_num = pad_write_num;
        b->format_value = pad_preview_format;
      }
      at++;
    }
  }
  return bindings_of(s->items, at, o, free_state);
}
qa_ui_setting_bindings qa_ui_bind_gamepad_settings(qa_seat_input *input) {
  gamepad_owner *o = xcalloc(1, sizeof *o);
  o->state.input = input;
  return gamepad_build(o);
}
qa_ui_setting_bindings qa_ui_bind_gamepad_settings_fn(
    qa_seat_input *(*get_input)(void *ctx), void *ctx) {
  gamepad_owner *o = xcalloc(1, sizeof *o);
  o->state.get_input = get_input;
  o->state.get_ctx = ctx;
  return gamepad_build(o);
}

/* ---- bindInputSettings ------------------------------------------------ */

typedef struct input_settings_owner {
  qa_input_command_builder *builder;
  qa_seat_input *input;
  qa_ui_setting_bindings parts[4];
  size_t part_count;
  qa_ui_setting_binding *items;
  qa_ui_primary_input_service primary;
  qa_ui_mouse_motion_service motion;
} input_settings_owner;
static qa_ui_primary_input_settings input_primary_read(void *v) {
  input_settings_owner *o = v;
  qa_mouse_input *mouse = qa_input_command_builder_mouse(o->builder);
  qa_mouse_tuning tuning = qa_mouse_input_tuning(mouse);
  qa_view_input_tuning view = qa_input_command_builder_tuning(o->builder);
  qa_ui_primary_input_settings out = {
    tuning.sensitivity, tuning.pitch, tuning.yaw, tuning.invert_pitch,
    view.always_run
  };
  return out;
}
static void input_primary_write(void *v, qa_ui_primary_input_settings values,
                                unsigned mask) {
  input_settings_owner *o = v;
  qa_mouse_input *mouse = qa_input_command_builder_mouse(o->builder);
  qa_mouse_tuning tuning = qa_mouse_input_tuning(mouse);
  if (mask & QA_UI_PRIMARY_SENSITIVITY) tuning.sensitivity = values.sensitivity;
  if (mask & QA_UI_PRIMARY_PITCH) tuning.pitch = values.pitch;
  if (mask & QA_UI_PRIMARY_YAW) tuning.yaw = values.yaw;
  if (mask & QA_UI_PRIMARY_INVERT) tuning.invert_pitch = values.invert_mouse;
  qa_mouse_input_set_tuning(mouse, tuning);
  if (mask & QA_UI_PRIMARY_ALWAYS_RUN) {
    qa_view_input_tuning view = qa_input_command_builder_tuning(o->builder);
    view.always_run = values.always_run;
    qa_input_command_builder_set_tuning(o->builder, view);
  }
}
static qa_ui_mouse_motion_settings input_motion_read(void *v) {
  input_settings_owner *o = v;
  qa_mouse_tuning tuning = qa_mouse_input_tuning(
      qa_input_command_builder_mouse(o->builder));
  qa_ui_mouse_motion_settings out = {
    tuning.acceleration, tuning.filter, tuning.free_look,
    tuning.look_spring, tuning.look_strafe
  };
  return out;
}
static void input_motion_write(void *v, qa_ui_mouse_motion_settings values,
                               unsigned mask) {
  input_settings_owner *o = v;
  qa_mouse_input *mouse = qa_input_command_builder_mouse(o->builder);
  qa_mouse_tuning tuning = qa_mouse_input_tuning(mouse);
  if (mask & QA_UI_MOTION_ACCELERATION) tuning.acceleration = values.acceleration;
  if (mask & QA_UI_MOTION_FILTER) tuning.filter = values.filter;
  if (mask & QA_UI_MOTION_FREE_LOOK) tuning.free_look = values.free_look;
  if (mask & QA_UI_MOTION_LOOK_SPRING) tuning.look_spring = values.look_spring;
  if (mask & QA_UI_MOTION_LOOK_STRAFE) tuning.look_strafe = values.look_strafe;
  qa_mouse_input_set_tuning(mouse, tuning);
}
static bool input_spring_available(void *v) {
  input_settings_owner *o = v;
  qa_command_dialect dialect = qa_input_command_builder_dialect(o->builder);
  return dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE
         || dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
}
static void input_settings_teardown(void *v) {
  input_settings_owner *o = v;
  for (size_t i = 0; i < o->part_count; i++)
    qa_ui_setting_bindings_free(&o->parts[i]);
  free(o->items);
  free(o);
}
qa_ui_setting_bindings qa_ui_bind_input_settings(
    qa_seat_input *input, qa_input_command_builder *builder,
    const qa_ui_controller_vibration_service *vibration) {
  input_settings_owner *o = xcalloc(1, sizeof *o);
  o->builder = builder;
  o->input = input;
  o->primary.ctx = o;
  o->primary.read = input_primary_read;
  o->primary.write = input_primary_write;
  o->motion.ctx = o;
  o->motion.read = input_motion_read;
  o->motion.write = input_motion_write;
  size_t total = 0;
  if (vibration != NULL)
    o->parts[o->part_count++] = qa_ui_bind_controller_vibration(vibration);
  o->parts[o->part_count++] = qa_ui_bind_primary_input_settings(&o->primary);
  o->parts[o->part_count++] = qa_ui_bind_mouse_motion_settings(
      &o->motion, input_spring_available, o);
  o->parts[o->part_count++] = qa_ui_bind_gamepad_settings(input);
  for (size_t i = 0; i < o->part_count; i++) total += o->parts[i].count;
  o->items = xmalloc(total * sizeof *o->items);
  size_t at = 0;
  for (size_t i = 0; i < o->part_count; i++) {
    memcpy(o->items + at, o->parts[i].items,
           o->parts[i].count * sizeof *o->items);
    at += o->parts[i].count;
  }
  return bindings_of(o->items, total, o, input_settings_teardown);
}

/* ---- SeatUiPreferences ------------------------------------------------ */

typedef struct pref_ctx {
  qa_ui_seat_preferences *prefs;
  int field;
} pref_ctx;
enum {
  PREF_HUD = 0, PREF_TEXT, PREF_MENU, PREF_CROSSHAIR_SIZE,
  PREF_HIGH_CONTRAST, PREF_REDUCED, PREF_CAPTIONS, PREF_CROSSHAIR,
  PREF_TYPEFACE, PREF_COLOR_MODE, PREF_RESET
};
static void prefs_set(qa_ui_seat_preferences *prefs,
                      qa_ui_preference_values values) {
  qa_ui_seat_preferences_set_values(prefs, values);
}
static double pref_read_num(void *v) {
  pref_ctx *c = v;
  qa_ui_preference_values values = qa_ui_seat_preferences_values(c->prefs);
  switch (c->field) {
    case PREF_HUD: return values.hud_scale;
    case PREF_TEXT: return values.text_scale;
    case PREF_MENU: return values.menu_scale;
    default: return values.crosshair_size;
  }
}
static void pref_write_num(void *v, double value) {
  pref_ctx *c = v;
  qa_ui_preference_values values = qa_ui_seat_preferences_values(c->prefs);
  switch (c->field) {
    case PREF_HUD: values.hud_scale = value; break;
    case PREF_TEXT: values.text_scale = value; break;
    case PREF_MENU: values.menu_scale = value; break;
    default: values.crosshair_size = value; break;
  }
  prefs_set(c->prefs, values);
}
static bool pref_read_bool(void *v) {
  pref_ctx *c = v;
  qa_ui_preference_values values = qa_ui_seat_preferences_values(c->prefs);
  switch (c->field) {
    case PREF_HIGH_CONTRAST: return values.high_contrast;
    case PREF_REDUCED: return values.reduced_flashes;
    case PREF_CAPTIONS: return values.captions;
    default: return values.crosshair;
  }
}
static void pref_write_bool(void *v, bool value) {
  pref_ctx *c = v;
  qa_ui_preference_values values = qa_ui_seat_preferences_values(c->prefs);
  switch (c->field) {
    case PREF_HIGH_CONTRAST: values.high_contrast = value; break;
    case PREF_REDUCED: values.reduced_flashes = value; break;
    case PREF_CAPTIONS: values.captions = value; break;
    default: values.crosshair = value; break;
  }
  prefs_set(c->prefs, values);
}
static const char *pref_read_str(void *v) {
  pref_ctx *c = v;
  qa_ui_preference_values values = qa_ui_seat_preferences_values(c->prefs);
  return c->field == PREF_TYPEFACE ? values.typeface : values.color_mode;
}
static void pref_write_str(void *v, const char *value) {
  pref_ctx *c = v;
  qa_ui_preference_values values = qa_ui_seat_preferences_values(c->prefs);
  if (c->field == PREF_TYPEFACE) {
    if (strcmp(value, "standard") != 0 && strcmp(value, "bold") != 0)
      qa_range_error("Unknown typeface");
    values.typeface = strcmp(value, "bold") == 0 ? "bold" : "standard";
  } else {
    if (strcmp(value, "standard") != 0 && strcmp(value, "blue-yellow") != 0
        && strcmp(value, "monochrome") != 0)
      qa_range_error("Unknown interface colors");
    values.color_mode = strcmp(value, "blue-yellow") == 0 ? "blue-yellow"
                        : strcmp(value, "monochrome") == 0 ? "monochrome"
                                                            : "standard";
  }
  prefs_set(c->prefs, values);
}
static const qa_ui_choice *pref_choices(void *v, size_t *out_count) {
  pref_ctx *c = v;
  static const qa_ui_choice typeface[2] = {
    { "standard", "Standard" }, { "bold", "Bold" }
  };
  static const qa_ui_choice color_mode[3] = {
    { "standard", "Standard" }, { "blue-yellow", "Blue and yellow" },
    { "monochrome", "Monochrome" }
  };
  if (c->field == PREF_TYPEFACE) { *out_count = 2; return typeface; }
  *out_count = 3;
  return color_mode;
}
static void pref_reset(void *v) {
  pref_ctx *c = v;
  prefs_set(c->prefs, qa_ui_default_preferences);
}

void qa_ui_seat_preferences_init(qa_ui_seat_preferences *prefs,
                                 qa_seat_id seat, qa_cvar_registry *cvars) {
  memset(prefs, 0, sizeof *prefs);
  prefs->seat = seat;
  prefs->cvars = cvars;
  prefs->local = qa_ui_default_preferences;
  static const struct {
    const char *id, *label, *kind;
    double min, max, step;
    int field;
  } rows[QA_UI_PREFERENCE_BINDING_COUNT] = {
    { "ui:accessibility:typeface", "Typeface", "choice", 0, 0, 0, PREF_TYPEFACE },
    { "ui:accessibility:colorMode", "Interface colors", "choice", 0, 0, 0, PREF_COLOR_MODE },
    { "ui:accessibility:hudScale", "HUD size", "slider", 0.5, 1.5, 0.05, PREF_HUD },
    { "ui:accessibility:textScale", "Text size", "slider", 0.75, 2, 0.05, PREF_TEXT },
    { "ui:accessibility:menuScale", "Menu size", "slider", 0.75, 1, 0.05, PREF_MENU },
    { "ui:accessibility:crosshairSize", "Crosshair size", "slider", 2, 32, 1, PREF_CROSSHAIR_SIZE },
    { "ui:accessibility:highContrast", "High contrast", "toggle", 0, 0, 0, PREF_HIGH_CONTRAST },
    { "ui:accessibility:reducedFlashes", "Reduce HUD flashes", "toggle", 0, 0, 0, PREF_REDUCED },
    { "ui:accessibility:captions", "Captions", "toggle", 0, 0, 0, PREF_CAPTIONS },
    { "ui:accessibility:crosshair", "Crosshair", "toggle", 0, 0, 0, PREF_CROSSHAIR },
    { "ui:accessibility:reset", "Reset accessibility settings", "button", 0, 0, 0, PREF_RESET },
  };
  for (size_t i = 0; i < QA_UI_PREFERENCE_BINDING_COUNT; i++) {
    pref_ctx *ctx = xmalloc(sizeof *ctx);
    ctx->prefs = prefs;
    ctx->field = rows[i].field;
    prefs->binding_ctx[i] = ctx;
    qa_ui_setting_binding *b = &prefs->bindings[i];
    b->id = rows[i].id;
    b->label = rows[i].label;
    b->category = "accessibility";
    b->kind = rows[i].kind;
    b->minimum = rows[i].min;
    b->maximum = rows[i].max;
    b->step = rows[i].step;
    b->ctx = ctx;
    b->enabled = always_enabled;
    b->read_num = pref_read_num;
    b->write_num = pref_write_num;
    b->read_bool = pref_read_bool;
    b->write_bool = pref_write_bool;
    b->read_str = pref_read_str;
    b->write_str = pref_write_str;
    b->choices = pref_choices;
    b->activate = pref_reset;
  }
}
qa_ui_preference_values qa_ui_seat_preferences_values(
    const qa_ui_seat_preferences *prefs) {
  return prefs->cvars == NULL
             ? prefs->local
             : qa_ui_read_preferences(prefs->cvars, prefs->seat.index);
}
void qa_ui_seat_preferences_set_values(qa_ui_seat_preferences *prefs,
                                       qa_ui_preference_values values) {
  if (prefs->cvars == NULL) prefs->local = values;
  else qa_ui_write_preferences(prefs->cvars, prefs->seat.index, values);
}
const qa_ui_setting_binding *qa_ui_seat_preferences_bindings(
    qa_ui_seat_preferences *prefs, size_t *out_count) {
  *out_count = QA_UI_PREFERENCE_BINDING_COUNT;
  return prefs->bindings;
}
