/* Port of src/app/bootstrap/startup-menu.ts: the StartupMenu class over the
 * NativeUiController, the StartupSelectionModel, the menu art and fonts.
 * Menu factories are rebuilt on every controller read exactly as the TS
 * factory closures are. Each factory's controls, strings and closure records
 * live in a per-menu pool that is retired when that factory runs again and
 * freed at the next input()/draw() entry, so a control's closure record stays
 * valid while its own callback reopens or rebuilds the same menu.
 * Async model preparations run synchronously (startup_menu.h).
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/startup_menu.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/input/key_codes.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/common/js_number.h"
#include "qa/network/q1/profile.h"
#include "qa/text/layout.h"
#include "qa/text/ui.h"
#include "qa/ui/common/accessibility.h"
#include "qa/ui/common/captions.h"
#include "qa/ui/common/draw.h"
#include "qa/ui/common/layout.h"
#include "qa/ui/common/menu_theme.h"
#include "qa/ui/settings/accessibility.h"
#include "qa/ui/settings/llm.h"

static const qa_ui_menu_id ID_LIBRARY = "menu:startup:library";
static const qa_ui_menu_id ID_MAIN = "menu:startup:main";
static const qa_ui_menu_id ID_BROWSER = "menu:startup:servers";
static const qa_ui_menu_id ID_BROWSER_OPTS = "menu:startup:server-filters";
static const qa_ui_menu_id ID_BROWSER_DET = "menu:startup:server-details";
static const qa_ui_menu_id ID_FAMILY = "menu:startup:native-family";
static const qa_ui_menu_id ID_CAMPAIGN = "menu:startup:native-campaign";
static const qa_ui_menu_id ID_DIFFICULTY = "menu:startup:native-difficulty";
static const qa_ui_menu_id ID_SESSION = "menu:startup:session";
static const qa_ui_menu_id ID_HOSTING = "menu:startup:hosting";
static const qa_ui_menu_id ID_OPTIONS = "menu:startup:options";
static const qa_ui_menu_id ID_DISPLAY = "menu:settings:display:0";
static const qa_ui_menu_id ID_SOUND = "menu:startup:sound";
static const qa_ui_menu_id ID_CONTROLS = "menu:settings:input:0";
static const qa_ui_menu_id ID_ACCESS = "menu:settings:accessibility:0";
static const qa_ui_menu_id ID_SELECT = "menu:startup:select";
static const qa_ui_menu_id ID_ROSTER = "menu:startup:roster";
static const qa_ui_menu_id ID_LOAD = "menu:startup:load";
static const qa_ui_menu_id ID_ARENAS = "menu:library:arena-selection";

enum { SLOT_MAIN, SLOT_LIBRARY, SLOT_FAMILY, SLOT_CAMPAIGN, SLOT_DIFFICULTY,
       SLOT_BROWSER, SLOT_BROWSER_OPTS, SLOT_BROWSER_DET, SLOT_SESSION,
       SLOT_HOSTING, SLOT_OPTIONS, SLOT_SOUND, SLOT_SELECT, SLOT_ROSTER,
       SLOT_LOAD, SLOT_COUNT };

enum { ACT_OPEN, ACT_CLOSE_MENU, ACT_QUIT, ACT_PLAY, ACT_LOAD_MENU, ACT_FAMILY,
       ACT_CUSTOM, ACT_EDITION, ACT_PRESET, ACT_CAMPAIGN_PAGE, ACT_DIFFICULTY,
       ACT_PLAY_PRESET, ACT_SAVE_LOAD, ACT_SAVE_PAGE, ACT_REFRESH_SAVES,
       ACT_HOST_APPLY, ACT_HOST_KIND, ACT_HOST_PORT, ACT_HOST_Q1,
       ACT_SELECT_CHOICE, ACT_ROSTER_CLASS, ACT_ROSTER_SOURCE, ACT_ROW_SELECT,
       ACT_FIELD_SELECT, ACT_GRAPPLE, ACT_GRENADES, ACT_TEAM,
       ACT_BUILDER_OPEN, ACT_BUILDER_CHOICE, ACT_BUILDER_GROUP,
       ACT_BUILDER_STAGE_BACK, ACT_BUILDER_EDITION, ACT_PREVIEW_DONE,
       ACT_FOOTER_HOSTING, ACT_FOOTER_MODS, ACT_FOOTER_BROWSE, ACT_FOOTER_LOBBY,
       ACT_BROWSER_PROTOCOL, ACT_BROWSER_ADDRESS, ACT_BROWSER_QUERY,
       ACT_BROWSER_SCAN, ACT_BROWSER_FAVORITE, ACT_BROWSER_FILTER,
       ACT_BROWSER_ENTRY, ACT_BROWSER_PAGE, ACT_BROWSER_FAV_ONLY,
       ACT_BROWSER_CONNECT, ACT_BROWSER_DETAILS, ACT_BROWSER_SORT,
       ACT_BROWSER_HIDE_EMPTY, ACT_BROWSER_HIDE_FULL, ACT_BROWSER_MASTER,
       ACT_BROWSER_DISCOVER, ACT_BROWSER_DETAIL_PAGE, ACT_NOOP };

/* A closure record: the TS arrow function a control carries. */
typedef struct action {
  qa_app_startup_menu *m; int kind; const char *a; const char *b; bool flag;
} action;

/* Owned blocks of one factory generation. */
typedef struct pool { void **blocks; size_t count, cap; } pool;
typedef struct scratch {
  pool current;   /* this generation's strings, closure records, arrays */
  qa_ui_control *controls; size_t count, cap;
  qa_ui_menu_scroll scroll;
} scratch;

typedef struct slot { qa_app_startup_menu *m; int index; int handle; scratch sc; } slot;

typedef struct dd_entry {
  bool header; const char *group; bool expanded;
  const qa_app_startup_selection_choice *choice;
} dd_entry;

/* TS BuilderLayout pieces. Controls are separate pool blocks. */
typedef struct builder_field {
  qa_ui_control *control; qa_rect rect; const char *label;
  bool has_value; const char *value_text; double value_width;
} builder_field;
typedef struct builder_head { double x, y; const char *label; } builder_head;
typedef struct builder_item {
  qa_ui_control *control; const char *choice; const char *label;
  bool selected, header, indent; const char *unavailable;
} builder_item;
typedef struct builder_dropdown {
  double x; const char *title; double item_scale, items_top;
  double scroll_offset, scroll_visible, scroll_total;
  bool has_toggle; qa_ui_control *classic, *rerelease; bool classic_selected, rerelease_selected;
  builder_item *items; size_t item_count;
} builder_dropdown;
typedef struct footer_entry { qa_ui_control *control; qa_rect rect; const char *label; } footer_entry;
typedef struct viewer_spec {
  const char *resource; qa_rect panel, rect; const char *label; qa_ui_control *control;
} viewer_spec;
typedef struct builder_layout {
  builder_field *fields; size_t field_count;
  builder_head heads[4]; size_t head_count;
  bool has_dropdown; builder_dropdown dropdown;
  footer_entry footer[6]; size_t footer_count;
  bool has_viewer; viewer_spec viewer;
} builder_layout;

/* TS BuilderOpenDropdown (active false is TS null). */
typedef struct open_dropdown {
  bool active;
  char field[32], stage[32];
  double scroll;
  bool has_edition; char edition[16];
  char **collapsed; size_t collapsed_count;
} open_dropdown;

struct qa_app_startup_menu {
  qa_app_startup_menu_options o;
  qa_native_ui_controller *controller;
  qa_ui_text_renderer text;
  qa_ui_skin skin;
  slot slots[SLOT_COUNT];
  pool retired;     /* retired factory generations, freed at input()/draw() entry */
  scratch draw_sc;  /* draw()'s own builder layouts and strings */
  char *status; bool busy;
  char field[32]; double page;
  int monster_kind; /* 0 none, 1 source, 2 class */
  char *monster_classname; /* NULL is TS null */
  double save_page, campaign_page;
  char native_family[4], native_edition[16], native_skill[32];
  char *native_preset_id, *native_arena; /* NULL is TS null / undefined */
  qa_app_startup_hosting host_draft; char host_port[32];
  open_dropdown dd;
  bool preview_armed;
  qa_ui_menu_id gyro_menu, binding_menu, lobby_menu, mods_menu, llm_root;
  struct { const char *name; const char *label; qa_ui_menu_id root;
           qa_ui_settings_menus menus; } library_pages[6];
  size_t library_page_count;
  qa_app_arena_selection_menu *arena_menu;
  qa_ui_settings_menus settings_menus, mods_menus, lobby_menus, gyro_menus, binding_menus;
  qa_ui_llm_settings_menus llm_menus;
  bool has_settings, has_mods, has_lobby_menus, has_gyro, has_bindings, has_llm, disposed;
  qa_ui_setting_binding *all_bindings; size_t all_binding_count;
  /* Rebuilt-per-call views. */
  const qa_app_startup_native_preset **campaigns; size_t campaign_cap;
  dd_entry *entries; size_t entry_cap;
  qa_app_startup_selection_row sel_row;
  char browser_sort[32]; /* TS browser.sortOrder (the C browser keeps no getter) */
  /* captionCommands() result. */
  qa_render_command *captions; size_t caption_count, caption_cap;
};

/* ---- allocation ------------------------------------------------------------------ */
static void *xgrow(void *p, size_t *cap, size_t need, size_t size) {
  if (*cap >= need) return p;
  size_t next = *cap ? *cap : 16;
  while (next < need) next *= 2;
  void *grown = realloc(p, next * size);
  if (!grown) qa_range_error("Out of memory");
  *cap = next; return grown;
}
static char *xdup(const char *s) {
  char *r = strdup(s ? s : "");
  if (!r) qa_range_error("Out of memory");
  return r;
}
static void set_str(char **dst, const char *src) { char *next = src ? xdup(src) : NULL; free(*dst); *dst = next; }
static void set_fixed(char *dst, size_t size, const char *src) { snprintf(dst, size, "%s", src ? src : ""); }
static bool streq(const char *a, const char *b) {
  if (a == b) return true;
  if (!a || !b) return false;
  return strcmp(a, b) == 0;
}
static void pool_add(pool *p, void *block) {
  p->blocks = xgrow(p->blocks, &p->cap, p->count + 1, sizeof *p->blocks);
  p->blocks[p->count++] = block;
}
static void pool_free(pool *p) {
  for (size_t i = 0; i < p->count; i++) free(p->blocks[i]);
  p->count = 0;
}
static void pool_destroy(pool *p) { pool_free(p); free(p->blocks); memset(p, 0, sizeof *p); }
static void pool_move(pool *from, pool *to) {
  for (size_t i = 0; i < from->count; i++) pool_add(to, from->blocks[i]);
  from->count = 0;
}
/* A factory generation starts: the previous one waits in `retired`. */
static void sc_begin(scratch *sc, pool *retired) {
  pool_move(&sc->current, retired);
  if (sc->controls) pool_add(retired, sc->controls);
  sc->controls = NULL; sc->count = 0; sc->cap = 0;
}
static void sc_destroy(scratch *sc) {
  pool_destroy(&sc->current);
  free(sc->controls);
  memset(sc, 0, sizeof *sc);
}
static void *sc_alloc(scratch *sc, size_t size) {
  void *block = calloc(1, size ? size : 1);
  if (!block) qa_range_error("Out of memory");
  pool_add(&sc->current, block);
  return block;
}
static char *sc_str(scratch *sc, const char *s) {
  char *copy = xdup(s);
  pool_add(&sc->current, copy);
  return copy;
}
static char *sc_fmt(scratch *sc, const char *format, ...) {
  va_list args; va_start(args, format);
  va_list copy; va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length < 0) qa_range_error("Out of memory");
  char *out = malloc((size_t)length + 1);
  if (!out) qa_range_error("Out of memory");
  vsnprintf(out, (size_t)length + 1, format, args);
  va_end(args);
  pool_add(&sc->current, out);
  return out;
}
static action *sc_action(scratch *sc, qa_app_startup_menu *m, int kind, const char *a, const char *b) {
  action *act = sc_alloc(sc, sizeof *act);
  act->m = m; act->kind = kind;
  act->a = a ? sc_str(sc, a) : NULL;
  act->b = b ? sc_str(sc, b) : NULL;
  act->flag = false;
  return act;
}
static qa_ui_control *sc_push(scratch *sc) {
  sc->controls = xgrow(sc->controls, &sc->cap, sc->count + 1, sizeof *sc->controls);
  qa_ui_control *control = &sc->controls[sc->count++];
  memset(control, 0, sizeof *control);
  return control;
}
/* A control that is not (yet) part of a factory's array. */
static qa_ui_control *sc_control(scratch *sc) { return sc_alloc(sc, sizeof(qa_ui_control)); }
/* Safe point: no menu callback is on the stack. */
static void release_retired(qa_app_startup_menu *m) { pool_free(&m->retired); }

/* ---- text measure ------------------------------------------------------------------ */
static qa_ui_preference_values appearance(qa_app_startup_menu *m) {
  return m->o.appearance ? m->o.appearance(m->o.ctx) : qa_ui_default_preferences;
}
/* TS measure(text, scale): layoutText over the body or (bold) title font. */
static double measure(qa_app_startup_menu *m, const char *text, double scale) {
  bool bold = m->o.appearance != NULL && streq(m->o.appearance(m->o.ctx).typeface, "bold");
  qa_text_layout_options options;
  memset(&options, 0, sizeof options);
  options.text = text; options.text_length = strlen(text);
  options.font = bold ? m->o.title_font : m->o.font;
  options.scale = scale;
  options.color = (qa_vec4){1, 1, 1, 1};
  qa_text_layout layout;
  if (!qa_layout_text(&options, &layout)) qa_range_error("Out of memory");
  double width = layout.width;
  qa_text_layout_destroy(&layout);
  return width;
}
/* Start of the code point before `end` (TS slices UTF-16 units; one per BMP code point). */
static size_t previous_boundary(const char *text, size_t end) {
  if (end == 0) return 0;
  end--;
  while (end > 0 && ((unsigned char)text[end] & 0xc0) == 0x80) end--;
  return end;
}
/* TS fit(text, width, scale). */
static char *fit(scratch *sc, qa_app_startup_menu *m, const char *text, double width, double scale) {
  if (measure(m, text, scale) <= width) return sc_str(sc, text);
  size_t length = strlen(text), end = length;
  char *trial = malloc(length + 4);
  if (!trial) qa_range_error("Out of memory");
  pool_add(&sc->current, trial);
  for (;;) {
    if (end == 0) break;
    memcpy(trial, text, end); memcpy(trial + end, "...", 4);
    if (!(measure(m, trial, scale) > width)) break;
    end = previous_boundary(text, end);
  }
  memcpy(trial, text, end); memcpy(trial + end, "...", 4);
  return trial;
}
/* TS fitScale(value, width, maximum). */
static double fit_scale(qa_app_startup_menu *m, const char *value, double width, double maximum) {
  if (measure(m, value, maximum) <= width) return maximum;
  double low = 1.2, high = maximum;
  for (int i = 0; i < 12; i++) {
    double middle = (low + high) / 2;
    if (measure(m, value, middle) <= width) low = middle; else high = middle;
  }
  return low;
}
/* TS new Date(ms).toLocaleString() under the en-US default locale, local time. */
static char *locale_string(scratch *sc, double milliseconds) {
  if (!isfinite(milliseconds) || fabs(milliseconds) > 8.64e15) return sc_str(sc, "Invalid Date");
  time_t seconds = (time_t)floor(milliseconds / 1000);
  struct tm local;
  if (localtime_r(&seconds, &local) == NULL) return sc_str(sc, "Invalid Date");
  int hour = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
  return sc_fmt(sc, "%d/%d/%d, %d:%02d:%02d %s", local.tm_mon + 1, local.tm_mday, local.tm_year + 1900, hour,
                local.tm_min, local.tm_sec, local.tm_hour < 12 ? "AM" : "PM");
}

/* ---- model access ------------------------------------------------------------------ */
typedef struct rows_call { qa_app_startup_menu *m; const qa_app_startup_selection_row *rows; size_t count; } rows_call;
static void rows_body(void *ctx) {
  rows_call *call = ctx;
  call->rows = qa_app_startup_selection_rows(call->m->o.model, &call->count);
}
/* TS safeRows(): a throwing rows() is []. */
static const qa_app_startup_selection_row *safe_rows(qa_app_startup_menu *m, size_t *out_count) {
  rows_call call = {m, NULL, 0};
  qa_caught_error caught;
  if (!qa_try(rows_body, &call, &caught)) { *out_count = 0; return NULL; }
  *out_count = call.count;
  return call.rows;
}
static const qa_app_startup_selection_row *find_row(const qa_app_startup_selection_row *rows, size_t count, const char *field) {
  for (size_t i = 0; rows && i < count; i++) if (streq(rows[i].id, field)) return &rows[i];
  return NULL;
}
static const qa_app_startup_selection_choice *find_choice(const qa_app_startup_selection_row *row, const char *id) {
  for (size_t i = 0; row && i < row->choice_count; i++) if (streq(row->choices[i].id, id)) return &row->choices[i];
  return NULL;
}
static const char *family_name(qa_game_family family) {
  return family == QA_GAME_FAMILY_Q1 ? "q1" : family == QA_GAME_FAMILY_Q2 ? "q2" : "q3";
}
static const qa_app_startup_native_preset *presets(qa_app_startup_menu *m, size_t *out_count) {
  return qa_app_startup_selection_presets(m->o.model, out_count);
}
static const qa_app_startup_native_preset *find_preset(qa_app_startup_menu *m, const char *id) {
  if (!id) return NULL;
  size_t count = 0;
  const qa_app_startup_native_preset *list = presets(m, &count);
  for (size_t i = 0; i < count; i++) if (streq(list[i].choice.id, id)) return &list[i];
  return NULL;
}
/* catalog.product(model.options.product).expectation */
static const qa_catalog_product_expectation *selected_expectation(qa_app_startup_menu *m) {
  qa_app_options options;
  qa_app_startup_selection_options(m->o.model, &options);
  const qa_catalog_discovered_product *product =
      qa_content_catalog_product(qa_app_startup_selection_catalog(m->o.model), options.product);
  char product_id[256];
  set_fixed(product_id, sizeof product_id, options.product);
  qa_app_options_destroy(&options);
  if (product == NULL) qa_raise("Error", 0, 0, "Unknown product %s", product_id);
  return &product->expectation;
}
static void set_status(qa_app_startup_menu *m, const char *text, bool busy) {
  set_str(&m->status, text ? text : "");
  m->busy = busy;
}
static void clear_status(qa_app_startup_menu *m) { set_str(&m->status, ""); } /* TS this.status = "" */

/* TS nativeCampaigns(): unified by title, the edition picks the variant. */
static size_t native_campaigns(qa_app_startup_menu *m) {
  size_t count = 0, out = 0;
  const qa_app_startup_native_preset *list = presets(m, &count);
  for (size_t i = 0; i < count; i++) {
    if (!streq(family_name(list[i].family), m->native_family)) continue;
    bool seen = false;
    for (size_t j = 0; j < i && !seen; j++)
      if (streq(family_name(list[j].family), m->native_family) && streq(list[j].choice.label, list[i].choice.label)) seen = true;
    if (seen) continue;
    const qa_app_startup_native_preset *picked = &list[i];
    for (size_t j = 0; j < count; j++)
      if (streq(family_name(list[j].family), m->native_family) && streq(list[j].choice.label, list[i].choice.label)
          && streq(list[j].edition, m->native_edition)) { picked = &list[j]; break; }
    m->campaigns = xgrow(m->campaigns, &m->campaign_cap, out + 1, sizeof *m->campaigns);
    m->campaigns[out++] = picked;
  }
  return out;
}

/* ---- picker dropdown state -------------------------------------------------------- */
static char *variant_key(const qa_app_startup_selection_choice *choice) {
  size_t size = strlen(choice->id) + strlen(choice->label) + (choice->group ? strlen(choice->group) : 0) + 2;
  char *key = malloc(size);
  if (!key) qa_range_error("Out of memory");
  if (!choice->edition) snprintf(key, size, "%s", choice->id);
  else snprintf(key, size, "%s|%s", choice->group ? choice->group : "", choice->label);
  return key;
}
static bool same_variant(const qa_app_startup_selection_choice *a, const qa_app_startup_selection_choice *b) {
  char *ka = variant_key(a), *kb = variant_key(b);
  bool same = strcmp(ka, kb) == 0;
  free(ka); free(kb);
  return same;
}
static const qa_app_startup_selection_choice *edition_variant(const qa_app_startup_selection_row *row,
    const qa_app_startup_selection_choice *choice, const char *edition) {
  for (size_t i = 0; i < row->choice_count; i++)
    if (streq(row->choices[i].edition, edition) && same_variant(&row->choices[i], choice)) return &row->choices[i];
  return NULL;
}
static bool list_has(char *const *list, size_t count, const char *value) {
  for (size_t i = 0; i < count; i++) if (streq(list[i], value)) return true;
  return false;
}
static void dd_clear_collapsed(open_dropdown *dd) {
  for (size_t i = 0; i < dd->collapsed_count; i++) free(dd->collapsed[i]);
  free(dd->collapsed); dd->collapsed = NULL; dd->collapsed_count = 0;
}
static void dd_add_collapsed(open_dropdown *dd, const char *group) {
  char **grown = realloc(dd->collapsed, (dd->collapsed_count + 1) * sizeof *grown);
  if (!grown) qa_range_error("Out of memory");
  dd->collapsed = grown;
  dd->collapsed[dd->collapsed_count++] = xdup(group);
}
/* TS dropdownEntries(row, edition, collapsed) into m->entries. */
static size_t dropdown_entries(qa_app_startup_menu *m, const qa_app_startup_selection_row *row,
                               const char *edition, char *const *collapsed, size_t collapsed_count) {
  const qa_app_startup_selection_choice **visible = NULL;
  size_t visible_count = 0, visible_cap = 0, out = 0;
  for (size_t i = 0; i < row->choice_count; i++) {
    const qa_app_startup_selection_choice *choice = &row->choices[i];
    if (!choice->edition) {
      visible = xgrow(visible, &visible_cap, visible_count + 1, sizeof *visible);
      visible[visible_count++] = choice;
      continue;
    }
    /* byKey: the visible entry that holds this key. */
    size_t existing = (size_t)-1;
    for (size_t j = 0; j < visible_count && existing == (size_t)-1; j++)
      if (visible[j]->edition && same_variant(visible[j], choice)) existing = j;
    if (existing == (size_t)-1) {
      visible = xgrow(visible, &visible_cap, visible_count + 1, sizeof *visible);
      visible[visible_count++] = choice;
    } else if (edition && streq(choice->edition, edition)) visible[existing] = choice;
  }
  for (size_t i = 0; i < visible_count; i++) {
    if (visible[i]->group) continue;
    m->entries = xgrow(m->entries, &m->entry_cap, out + 1, sizeof *m->entries);
    m->entries[out++] = (dd_entry){false, NULL, false, visible[i]};
  }
  for (size_t i = 0; i < visible_count; i++) {
    const char *group = visible[i]->group;
    if (!group) continue;
    bool listed = false;
    for (size_t j = 0; j < i && !listed; j++) if (streq(visible[j]->group, group)) listed = true;
    if (listed) continue;
    bool expanded = !list_has(collapsed, collapsed_count, group);
    m->entries = xgrow(m->entries, &m->entry_cap, out + 1, sizeof *m->entries);
    m->entries[out++] = (dd_entry){true, group, expanded, NULL};
    if (expanded)
      for (size_t j = 0; j < visible_count; j++) if (streq(visible[j]->group, group)) {
        m->entries = xgrow(m->entries, &m->entry_cap, out + 1, sizeof *m->entries);
        m->entries[out++] = (dd_entry){false, NULL, false, visible[j]};
      }
  }
  free(visible);
  return out;
}
static size_t dd_entries(qa_app_startup_menu *m, const qa_app_startup_selection_row *row) {
  return dropdown_entries(m, row, m->dd.has_edition ? m->dd.edition : NULL, m->dd.collapsed, m->dd.collapsed_count);
}
/* TS dropdownOpenState(row), written into m->dd (field/stage set by the caller). */
static void dropdown_open_state(qa_app_startup_menu *m, const qa_app_startup_selection_row *row) {
  const qa_app_startup_selection_choice *current = find_choice(row, row->value);
  bool has_classic = false, has_rerelease = false;
  for (size_t i = 0; i < row->choice_count; i++) {
    if (streq(row->choices[i].edition, "classic")) has_classic = true;
    if (streq(row->choices[i].edition, "rerelease")) has_rerelease = true;
  }
  open_dropdown *dd = &m->dd;
  dd->has_edition = has_classic && has_rerelease;
  set_fixed(dd->edition, sizeof dd->edition,
            dd->has_edition ? (current && current->edition ? current->edition : "classic") : "");
  dd_clear_collapsed(dd);
  for (size_t i = 0; i < row->choice_count; i++) {
    const char *group = row->choices[i].group;
    if (!group || list_has(dd->collapsed, dd->collapsed_count, group)) continue;
    if (streq(group, current ? current->group : NULL)) continue;
    dd_add_collapsed(dd, group);
  }
  size_t count = dd_entries(m, row);
  double visible_count = dd->has_edition ? 7 : 8;
  long selected_index = -1;
  for (size_t i = 0; i < count && selected_index < 0; i++)
    if (!m->entries[i].header && streq(m->entries[i].choice->id, row->value)) selected_index = (long)i;
  long selected = selected_index < 0 ? 0 : selected_index;
  long header = -1;
  for (long i = selected - 1; i >= 0; i--) if (m->entries[i].header) { header = i; break; }
  double anchored = header >= 0 && (double)(selected - header) < visible_count ? (double)header : (double)selected;
  dd->scroll = qa_js_max(0, qa_js_min(anchored, qa_js_max(0, (double)count - visible_count)));
}
static const char *const *stage_chain(const char *field, size_t *out_count, const char **single) {
  static const char *const map_chain[] = {"mapProduct", "map"};
  static const char *const char_chain[] = {"character", "model", "skin", "topColor", "bottomColor"};
  if (streq(field, "mapProduct")) { *out_count = 2; return map_chain; }
  if (streq(field, "character")) { *out_count = 5; return char_chain; }
  *single = field; *out_count = 1; return single;
}
static size_t chain_index(const char *const *chain, size_t count, const char *stage) {
  for (size_t i = 0; i < count; i++) if (streq(chain[i], stage)) return i;
  return (size_t)-1; /* TS indexOf -1 */
}
/* model.prepareMapChoices(offset, count) with the TS status window. */
typedef struct map_choices_call { qa_app_startup_menu *m; double offset, count; bool prepared; } map_choices_call;
static void map_choices_body(void *ctx) {
  map_choices_call *call = ctx;
  call->prepared = qa_app_startup_selection_prepare_map_choices(call->m->o.model, call->offset, call->count);
}
static void prepare_map_choices(qa_app_startup_menu *m, double offset, double count) {
  /* TS: null returns; otherwise "Checking map rules..." (busy) until the preparation settles. */
  map_choices_call call = {m, offset, count, false};
  qa_caught_error caught;
  if (!qa_try(map_choices_body, &call, &caught)) { set_status(m, caught.message, false); return; }
  if (call.prepared) set_status(m, "", false);
}
/* TS prepareBuilderDropdownScroll(scroll). */
static void prepare_builder_scroll(qa_app_startup_menu *m, double scroll) {
  if (!m->dd.active) return;
  m->dd.scroll = scroll;
  if (!streq(m->dd.stage, "map")) return;
  prepare_map_choices(m, scroll, 8);
}
static void close_dropdown(qa_app_startup_menu *m) {
  m->dd.active = false;
  dd_clear_collapsed(&m->dd);
}
/* TS advanceStage(). */
static bool advance_stage(qa_app_startup_menu *m) {
  if (!m->dd.active) return false;
  const char *single = NULL;
  size_t chain_count = 0;
  const char *const *chain = stage_chain(m->dd.field, &chain_count, &single);
  size_t at = chain_index(chain, chain_count, m->dd.stage);
  size_t count = 0;
  const qa_app_startup_selection_row *rows = safe_rows(m, &count);
  /* chain.slice(indexOf + 1): indexOf -1 slices from 0. */
  for (size_t i = at == (size_t)-1 ? 0 : at + 1; i < chain_count; i++) {
    const qa_app_startup_selection_row *row = find_row(rows, count, chain[i]);
    if (!row) continue;
    set_fixed(m->dd.stage, sizeof m->dd.stage, chain[i]);
    dropdown_open_state(m, row);
    if (streq(m->dd.stage, "map")) prepare_builder_scroll(m, m->dd.scroll);
    return true;
  }
  return false;
}
/* TS retreatStage(). */
static bool retreat_stage(qa_app_startup_menu *m) {
  if (!m->dd.active || streq(m->dd.stage, m->dd.field)) return false;
  const char *single = NULL;
  size_t chain_count = 0;
  const char *const *chain = stage_chain(m->dd.field, &chain_count, &single);
  size_t at = chain_index(chain, chain_count, m->dd.stage);
  /* chain.slice(0, indexOf): indexOf -1 slices to the last element. */
  size_t end = at == (size_t)-1 ? (chain_count > 0 ? chain_count - 1 : 0) : at;
  size_t count = 0;
  const qa_app_startup_selection_row *rows = safe_rows(m, &count);
  for (size_t i = end; i-- > 0;) {
    const qa_app_startup_selection_row *row = find_row(rows, count, chain[i]);
    if (!row) continue;
    set_fixed(m->dd.stage, sizeof m->dd.stage, chain[i]);
    dropdown_open_state(m, row);
    return true;
  }
  return false;
}

/* ---- roster and choice pages ------------------------------------------------------- */
/* TS prepareChoicePage(page). */
static void prepare_choice_page(qa_app_startup_menu *m, double page) {
  m->page = page;
  if (m->monster_kind != 0 || !streq(m->field, "map")) return;
  prepare_map_choices(m, page * 7, 7);
}
static void roster_body(void *ctx) { qa_app_startup_selection_prepare_monster_roster(((qa_app_startup_menu *)ctx)->o.model); }
/* TS openRoster(). */
static void open_roster(qa_app_startup_menu *m) {
  set_status(m, "Reading this map's monster roster...", true);
  qa_caught_error caught;
  if (!qa_try(roster_body, m, &caught)) { set_status(m, caught.message, false); return; }
  set_status(m, "", false);
  qa_native_ui_controller_open_menu(m->controller, ID_ROSTER);
}
/* TS selectionRow() (NULL is TS undefined). */
static const qa_app_startup_selection_row *selection_row(qa_app_startup_menu *m) {
  if (m->monster_kind == 1) {
    m->sel_row = qa_app_startup_selection_monster_source_row(m->o.model);
    return &m->sel_row;
  }
  if (m->monster_kind == 2) {
    size_t count = 0;
    const qa_app_monster_roster_row *rows = qa_app_startup_selection_monster_roster_rows(m->o.model, &count);
    for (size_t i = 0; i < count; i++) if (streq(rows[i].classname, m->monster_classname)) {
      m->sel_row = (qa_app_startup_selection_row){rows[i].classname, rows[i].label, rows[i].value,
                                                   rows[i].choices, rows[i].choice_count};
      return &m->sel_row;
    }
    return NULL;
  }
  size_t count = 0;
  const qa_app_startup_selection_row *rows = safe_rows(m, &count);
  return find_row(rows, count, m->field);
}

/* ---- controls ---------------------------------------------------------------------- */
static void on_activate(void *ctx, qa_seat_id seat);
static void on_select(void *ctx, qa_seat_id seat, const char *choice);
static void on_checked(void *ctx, qa_seat_id seat, bool checked);
static void on_change_text(void *ctx, qa_seat_id seat, const char *text);
static void on_submit_text(void *ctx, qa_seat_id seat, const char *text);

static void fill_button(qa_ui_control *c, scratch *sc, qa_app_startup_menu *m, const char *id,
                        const char *label, double row, bool wide, int kind, const char *a, const char *b) {
  c->id = sc_fmt(sc, "ui:startup:%s", id);
  c->kind = "button";
  c->label = sc_str(sc, label);
  c->rect = (qa_rect){64, 114 + row * 33, wide ? 512 : 224, 29};
  c->visible = true;
  c->enabled = !m->busy;
  c->ctx = sc_action(sc, m, kind, a, b);
  c->activate_seat = on_activate;
}
/* TS button(id, label, row, activate, wide). */
static qa_ui_control *button(scratch *sc, qa_app_startup_menu *m, const char *id, const char *label,
                             double row, bool wide, int kind, const char *a, const char *b) {
  qa_ui_control *c = sc_push(sc);
  fill_button(c, sc, m, id, label, row, wide, kind, a, b);
  return c;
}
/* A builder button kept outside the factory array. */
static qa_ui_control *loose_button(scratch *sc, qa_app_startup_menu *m, const char *id, int kind,
                                   const char *a, const char *b) {
  qa_ui_control *c = sc_control(sc);
  fill_button(c, sc, m, id, "", 0, false, kind, a, b);
  return c;
}
static qa_ui_control *back_button(scratch *sc, qa_app_startup_menu *m) {
  return button(sc, m, "back", "Back", 9, true, ACT_CLOSE_MENU, NULL, NULL);
}
static void fill_choice(qa_ui_control *c, scratch *sc, qa_app_startup_menu *m, const char *id, const char *label,
                        qa_rect rect, bool enabled, bool visible, const qa_ui_choice *choices, size_t choice_count,
                        const char *selected, int kind, const char *a) {
  c->id = sc_str(sc, id);
  c->kind = "choice";
  c->label = sc_str(sc, label);
  c->rect = rect; c->visible = visible; c->enabled = enabled;
  qa_ui_choice *mine = sc_alloc(sc, (choice_count ? choice_count : 1) * sizeof *mine);
  for (size_t i = 0; i < choice_count; i++) {
    mine[i].id = sc_str(sc, choices[i].id);
    mine[i].label = sc_str(sc, choices[i].label);
  }
  c->choices = mine; c->choice_count = choice_count;
  c->selected = selected ? sc_str(sc, selected) : NULL;
  c->ctx = sc_action(sc, m, kind, a, NULL);
  c->select_choice = on_select;
}
static qa_ui_control *choice_control(scratch *sc, qa_app_startup_menu *m, const char *id, const char *label,
                                     qa_rect rect, bool enabled, bool visible, const qa_ui_choice *choices,
                                     size_t choice_count, const char *selected, int kind, const char *a) {
  qa_ui_control *c = sc_push(sc);
  fill_choice(c, sc, m, id, label, rect, enabled, visible, choices, choice_count, selected, kind, a);
  return c;
}
static qa_ui_control *text_control(scratch *sc, qa_app_startup_menu *m, const char *id, const char *label,
                                   const char *text, double max_len, qa_rect rect, bool enabled, int kind) {
  qa_ui_control *c = sc_push(sc);
  c->id = sc_str(sc, id);
  c->kind = "text-entry";
  c->label = sc_str(sc, label);
  c->text = sc_str(sc, text);
  c->maximum_length = max_len;
  c->rect = rect; c->visible = true; c->enabled = enabled;
  c->ctx = sc_action(sc, m, kind, NULL, NULL);
  c->change_text = on_change_text;
  c->submit_text = on_submit_text;
  return c;
}
/* TS row(row, index), as the sound menu uses it. */
static void push_row(scratch *sc, qa_app_startup_menu *m, const qa_app_startup_selection_row *row, double index) {
  qa_rect rect = {64, 114 + index * 33, 512, 29};
  if (streq(row->id, "grapple")) {
    qa_ui_choice *available = sc_alloc(sc, (row->choice_count ? row->choice_count : 1) * sizeof *available);
    size_t n = 0;
    for (size_t i = 0; i < row->choice_count; i++)
      if (!row->choices[i].unavailable) available[n++] = (qa_ui_choice){row->choices[i].id, row->choices[i].label};
    choice_control(sc, m, "ui:startup:grapple", row->label, rect, !m->busy, true, available, n, row->value, ACT_GRAPPLE, NULL);
    return;
  }
  if (streq(row->id, "grenades")) {
    bool can_enable = streq(row->value, "enabled");
    for (size_t i = 0; i < row->choice_count; i++)
      if (streq(row->choices[i].id, "enabled") && !row->choices[i].unavailable) can_enable = true;
    qa_ui_control *c = sc_push(sc);
    c->id = "ui:startup:grenades"; c->kind = "toggle"; c->label = sc_str(sc, row->label);
    c->rect = rect; c->visible = true; c->enabled = !m->busy && can_enable;
    c->checked = streq(row->value, "enabled");
    c->ctx = sc_action(sc, m, ACT_GRENADES, NULL, NULL);
    c->change_checked = on_checked;
    return;
  }
  const qa_app_startup_selection_choice *choice = find_choice(row, row->value);
  const char *selected = choice ? choice->label : row->value;
  char *label = fit(sc, m, sc_fmt(sc, "%s: %s", row->label, selected), 486, qa_ui_menu_text.body);
  button(sc, m, row->id, label, index, true, ACT_ROW_SELECT, row->id, NULL);
}

/* ---- builder layout ---------------------------------------------------------------- */
static bool appearance_field(const char *field) {
  return streq(field, "character") || streq(field, "model") || streq(field, "skin") || streq(field, "topColor")
      || streq(field, "bottomColor");
}
/* String.replace(text, "") of the first occurrence, and the anchored regex forms. */
static char *replace_first(scratch *sc, const char *text, const char *pattern) {
  const char *at = strstr(text, pattern);
  if (!at) return sc_str(sc, text);
  return sc_fmt(sc, "%.*s%s", (int)(at - text), text, at + strlen(pattern));
}
static char *strip_suffix(scratch *sc, const char *text, const char *suffix) {
  size_t length = strlen(text), n = strlen(suffix);
  if (length >= n && strcmp(text + length - n, suffix) == 0) return sc_fmt(sc, "%.*s", (int)(length - n), text);
  return sc_str(sc, text);
}
static char *strip_prefix(scratch *sc, const char *text, const char *prefix) {
  size_t n = strlen(prefix);
  return sc_str(sc, strncmp(text, prefix, n) == 0 ? text + n : text);
}
typedef struct section_field { const char *field; const char *label; bool picker; } section_field;
typedef struct section { const char *title; double x; section_field fields[4]; size_t count; } section;
static const section sections[] = {
  {"WORLD", 64, {{"product", "Game", true}, {"mapProduct", "Map set", true}, {"enemies", "Monsters", true}}, 3},
  {"PLAYER", 64, {{"character", "Character", true}, {"movement", "Movement", true}, {"weapons", "Weapons", true}}, 3},
  {"MATCH", 328, {{"seats", "Players", false}, {"mode", "Mode", false}, {"rules", "Rules", true}, {"skill", "Difficulty", false}}, 4},
  {"KIT", 328, {{"grapple", "Hook", false}, {"grappleStyle", "Hook style", true}, {"grenades", "Grenades", false}}, 3},
};
/* TS builderFields(all, dropdown, appearanceDropdown, panelX). */
static void builder_fields(scratch *sc, qa_app_startup_menu *m, const qa_app_startup_selection_row *rows, size_t count,
                           bool dropdown, bool appearance_dropdown, double panel_x, builder_layout *out) {
  double cursor_left = -1, cursor_right = -1; /* cursorByColumn (-1 = unset) */
  out->fields = sc_alloc(sc, 16 * sizeof *out->fields);
  for (size_t si = 0; si < sizeof sections / sizeof *sections; si++) {
    const section *s = &sections[si];
    if (appearance_dropdown || s->x == panel_x) continue;
    size_t present = 0;
    for (size_t i = 0; i < s->count; i++) if (find_row(rows, count, s->fields[i].field)) present++;
    if (present == 0) continue;
    double *cursor = s->x == 64 ? &cursor_left : &cursor_right;
    double start_y = *cursor < 0 ? 110 : *cursor;
    out->heads[out->head_count++] = (builder_head){s->x, start_y, s->title};
    size_t index = 0;
    for (size_t i = 0; i < s->count; i++) {
      const section_field *spec = &s->fields[i];
      const qa_app_startup_selection_row *row = find_row(rows, count, spec->field);
      if (!row) continue;
      double indent = 0; /* no TS section sets indent */
      qa_rect rect = {s->x + indent, start_y + 15 + (double)index * 23, 248 - indent, 22};
      double value_width = rect.width - measure(m, spec->label, qa_ui_menu_text.field) - 34;
      bool enabled = !m->busy && !dropdown;
      builder_field *field = &out->fields[out->field_count++];
      field->rect = rect; field->label = spec->label; field->has_value = true; field->value_width = value_width;
      const qa_app_startup_selection_choice *current = find_choice(row, row->value);
      const char *selected = current ? current->label : row->value;
      if (spec->picker) {
        char *display = streq(spec->field, "enemies") ? replace_first(sc, selected, " authored monsters")
            : streq(spec->field, "weapons") ? strip_suffix(sc, selected, " weapons") : sc_str(sc, selected);
        const qa_app_startup_selection_row *staged = streq(spec->field, "mapProduct") ? find_row(rows, count, "map")
            : streq(spec->field, "character") ? find_row(rows, count, "model") : NULL;
        const qa_app_startup_selection_choice *staged_choice = staged ? find_choice(staged, staged->value) : NULL;
        if (staged_choice) {
          const char *staged_label = staged_choice->label;
          if (streq(spec->field, "mapProduct")) staged_label = strip_suffix(sc, strip_prefix(sc, staged_label, "maps/"), ".bsp");
          display = sc_fmt(sc, "%s / %s", display, staged_label);
        }
        qa_ui_control *c = loose_button(sc, m, row->id, ACT_BUILDER_OPEN, spec->field, NULL);
        c->rect = rect; c->enabled = enabled;
        field->control = c;
        field->value_text = sc_fmt(sc, "%s >", display);
      } else {
        qa_ui_choice *available = sc_alloc(sc, (row->choice_count ? row->choice_count : 1) * sizeof *available);
        size_t n = 0;
        bool enabled_choice = false;
        for (size_t j = 0; j < row->choice_count; j++) {
          if (row->choices[j].unavailable) continue;
          available[n++] = (qa_ui_choice){row->choices[j].id, ""};
          if (streq(row->choices[j].id, "enabled")) enabled_choice = true;
        }
        bool field_enabled = streq(row->id, "grenades") ? enabled && (streq(row->value, "enabled") || enabled_choice) : enabled;
        qa_ui_control *c = sc_control(sc);
        char *id = sc_fmt(sc, "ui:startup:%s", row->id);
        fill_choice(c, sc, m, id, "", rect, field_enabled, true, available, n, row->value, ACT_FIELD_SELECT, spec->field);
        field->control = c;
        field->value_text = streq(row->id, "grenades") ? (streq(row->value, "enabled") ? "On" : "Off") : sc_str(sc, selected);
      }
      index++;
    }
    *cursor = start_y + 15 + (double)present * 23 + 8;
  }
}
/* TS builderDropdownSpec(all, dropdown, appearanceDropdown, panelX). */
static bool builder_dropdown_spec(scratch *sc, qa_app_startup_menu *m, const qa_app_startup_selection_row *rows, size_t count,
                                  bool appearance_dropdown, double panel_x, builder_dropdown *out) {
  const qa_app_startup_selection_row *row = find_row(rows, count, m->dd.stage);
  if (!row) { close_dropdown(m); return false; }
  char *stage = sc_str(sc, m->dd.stage), *field = sc_str(sc, m->dd.field);
  size_t total = dd_entries(m, row);
  bool staged = !streq(m->dd.stage, m->dd.field);
  double visible_count = (m->dd.has_edition ? 7 : 8) - (staged ? 1 : 0);
  double items_top = m->dd.has_edition ? 164 : 138;
  double scroll = qa_js_max(0, qa_js_min(m->dd.scroll, qa_js_max(0, (double)total - visible_count)));
  double item_scale = qa_ui_menu_text.field;
  for (size_t i = 0; i < total; i++)
    item_scale = qa_js_min(item_scale, fit_scale(m, m->entries[i].header ? m->entries[i].group : m->entries[i].choice->label,
                                                 216, qa_ui_menu_text.field));
  memset(out, 0, sizeof *out);
  if (m->dd.has_edition) {
    out->has_toggle = true;
    out->classic = loose_button(sc, m, "edition-classic", ACT_BUILDER_EDITION, "classic", stage);
    out->classic->rect = (qa_rect){panel_x, 136, 120, 22};
    out->classic_selected = streq(m->dd.edition, "classic");
    out->rerelease = loose_button(sc, m, "edition-rerelease", ACT_BUILDER_EDITION, "rerelease", stage);
    out->rerelease->rect = (qa_rect){panel_x + 128, 136, 120, 22};
    out->rerelease_selected = streq(m->dd.edition, "rerelease");
  }
  out->items = sc_alloc(sc, ((size_t)visible_count + 2) * sizeof *out->items);
  const qa_app_startup_selection_choice *current = find_choice(row, row->value);
  size_t first = (size_t)scroll;
  if (staged) out->item_count = 1; /* items.unshift(stage-back) below */
  for (size_t i = first; i < total && i < first + (size_t)visible_count; i++) {
    dd_entry *e = &m->entries[i];
    qa_rect rect = {panel_x, items_top + ((double)(i - first) + (staged ? 1 : 0)) * 27, 248, 26};
    builder_item *item = &out->items[out->item_count++];
    qa_ui_control *c = sc_control(sc);
    c->kind = "button"; c->label = ""; c->rect = rect; c->visible = true;
    c->activate_seat = on_activate;
    item->control = c;
    if (e->header) {
      c->id = sc_fmt(sc, "ui:startup:group:%s", e->group);
      c->enabled = !m->busy;
      c->ctx = sc_action(sc, m, ACT_BUILDER_GROUP, e->group, NULL);
      item->choice = sc_str(sc, row->value);
      item->label = sc_fmt(sc, "%s%s", e->expanded ? "- " : "+ ", e->group);
      item->header = true;
      continue;
    }
    const qa_app_startup_selection_choice *choice = e->choice;
    if (choice->unavailable) {
      for (size_t j = 0; j < row->choice_count; j++)
        if (same_variant(&row->choices[j], choice) && !row->choices[j].unavailable) { choice = &row->choices[j]; break; }
    }
    bool selected = current && same_variant(current, choice);
    c->id = appearance_dropdown || !selected ? sc_fmt(sc, "ui:startup:choice:%s", choice->id) : sc_fmt(sc, "ui:startup:%s", row->id);
    c->enabled = !choice->unavailable && !m->busy;
    action *act = sc_action(sc, m, ACT_BUILDER_CHOICE, choice->id, stage);
    act->flag = appearance_dropdown;
    c->ctx = act;
    item->choice = sc_str(sc, choice->id);
    item->label = sc_fmt(sc, "%s%s", selected ? "> " : "", choice->label);
    item->selected = selected;
    item->indent = choice->group != NULL;
    item->unavailable = choice->unavailable ? sc_str(sc, choice->unavailable) : NULL;
  }
  if (staged) {
    const char *single = NULL;
    size_t chain_count = 0;
    const char *const *chain = stage_chain(field, &chain_count, &single);
    size_t at = chain_index(chain, chain_count, stage);
    size_t end = at == (size_t)-1 ? (chain_count > 0 ? chain_count - 1 : 0) : at;
    const qa_app_startup_selection_row *previous = NULL;
    for (size_t i = end; i-- > 0 && !previous;) previous = find_row(rows, count, chain[i]);
    const qa_app_startup_selection_choice *previous_choice = previous ? find_choice(previous, previous->value) : NULL;
    const char *previous_label = previous_choice ? previous_choice->label : previous ? previous->label : "Back";
    qa_ui_control *c = sc_control(sc);
    c->id = "ui:startup:stage-back"; c->kind = "button"; c->label = "";
    c->rect = (qa_rect){panel_x, items_top, 248, 26};
    c->visible = true; c->enabled = !m->busy;
    c->ctx = sc_action(sc, m, ACT_BUILDER_STAGE_BACK, NULL, NULL);
    c->activate_seat = on_activate;
    const char *back_choice = sc_str(sc, row->value);
    const char *back_label = sc_fmt(sc, "< %s", previous_label);
    out->items[0] = (builder_item){c, back_choice, back_label, false, true, false, NULL};
  }
  out->x = panel_x; out->title = sc_str(sc, row->label); out->item_scale = item_scale; out->items_top = items_top;
  out->scroll_offset = scroll; out->scroll_visible = visible_count; out->scroll_total = (double)total;
  return true;
}
/* TS builderFooter(dropdown). */
static void builder_footer(scratch *sc, qa_app_startup_menu *m, bool dropdown, builder_layout *out) {
  bool footer_enabled = !dropdown;
  struct { int kind; const char *id; const char *label; double width; } entries[4];
  size_t n = 0;
  qa_app_startup_hosting hosting = qa_app_startup_selection_hosting(m->o.model);
  entries[n].kind = ACT_FOOTER_HOSTING; entries[n].id = "hosting";
  entries[n].label = streq(hosting.kind, "offline") ? "Network: local only" : "Network: hosting";
  entries[n].width = 0; n++;
  if (m->mods_menu) { entries[n].kind = ACT_FOOTER_MODS; entries[n].id = "mods"; entries[n].label = "Mods"; entries[n].width = 72; n++; }
  if (m->o.browser) { entries[n].kind = ACT_FOOTER_BROWSE; entries[n].id = "browse"; entries[n].label = "Find servers"; entries[n].width = 118; n++; }
  if (m->lobby_menu) { entries[n].kind = ACT_FOOTER_LOBBY; entries[n].id = "local-lobby"; entries[n].label = "Local lobby"; entries[n].width = 102; n++; }
  double gaps = (double)(n - 1) * 8, fixed = 0, x = 64;
  for (size_t i = 0; i < n; i++) fixed += entries[i].width;
  for (size_t i = 0; i < n; i++) {
    double width = entries[i].width == 0 ? 512 - fixed - gaps : entries[i].width;
    qa_rect rect = {x, 388, width, 24};
    qa_ui_control *c = loose_button(sc, m, entries[i].id, entries[i].kind, NULL, NULL);
    c->rect = rect;
    c->enabled = footer_enabled && c->enabled;
    out->footer[out->footer_count++] = (footer_entry){c, rect, entries[i].label};
    x += width + 8;
  }
  qa_rect back_rect = {64, 416, 248, 26}, play_rect = {328, 416, 248, 26};
  qa_ui_control *back = loose_button(sc, m, "back", ACT_CLOSE_MENU, NULL, NULL);
  back->rect = back_rect; back->enabled = footer_enabled;
  out->footer[out->footer_count++] = (footer_entry){back, back_rect, "Back"};
  qa_ui_control *play = loose_button(sc, m, "play", ACT_PLAY, NULL, NULL);
  play->rect = play_rect; play->enabled = footer_enabled;
  out->footer[out->footer_count++] = (footer_entry){play, play_rect, streq(hosting.kind, "offline") ? "Play" : "Start server"};
}
/* TS builderViewer(all, dropdown, dropdownSpec, turntable). */
static void builder_viewer(scratch *sc, qa_app_startup_menu *m, const qa_app_startup_selection_row *rows, size_t count,
                           const builder_dropdown *spec, viewer_spec *out) {
  qa_seat_ui_state state = qa_native_ui_controller_state(m->controller);
  const char *focused_id = streq(state.focus.kind, "menu") ? state.focus.control : NULL;
  const builder_item *hovered = NULL, *focused = NULL, *first = NULL;
  for (size_t i = 0; i < spec->item_count; i++) {
    const builder_item *item = &spec->items[i];
    if (item->header || item->unavailable) continue;
    if (!first) first = item;
    if (!hovered && qa_ui_contains(item->control->rect, state.cursor)) hovered = item;
    if (!focused && streq(item->control->id, focused_id)) focused = item;
  }
  const builder_item *candidate = hovered ? hovered
      : focused && (m->preview_armed || first != focused || focused->selected) ? focused : NULL;
  qa_app_startup_preview_overrides overrides;
  memset(&overrides, 0, sizeof overrides);
  if (candidate) {
    if (streq(m->dd.stage, "character")) overrides.character = candidate->choice;
    else if (streq(m->dd.stage, "model")) overrides.model = candidate->choice;
    else if (streq(m->dd.stage, "skin")) overrides.skin = candidate->choice;
    else if (streq(m->dd.stage, "topColor")) overrides.top_color = candidate->choice;
    else if (streq(m->dd.stage, "bottomColor")) overrides.bottom_color = candidate->choice;
  }
  qa_app_startup_character_preview preview = qa_app_startup_selection_character_preview(m->o.model, &overrides);
  const char *model_label = preview.family == QA_GAME_FAMILY_Q1 && streq(preview.model, "player") ? "Quake player" : preview.model;
  char *label;
  if (preview.family == QA_GAME_FAMILY_Q1) {
    const char *colors[2];
    const char *fields[2] = {"topColor", "bottomColor"};
    double values[2] = {preview.top_color, preview.bottom_color};
    for (int i = 0; i < 2; i++) {
      char *number = sc_str(sc, QA_JS_NUMBER(values[i]));
      const qa_app_startup_selection_choice *choice = find_choice(find_row(rows, count, fields[i]), number);
      colors[i] = choice ? choice->label : number;
    }
    label = sc_fmt(sc, "%s \xc2\xb7 %s / %s", model_label, colors[0], colors[1]);
  } else label = sc_fmt(sc, "%s \xc2\xb7 %s", model_label, preview.skin[0] == '\0' ? "Default" : preview.skin);
  qa_app_turntable_request request = {preview.product, preview.family, preview.model, preview.skin,
                                      preview.top_color, preview.bottom_color};
  double now = m->o.now(m->o.ctx);
  out->resource = sc_str(sc, qa_app_character_turntable_request(m->o.character_turntable, &request, now));
  out->panel = (qa_rect){320, 110, 264, 272};
  out->rect = (qa_rect){368, 118, 168, 224};
  out->label = label;
  qa_ui_control *c = loose_button(sc, m, "preview-done", ACT_PREVIEW_DONE, NULL, NULL);
  c->rect = (qa_rect){328, 358, 248, 20};
  c->enabled = !m->busy;
  out->control = c;
}
/* TS builderLayout(). */
static void builder_layout_build(scratch *sc, qa_app_startup_menu *m, builder_layout *out) {
  memset(out, 0, sizeof *out);
  size_t count = 0;
  const qa_app_startup_selection_row *rows = safe_rows(m, &count);
  bool dropdown = m->dd.active;
  bool appearance_dropdown = dropdown && m->o.character_turntable != NULL && appearance_field(m->dd.stage);
  double panel_x = -1;
  if (dropdown) {
    static const char *const right[] = {"product", "mapProduct", "map", "enemies", "character", "model", "skin",
                                        "topColor", "bottomColor", "movement", "weapons"};
    panel_x = 64;
    if (!appearance_dropdown)
      for (size_t i = 0; i < sizeof right / sizeof *right; i++) if (streq(m->dd.field, right[i])) panel_x = 328;
  }
  builder_fields(sc, m, rows, count, dropdown, appearance_dropdown, panel_x, out);
  if (dropdown) out->has_dropdown = builder_dropdown_spec(sc, m, rows, count, appearance_dropdown, panel_x, &out->dropdown);
  builder_footer(sc, m, dropdown, out);
  if (appearance_dropdown && out->has_dropdown) {
    out->has_viewer = true;
    builder_viewer(sc, m, rows, count, &out->dropdown, &out->viewer);
  }
}

/* ---- callbacks --------------------------------------------------------------------- */
/* TS browserControls run(work): a rejected browser operation becomes browser.status. */
static void browser_query_body(void *ctx) { qa_app_startup_server_browser_query(((qa_app_startup_menu *)ctx)->o.browser); }
static void browser_favorite_body(void *ctx) { qa_app_startup_server_browser_favorite(((qa_app_startup_menu *)ctx)->o.browser); }
static void browser_connect_body(void *ctx) {
  qa_app_startup_menu *m = ctx;
  qa_app_browser_connection connection = qa_app_startup_server_browser_connection(m->o.browser);
  if (m->o.connect) m->o.connect(m->o.ctx, connection);
  else qa_app_browser_connection_destroy(&connection);
}
static void browser_discover_body(void *ctx) { qa_app_startup_server_browser_discover(((qa_app_startup_menu *)ctx)->o.browser); }
static void browser_run(qa_app_startup_menu *m, void (*work)(void *ctx)) {
  qa_caught_error caught;
  if (!qa_try(work, m, &caught)) qa_app_startup_server_browser_set_status(m->o.browser, caught.message);
}
static void arenas_body(void *ctx) { qa_app_startup_selection_refresh_base_arenas(((qa_app_startup_menu *)ctx)->o.model); }
typedef struct hosting_call { qa_app_startup_menu *m; qa_app_startup_hosting value; } hosting_call;
static void hosting_body(void *ctx) {
  hosting_call *call = ctx;
  qa_app_startup_selection_set_hosting(call->m->o.model, call->value);
}
static void on_activate(void *ctx, qa_seat_id seat) {
  (void)seat; /* TS activate: () => ... takes no seat */
  action *act = ctx;
  qa_app_startup_menu *m = act->m;
  void *octx = m->o.ctx;
  switch (act->kind) {
    case ACT_OPEN:
      if (act->b) clear_status(m); /* the entries whose TS activate sets this.status = "" */
      qa_native_ui_controller_open_menu(m->controller, act->a); break;
    case ACT_CLOSE_MENU: qa_native_ui_controller_close_menu(m->controller); break;
    case ACT_QUIT: m->o.quit(octx); break;
    case ACT_PLAY: m->o.play(octx); break;
    case ACT_LOAD_MENU:
      m->save_page = 0;
      qa_native_ui_controller_open_menu(m->controller, ID_LOAD);
      m->o.refresh_saves(octx);
      break;
    case ACT_FAMILY:
      set_fixed(m->native_family, sizeof m->native_family, act->a);
      m->campaign_page = 0; clear_status(m);
      qa_native_ui_controller_open_menu(m->controller, ID_CAMPAIGN);
      break;
    case ACT_CUSTOM:
      clear_status(m);
      qa_native_ui_controller_open_menu(m->controller, ID_SESSION);
      break;
    case ACT_EDITION:
      set_fixed(m->native_edition, sizeof m->native_edition, act->a);
      m->campaign_page = 0; clear_status(m);
      break;
    case ACT_PRESET: {
      const qa_app_startup_native_preset *preset = find_preset(m, act->a);
      if (!preset) break;
      set_str(&m->native_preset_id, preset->choice.id);
      set_fixed(m->native_skill, sizeof m->native_skill, preset->default_skill);
      set_str(&m->native_arena, NULL);
      clear_status(m);
      if (!streq(act->a, "q3-baseq3")) {
        qa_native_ui_controller_open_menu(m->controller, ID_DIFFICULTY);
        break;
      }
      set_status(m, "Loading arenas...", true);
      qa_caught_error caught;
      if (!qa_try(arenas_body, m, &caught)) { set_status(m, caught.message, false); break; }
      set_status(m, "", false);
      qa_native_ui_controller_open_menu(m->controller, ID_ARENAS);
      break;
    }
    case ACT_CAMPAIGN_PAGE: /* the closure's `pages` from the factory that built this button */
      m->campaign_page = fmod(m->campaign_page + 1, qa_js_string_to_number(act->a));
      break;
    case ACT_DIFFICULTY: set_fixed(m->native_skill, sizeof m->native_skill, act->a); break;
    case ACT_PLAY_PRESET:
      if (m->o.play_preset) m->o.play_preset(octx, act->a, qa_js_string_to_number(m->native_skill), m->native_arena);
      break;
    case ACT_SAVE_LOAD: m->o.load(octx, act->a); break;
    case ACT_SAVE_PAGE: m->save_page = fmod(m->save_page + 1, qa_js_string_to_number(act->a)); break;
    case ACT_REFRESH_SAVES: m->o.refresh_saves(octx); break;
    case ACT_HOST_APPLY: {
      hosting_call call = {m, m->host_draft};
      if (!streq(m->host_draft.kind, "offline")) call.value.port = qa_js_string_to_number(m->host_port);
      qa_caught_error caught;
      if (!qa_try(hosting_body, &call, &caught)) { set_status(m, caught.message, false); break; }
      clear_status(m);
      qa_native_ui_controller_close_menu(m->controller);
      break;
    }
    case ACT_SELECT_CHOICE: {
      if (m->monster_kind == 2) qa_app_startup_selection_select_monster(m->o.model, m->monster_classname, act->a);
      else if (m->monster_kind == 1) qa_app_startup_selection_select_monster_source(m->o.model, act->a);
      else qa_app_startup_selection_select(m->o.model, m->field, act->a);
      clear_status(m);
      qa_native_ui_controller_close_menu(m->controller);
      if (m->monster_kind == 0 && streq(m->field, "enemies") && streq(act->a, "custom")) open_roster(m);
      break;
    }
    case ACT_ROSTER_CLASS:
      m->monster_kind = 2;
      set_str(&m->monster_classname, act->a);
      qa_native_ui_controller_open_menu(m->controller, ID_SELECT);
      break;
    case ACT_ROSTER_SOURCE:
      m->monster_kind = 1;
      qa_native_ui_controller_open_menu(m->controller, ID_SELECT);
      break;
    case ACT_ROW_SELECT:
      m->monster_kind = 0;
      set_fixed(m->field, sizeof m->field, act->a);
      qa_native_ui_controller_open_menu(m->controller, ID_SELECT);
      prepare_choice_page(m, 0);
      break;
    case ACT_BUILDER_OPEN: {
      size_t count = 0;
      const qa_app_startup_selection_row *rows = safe_rows(m, &count);
      const qa_app_startup_selection_row *row = find_row(rows, count, act->a);
      if (!row) break;
      close_dropdown(m);
      m->dd.active = true;
      set_fixed(m->dd.field, sizeof m->dd.field, act->a);
      set_fixed(m->dd.stage, sizeof m->dd.stage, act->a);
      dropdown_open_state(m, row);
      m->preview_armed = false;
      break;
    }
    case ACT_BUILDER_CHOICE: {
      const char *stage = act->b;
      size_t count = 0;
      const qa_app_startup_selection_row *fresh = find_row(qa_app_startup_selection_rows(m->o.model, &count), count, stage);
      bool was_selected = fresh != NULL && streq(fresh->value, act->a);
      qa_app_startup_selection_select(m->o.model, stage, act->a);
      clear_status(m);
      if (!advance_stage(m) && (!act->flag || was_selected)) { close_dropdown(m); m->preview_armed = false; }
      if (streq(stage, "enemies") && streq(act->a, "custom")) open_roster(m);
      break;
    }
    case ACT_BUILDER_GROUP: {
      if (!m->dd.active) break;
      if (list_has(m->dd.collapsed, m->dd.collapsed_count, act->a)) {
        size_t w = 0;
        for (size_t i = 0; i < m->dd.collapsed_count; i++) {
          if (streq(m->dd.collapsed[i], act->a)) { free(m->dd.collapsed[i]); continue; }
          m->dd.collapsed[w++] = m->dd.collapsed[i];
        }
        m->dd.collapsed_count = w;
      } else dd_add_collapsed(&m->dd, act->a);
      break;
    }
    case ACT_BUILDER_STAGE_BACK: (void)retreat_stage(m); break;
    case ACT_BUILDER_EDITION: {
      /* switchEdition(edition) over the closure's dropdown.stage. */
      if (!m->dd.active) break;
      set_fixed(m->dd.edition, sizeof m->dd.edition, act->a);
      m->dd.has_edition = true;
      m->dd.scroll = 0;
      size_t count = 0;
      const qa_app_startup_selection_row *fresh = find_row(qa_app_startup_selection_rows(m->o.model, &count), count, act->b);
      const qa_app_startup_selection_choice *current = fresh ? find_choice(fresh, fresh->value) : NULL;
      if (!fresh || !current || !current->edition || streq(current->edition, act->a)) break;
      const qa_app_startup_selection_choice *counterpart = edition_variant(fresh, current, act->a);
      if (counterpart && !streq(counterpart->id, current->id) && !counterpart->unavailable) {
        qa_app_startup_selection_select(m->o.model, act->b, counterpart->id);
        clear_status(m);
      }
      break;
    }
    case ACT_PREVIEW_DONE: close_dropdown(m); m->preview_armed = false; break;
    case ACT_FOOTER_HOSTING:
      m->host_draft = qa_app_startup_selection_hosting(m->o.model);
      set_fixed(m->host_port, sizeof m->host_port, QA_JS_NUMBER(m->host_draft.port));
      clear_status(m);
      qa_native_ui_controller_open_menu(m->controller, ID_HOSTING);
      break;
    case ACT_FOOTER_MODS:
      if (m->mods_menu) qa_native_ui_controller_open_menu(m->controller, m->mods_menu);
      break;
    case ACT_FOOTER_BROWSE: qa_native_ui_controller_open_menu(m->controller, ID_BROWSER); break;
    case ACT_FOOTER_LOBBY: qa_app_startup_menu_open_local_lobby(m); break;
    case ACT_BROWSER_QUERY: browser_run(m, browser_query_body); break;
    /* TS browser.scan() has no catch of its own; a throw reaches input()'s catch. */
    case ACT_BROWSER_SCAN: qa_app_startup_server_browser_scan(m->o.browser); break;
    case ACT_BROWSER_FAVORITE: browser_run(m, browser_favorite_body); break;
    case ACT_BROWSER_ENTRY: qa_app_startup_server_browser_select(m->o.browser, act->a); break;
    case ACT_BROWSER_PAGE: m->page = fmod(m->page + 1, qa_js_string_to_number(act->a)); break;
    case ACT_BROWSER_FAV_ONLY:
      qa_app_startup_server_browser_set_favorites_only(m->o.browser, !qa_app_startup_server_browser_favorites_only(m->o.browser));
      m->page = 0;
      break;
    case ACT_BROWSER_CONNECT: browser_run(m, browser_connect_body); break;
    case ACT_BROWSER_DETAILS:
      m->page = 0;
      qa_native_ui_controller_open_menu(m->controller, ID_BROWSER_DET);
      break;
    case ACT_BROWSER_DISCOVER: {
      /* TS discover().catch(error => browser.status = String(error)). */
      qa_caught_error caught;
      if (!qa_try(browser_discover_body, m, &caught)) {
        char text[sizeof caught.message + 64];
        snprintf(text, sizeof text, "%s: %s", caught.name != NULL ? caught.name : "Error", caught.message);
        qa_app_startup_server_browser_set_status(m->o.browser, text);
      }
      break;
    }
    case ACT_BROWSER_DETAIL_PAGE: m->page = fmod(m->page + 1, qa_js_string_to_number(act->a)); break;
    default: break; /* ACT_NOOP: () => undefined */
  }
}
static void on_select(void *ctx, qa_seat_id seat, const char *choice) {
  (void)seat; /* TS select: (_seat, value) */
  action *act = ctx;
  qa_app_startup_menu *m = act->m;
  switch (act->kind) {
    case ACT_HOST_KIND:
      if (streq(choice, "offline")) m->host_draft.kind = "offline";
      else if (streq(choice, "native-server")) m->host_draft.kind = "native-server";
      else if (streq(choice, "unified-server")) m->host_draft.kind = "unified-server";
      break;
    case ACT_HOST_Q1:
      if (streq(choice, "15") || streq(choice, "666") || streq(choice, "999")) {
        m->host_draft.has_q1_protocol = true;
        m->host_draft.q1_protocol = qa_default_netquake_profile(qa_js_string_to_number(choice));
      }
      break;
    case ACT_GRAPPLE: qa_app_startup_selection_select(m->o.model, "grapple", choice); break;
    case ACT_FIELD_SELECT: qa_app_startup_selection_select(m->o.model, act->a, choice); break;
    case ACT_TEAM: qa_app_startup_selection_team_write(m->o.model, act->a, choice); break;
    case ACT_BROWSER_PROTOCOL: qa_app_startup_server_browser_choose(m->o.browser, choice); m->page = 0; break;
    case ACT_BROWSER_SORT:
      qa_app_startup_server_browser_choose_sort(m->o.browser, choice);
      set_fixed(m->browser_sort, sizeof m->browser_sort, choice);
      m->page = 0; break;
    default: break;
  }
}
static void on_checked(void *ctx, qa_seat_id seat, bool checked) {
  (void)seat; /* TS change: (_seat, value) */
  action *act = ctx;
  qa_app_startup_menu *m = act->m;
  switch (act->kind) {
    case ACT_GRENADES: qa_app_startup_selection_select(m->o.model, "grenades", checked ? "enabled" : "disabled"); break;
    case ACT_BROWSER_HIDE_EMPTY: qa_app_startup_server_browser_set_hide_empty(m->o.browser, checked); m->page = 0; break;
    case ACT_BROWSER_HIDE_FULL: qa_app_startup_server_browser_set_hide_full(m->o.browser, checked); m->page = 0; break;
    default: break;
  }
}
static void on_change_text(void *ctx, qa_seat_id seat, const char *text) {
  (void)seat; /* TS change: (_seat, value) */
  action *act = ctx;
  qa_app_startup_menu *m = act->m;
  switch (act->kind) {
    case ACT_HOST_PORT: set_fixed(m->host_port, sizeof m->host_port, text); break;
    case ACT_BROWSER_ADDRESS: qa_app_startup_server_browser_set_address(m->o.browser, text); break;
    case ACT_BROWSER_FILTER: qa_app_startup_server_browser_set_filter(m->o.browser, text); m->page = 0; break;
    case ACT_BROWSER_MASTER: qa_app_startup_server_browser_set_master_address(m->o.browser, text); break;
    default: break;
  }
}
static void on_submit_text(void *ctx, qa_seat_id seat, const char *text) {
  (void)seat; (void)text; /* TS submit: () => ... */
  action *act = ctx;
  if (act->kind == ACT_BROWSER_ADDRESS) browser_run(act->m, browser_query_body);
}

/* ---- menu factories ---------------------------------------------------------------- */
static void noop_open(void *ctx, qa_seat_id seat) { (void)ctx; (void)seat; /* open/close: () => undefined */ }
static scratch *begin(slot *s) { sc_begin(&s->sc, &s->m->retired); return &s->sc; }
static qa_ui_menu finish_menu(slot *s, qa_ui_menu_id id) {
  qa_ui_menu menu;
  memset(&menu, 0, sizeof menu);
  menu.id = id; menu.title = ""; menu.full_screen = false;
  menu.controls = s->sc.controls; menu.control_count = s->sc.count;
  menu.open = noop_open; menu.close = noop_open;
  return menu;
}
static qa_ui_menu finish_scroll(slot *s, qa_ui_menu_id id, qa_rect rect, double content_height, size_t first, size_t count) {
  scratch *sc = &s->sc;
  qa_ui_control_id *ids = sc_alloc(sc, (count ? count : 1) * sizeof *ids);
  for (size_t i = 0; i < count; i++) ids[i] = sc->controls[first + i].id;
  sc->scroll = (qa_ui_menu_scroll){rect, content_height, ids, count};
  qa_ui_menu menu = finish_menu(s, id);
  menu.scroll = &sc->scroll;
  return menu;
}
static qa_ui_menu build_main(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = begin(s);
  button(sc, m, "native", "Play a game", 0, false, ACT_OPEN, ID_FAMILY, "clear");
  button(sc, m, "load", "Load Game", 1, false, ACT_LOAD_MENU, NULL, NULL);
  button(sc, m, "options", "Options", 2, false, ACT_OPEN, ID_OPTIONS, NULL);
  if (m->o.has_libraries) button(sc, m, "library", "Library", 3, false, ACT_OPEN, ID_LIBRARY, NULL);
  button(sc, m, "quit", "Quit", m->o.has_libraries ? 4 : 3, false, ACT_QUIT, NULL, NULL);
  return finish_menu(s, ID_MAIN);
}
static qa_ui_menu build_library(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = begin(s);
  for (size_t i = 0; i < m->library_page_count; i++) {
    char *id = sc_fmt(sc, "library:%s", m->library_pages[i].name);
    button(sc, m, id, m->library_pages[i].label, (double)i, true, ACT_OPEN, m->library_pages[i].root, NULL);
  }
  back_button(sc, m);
  return finish_menu(s, ID_LIBRARY);
}
static qa_ui_menu build_family(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = begin(s);
  static const struct { const char *family; const char *label; } games[] = {{"q1", "Quake"}, {"q2", "Quake II"}, {"q3", "Quake III"}};
  for (size_t i = 0; i < 3; i++) {
    char *id = sc_fmt(sc, "game:%s", games[i].family);
    qa_ui_control *c = button(sc, m, id, games[i].label, (double)i, true, ACT_FAMILY, games[i].family, NULL);
    size_t count = 0;
    const qa_app_startup_native_preset *list = presets(m, &count);
    bool any = false;
    for (size_t j = 0; j < count && !any; j++) if (streq(family_name(list[j].family), games[i].family)) any = true;
    c->enabled = !m->busy && any;
  }
  button(sc, m, "custom", "Custom game", 3, true, ACT_CUSTOM, NULL, NULL);
  back_button(sc, m);
  return finish_menu(s, ID_FAMILY);
}
static qa_ui_menu build_campaign(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = begin(s);
  size_t count = 0;
  const qa_app_startup_native_preset *list = presets(m, &count);
  bool has_c = false, has_r = false;
  for (size_t i = 0; i < count; i++) if (streq(family_name(list[i].family), m->native_family)) {
    if (streq(list[i].edition, "classic")) has_c = true;
    if (streq(list[i].edition, "rerelease")) has_r = true;
  }
  bool toggle = has_c && has_r;
  double offset = toggle ? 1 : 0;
  size_t total = native_campaigns(m);
  double per_page = 8 - offset;
  double pages = qa_js_max(1, ceil((double)total / per_page));
  m->campaign_page = qa_js_min(m->campaign_page, pages - 1);
  size_t start = (size_t)(m->campaign_page * per_page);
  for (size_t i = start; i < total && i < start + (size_t)per_page; i++) {
    const qa_app_startup_native_preset *preset = m->campaigns[i];
    char *label = fit(sc, m, sc_fmt(sc, "%s%s", preset->choice.label, preset->choice.unavailable ? " (unavailable)" : ""),
                      486, qa_ui_menu_text.body);
    char *id = sc_fmt(sc, "preset:%s", preset->choice.id);
    qa_ui_control *c = button(sc, m, id, label, offset + (double)(i - start), true, ACT_PRESET, preset->choice.id, NULL);
    c->enabled = !m->busy && !preset->choice.unavailable;
  }
  if (toggle) {
    static const char *const editions[] = {"classic", "rerelease"};
    for (int i = 0; i < 2; i++) {
      char *id = sc_fmt(sc, "edition-%s", editions[i]);
      char *label = sc_fmt(sc, "%s%s", streq(m->native_edition, editions[i]) ? "> " : "", i == 0 ? "Classic" : "Rerelease");
      qa_ui_control *c = button(sc, m, id, label, 0, true, ACT_EDITION, editions[i], NULL);
      c->rect = (qa_rect){i == 0 ? 64 : 328, 114, 248, 29};
    }
  }
  if (pages > 1) {
    char *label = sc_fmt(sc, "More campaigns (%s/%s)", QA_JS_NUMBER(m->campaign_page + 1), QA_JS_NUMBER(pages));
    button(sc, m, "campaign-page", label, 8, true, ACT_CAMPAIGN_PAGE, QA_JS_NUMBER(pages), NULL);
  }
  back_button(sc, m);
  return finish_menu(s, ID_CAMPAIGN);
}
static qa_ui_menu build_difficulty(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = begin(s);
  const qa_app_startup_native_preset *preset = find_preset(m, m->native_preset_id);
  if (!preset) { back_button(sc, m); return finish_menu(s, ID_DIFFICULTY); }
  for (size_t i = 0; i < preset->difficulty_count; i++) {
    const qa_app_startup_selection_choice *d = &preset->difficulties[i];
    char *id = sc_fmt(sc, "difficulty:%s", d->id);
    char *label = sc_fmt(sc, "%s%s", streq(m->native_skill, d->id) ? "> " : "", d->label);
    qa_ui_control *c = button(sc, m, id, label, (double)i, true, ACT_DIFFICULTY, d->id, NULL);
    c->enabled = !m->busy && !d->unavailable;
  }
  if (streq(preset->choice.id, "q3-missionpack") && m->o.team_arena) {
    static const char *const sides[] = {"player", "opponent"};
    for (int i = 0; i < 2; i++) {
      size_t choice_count = 0;
      const qa_ui_choice *choices = qa_app_startup_selection_team_choices(m->o.model, &choice_count);
      const char *player = "", *opponent = "";
      qa_app_startup_selection_team_read(m->o.model, &player, &opponent);
      char *id = sc_fmt(sc, "ui:startup:team:%s", sides[i]);
      choice_control(sc, m, id, i == 0 ? "Your team" : "Opponent", qa_ui_menu_row_default(5.0 + i), !m->busy, true,
                     choices, choice_count, i == 0 ? player : opponent, ACT_TEAM, sides[i]);
    }
  }
  qa_ui_control *play = button(sc, m, "play-preset", "Play", 7, true, ACT_PLAY_PRESET, preset->choice.id, NULL);
  play->enabled = !m->busy && !preset->choice.unavailable && m->o.play_preset != NULL;
  back_button(sc, m);
  return finish_menu(s, ID_DIFFICULTY);
}
static qa_ui_menu build_options(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = begin(s);
  button(sc, m, "display", "Display", 0, true, ACT_OPEN, ID_DISPLAY, NULL);
  button(sc, m, "sound", "Sound", 1, true, ACT_OPEN, ID_SOUND, NULL);
  button(sc, m, "controls", "Controls", 2, true, ACT_OPEN, ID_CONTROLS, NULL);
  bool accessibility = false;
  for (size_t i = 0; i < m->o.setting_count; i++) if (streq(m->o.settings[i].category, "accessibility")) accessibility = true;
  if (accessibility) button(sc, m, "accessibility", "Accessibility", 3, true, ACT_OPEN, ID_ACCESS, NULL);
  button(sc, m, "all-options", "All options", 4, true, ACT_OPEN, "menu:settings:root", NULL);
  if (m->has_llm) button(sc, m, "llm", "LLM options", 5, true, ACT_OPEN, m->llm_root, NULL);
  back_button(sc, m);
  return finish_menu(s, ID_OPTIONS);
}
static qa_ui_menu build_hosting(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = begin(s);
  static const qa_ui_choice kinds[] = {{"offline", "Local only"}, {"native-server", "Native game clients"},
                                       {"unified-server", "This client: mixed games"}};
  choice_control(sc, m, "ui:startup:host-kind", "Connections", qa_ui_menu_row_default(0), !m->busy, true, kinds, 3,
                 m->host_draft.kind, ACT_HOST_KIND, NULL);
  text_control(sc, m, "ui:startup:host-port", "Port", m->host_port, 5, qa_ui_menu_row_default(1),
               !m->busy && !streq(m->host_draft.kind, "offline"), ACT_HOST_PORT);
  static const qa_ui_choice protocols[] = {{"15", "NetQuake (15)"}, {"666", "FitzQuake (666)"}, {"999", "RMQ (999)"}};
  char *version = sc_str(sc, QA_JS_NUMBER(m->host_draft.has_q1_protocol ? (double)m->host_draft.q1_protocol.version : 15));
  choice_control(sc, m, "ui:startup:host-q1-protocol", "Quake protocol", qa_ui_menu_row_default(2), !m->busy,
                 streq(m->host_draft.kind, "native-server") && m->host_draft.has_q1_protocol, protocols, 3, version, ACT_HOST_Q1, NULL);
  qa_app_options options;
  qa_app_startup_selection_options(m->o.model, &options);
  bool singleplayer = streq(options.mode, "singleplayer");
  qa_app_options_destroy(&options);
  const char *note = "Choose game rules and difficulty in Combat.";
  if (singleplayer && !streq(m->host_draft.kind, "offline"))
    note = selected_expectation(m)->family == QA_GAME_FAMILY_Q3 ? "Uses Deathmatch. Change rules in Combat."
                                                                 : "Uses Co-op. Change rules in Combat.";
  qa_ui_control *info = button(sc, m, "host-mode", note, 3, true, ACT_NOOP, NULL, NULL);
  info->enabled = false;
  button(sc, m, "host-apply", "Apply", 4, true, ACT_HOST_APPLY, NULL, NULL);
  back_button(sc, m);
  return finish_menu(s, ID_HOSTING);
}
static qa_ui_menu build_sound(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = begin(s);
  size_t count = 0;
  const qa_app_startup_selection_row *rows = safe_rows(m, &count);
  static const char *const fields[] = {"environment", "doppler"};
  size_t index = 0;
  for (int i = 0; i < 2; i++) {
    const qa_app_startup_selection_row *row = find_row(rows, count, fields[i]);
    if (!row) continue;
    push_row(sc, m, row, (double)index);
    sc->controls[sc->count - 1].rect = (qa_rect){64, 114 + (double)index * 36, 496, 32};
    index++;
  }
  for (size_t i = 0; i < m->o.setting_count; i++) {
    if (!streq(m->o.settings[i].category, "audio")) continue;
    qa_rect rect = {64, 114 + (double)index * 36, 496, 32};
    qa_ui_setting_control_storage *storage = sc_alloc(sc, sizeof *storage);
    qa_ui_control control = qa_ui_setting_control(&m->o.settings[i], rect, m->o.seat, storage);
    *sc_push(sc) = control;
    index++;
  }
  size_t scroll_controls = sc->count;
  back_button(sc, m);
  return finish_scroll(s, ID_SOUND, (qa_rect){64, 114, 512, 288}, (double)scroll_controls * 36, 0, scroll_controls);
}
static qa_ui_menu build_select(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = begin(s);
  const qa_app_startup_selection_row *row = selection_row(m);
  size_t total = row ? row->choice_count : 0;
  for (size_t i = 0; i < total; i++) {
    const qa_app_startup_selection_choice *choice = &row->choices[i];
    char *fitted = fit(sc, m, sc_fmt(sc, "%s%s", choice->label, choice->unavailable ? " (unavailable)" : ""), 466, qa_ui_menu_text.body);
    char *label = sc_fmt(sc, "%s%s", streq(row->value, choice->id) ? "> " : "", fitted);
    char *id = sc_fmt(sc, "choice:%s", choice->id);
    qa_ui_control *c = button(sc, m, id, label, (double)i, true, ACT_SELECT_CHOICE, choice->id, NULL);
    c->enabled = !choice->unavailable && !m->busy;
  }
  back_button(sc, m);
  return finish_scroll(s, ID_SELECT, (qa_rect){64, 114, 512, 297}, (double)total * 33, 0, total);
}
static qa_ui_menu build_roster(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = begin(s);
  size_t count = 0;
  const qa_app_monster_roster_row *rows = qa_app_startup_selection_monster_roster_rows(m->o.model, &count);
  qa_app_startup_selection_row source = qa_app_startup_selection_monster_source_row(m->o.model);
  const qa_app_startup_selection_choice *source_choice = find_choice(&source, source.value);
  const char *source_label = source_choice ? source_choice->label : source.value;
  /* The row controls are built before the source head, which the menu lists first. */
  qa_ui_control *row_controls = sc_alloc(sc, (count ? count : 1) * sizeof *row_controls);
  for (size_t i = 0; i < count; i++) {
    char *id = sc_fmt(sc, "monster:%s", rows[i].classname ? rows[i].classname : "default");
    char *text = fit(sc, m, sc_fmt(sc, "%s: %s", rows[i].label, rows[i].effective_label), 486, qa_ui_menu_text.body);
    fill_button(&row_controls[i], sc, m, id, text, (double)i, true, ACT_ROSTER_CLASS, rows[i].classname, NULL);
    row_controls[i].rect = (qa_rect){64, 114 + (double)i * 33, 512, 29};
  }
  char *label = fit(sc, m, sc_fmt(sc, "Monster source: %s", source_label), 486, qa_ui_menu_text.body);
  qa_ui_control *head = button(sc, m, "monster-source", label, 0, true, ACT_ROSTER_SOURCE, NULL, NULL);
  head->rect = (qa_rect){64, 78, 512, 26};
  for (size_t i = 0; i < count; i++) *sc_push(sc) = row_controls[i];
  back_button(sc, m);
  return finish_scroll(s, ID_ROSTER, (qa_rect){64, 114, 512, 297}, (double)count * 33, 1, count);
}
static qa_ui_menu build_load(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = begin(s);
  const qa_app_startup_save_list *saves = m->o.saves(m->o.ctx);
  size_t total = saves->row_count;
  double pages = qa_js_max(1, ceil((double)total / 5));
  m->save_page = qa_js_min(m->save_page, pages - 1);
  size_t start = (size_t)(m->save_page * 5);
  for (size_t i = start; i < total && i < start + 5; i++) {
    const qa_app_startup_save_row *save = &saves->rows[i];
    char *id = sc_fmt(sc, "save:%s", save->id);
    char *label = fit(sc, m, sc_fmt(sc, "%s  -  %s", save->label, save->map), 486, qa_ui_menu_text.body);
    qa_ui_control *c = button(sc, m, id, label, (double)(i - start), true, ACT_SAVE_LOAD, save->id, NULL);
    c->rect = (qa_rect){64, 118 + (double)(i - start) * 46, 512, 42};
    c->enabled = !save->unavailable && !m->busy;
  }
  if (pages > 1) button(sc, m, "older-saves", "Older saves", 7, true, ACT_SAVE_PAGE, QA_JS_NUMBER(pages), NULL);
  button(sc, m, "refresh-saves", "Refresh", 8, true, ACT_REFRESH_SAVES, NULL, NULL);
  back_button(sc, m);
  return finish_menu(s, ID_LOAD);
}
/* TS session factory: the two-column create-game screen (heads, labels and values are draw()-only). */
static qa_ui_menu build_session(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = begin(s);
  builder_layout spec;
  builder_layout_build(sc, m, &spec);
  if (spec.has_dropdown) {
    for (size_t i = 0; i < spec.dropdown.item_count; i++) *sc_push(sc) = *spec.dropdown.items[i].control;
    if (spec.dropdown.has_toggle) { *sc_push(sc) = *spec.dropdown.classic; *sc_push(sc) = *spec.dropdown.rerelease; }
  }
  if (spec.has_viewer) *sc_push(sc) = *spec.viewer.control;
  for (size_t i = 0; i < spec.field_count; i++) *sc_push(sc) = *spec.fields[i].control;
  for (size_t i = 0; i < spec.footer_count; i++) *sc_push(sc) = *spec.footer[i].control;
  return finish_menu(s, ID_SESSION);
}
static qa_ui_control *browser_button(scratch *sc, qa_app_startup_menu *m, const char *id, const char *label,
                                     double x, double y, double width, int kind, const char *a) {
  qa_ui_control *c = button(sc, m, id, label, 0, false, kind, a, NULL);
  c->rect = (qa_rect){x, y, width, 30};
  return c;
}
static qa_ui_menu build_browser(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = begin(s);
  qa_app_startup_server_browser *browser = m->o.browser;
  if (!browser) { back_button(sc, m); return finish_menu(s, ID_BROWSER); }
  size_t total = 0;
  const qa_browser_entry **entries = qa_app_startup_server_browser_rows(browser, &total);
  double pages = qa_js_max(1, ceil((double)total / 3));
  m->page = qa_js_min(m->page, pages - 1);
  static const qa_ui_choice protocols[] = {{"q1", "Quake"}, {"qw", "QuakeWorld"}, {"q2", "Quake II"}, {"q3", "Quake III Arena"}};
  choice_control(sc, m, "ui:startup:server-protocol", "Game", (qa_rect){64, 108, 512, 30}, true, true, protocols, 4,
                 qa_app_startup_server_browser_protocol(browser), ACT_BROWSER_PROTOCOL, NULL);
  text_control(sc, m, "ui:startup:server-address", "Address", qa_app_startup_server_browser_address(browser), 255,
               (qa_rect){64, 144, 512, 30}, !m->busy, ACT_BROWSER_ADDRESS);
  browser_button(sc, m, "server-query", "Query", 64, 180, 160, ACT_BROWSER_QUERY, NULL);
  browser_button(sc, m, "server-lan", "Find LAN", 240, 180, 160, ACT_BROWSER_SCAN, NULL);
  browser_button(sc, m, "server-favorite", "Favorite", 416, 180, 160, ACT_BROWSER_FAVORITE, NULL);
  text_control(sc, m, "ui:startup:server-filter", "Filter", qa_app_startup_server_browser_filter(browser), 255,
               (qa_rect){64, 216, 320, 30}, !m->busy, ACT_BROWSER_FILTER);
  bool filters_on = qa_app_startup_server_browser_hide_empty(browser) || qa_app_startup_server_browser_hide_full(browser);
  browser_button(sc, m, "server-sort-filter", filters_on ? "Filters on" : "Sort/filter", 400, 216, 176, ACT_OPEN, ID_BROWSER_OPTS);
  size_t start = (size_t)(m->page * 3);
  for (size_t i = start; entries && i < total && i < start + 3; i++) {
    const qa_browser_entry *e = entries[i];
    bool favorite = false;
    for (size_t j = 0; j < e->source_count; j++) if (streq(e->sources[j], "favorite")) favorite = true;
    char *address = qa_app_browser_address(&e->address);
    pool_add(&sc->current, address);
    const char *name = e->has_status && e->status.name && e->status.name[0] ? e->status.name : address;
    char *counts = e->has_status ? sc_fmt(sc, "%s/%s", QA_JS_NUMBER(e->status.players), QA_JS_NUMBER(e->status.max_players))
                                 : sc_str(sc, "?");
    char *ping = e->has_ping ? sc_fmt(sc, "%sms", QA_JS_NUMBER(qa_js_round(e->ping_ms))) : sc_str(sc, "");
    char *label = fit(sc, m, sc_fmt(sc, "%s%s  %s  %s", favorite ? "* " : "", name, counts, ping), 490, qa_ui_menu_text.body);
    char *key = qa_net_address_key(&e->address, true);
    pool_add(&sc->current, key);
    char *id = sc_fmt(sc, "server:%s", key);
    browser_button(sc, m, id, label, 64, 252 + (double)(i - start) * 34, 512, ACT_BROWSER_ENTRY, key);
  }
  char *page_label = sc_fmt(sc, "Page %s/%s", QA_JS_NUMBER(m->page + 1), QA_JS_NUMBER(pages));
  browser_button(sc, m, "server-page", page_label, 64, 358, 240, ACT_BROWSER_PAGE, QA_JS_NUMBER(pages));
  browser_button(sc, m, "server-favorites", qa_app_startup_server_browser_favorites_only(browser) ? "Favorites only" : "All servers",
                 320, 358, 256, ACT_BROWSER_FAV_ONLY, NULL);
  browser_button(sc, m, "server-connect", "Connect", 64, 396, 160, ACT_BROWSER_CONNECT, NULL);
  browser_button(sc, m, "server-details", "Details", 240, 396, 160, ACT_BROWSER_DETAILS, NULL);
  browser_button(sc, m, "server-back", "Back", 416, 396, 160, ACT_CLOSE_MENU, NULL);
  return finish_menu(s, ID_BROWSER);
}
static qa_ui_menu build_browser_options(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = begin(s);
  qa_app_startup_server_browser *browser = m->o.browser;
  if (!browser) { back_button(sc, m); return finish_menu(s, ID_BROWSER_OPTS); }
  qa_ui_choice sorts[8];
  for (int i = 0; i < 8; i++) sorts[i] = (qa_ui_choice){qa_app_browser_sort_orders[i].id, qa_app_browser_sort_orders[i].label};
  choice_control(sc, m, "ui:startup:server-sort", "Sort", (qa_rect){64, 118, 512, 30}, true, true, sorts, 8, m->browser_sort,
                 ACT_BROWSER_SORT, NULL);
  static const struct { const char *id; const char *label; double y; int kind; } toggles[] = {
    {"ui:startup:server-hide-empty", "Hide empty", 152, ACT_BROWSER_HIDE_EMPTY},
    {"ui:startup:server-hide-full", "Hide full", 186, ACT_BROWSER_HIDE_FULL}};
  for (int i = 0; i < 2; i++) {
    qa_ui_control *c = sc_push(sc);
    c->id = toggles[i].id; c->kind = "toggle"; c->label = toggles[i].label;
    c->rect = (qa_rect){64, toggles[i].y, 512, 30};
    c->enabled = true; c->visible = true;
    c->checked = toggles[i].kind == ACT_BROWSER_HIDE_EMPTY ? qa_app_startup_server_browser_hide_empty(browser)
                                                           : qa_app_startup_server_browser_hide_full(browser);
    c->ctx = sc_action(sc, m, toggles[i].kind, NULL, NULL);
    c->change_checked = on_checked;
  }
  text_control(sc, m, "ui:startup:server-master", "Master address or HTTP list", qa_app_startup_server_browser_master_address(browser),
               2048, (qa_rect){64, 226, 512, 30}, true, ACT_BROWSER_MASTER);
  button(sc, m, "server-discover", "Find Internet servers", 5, true, ACT_BROWSER_DISCOVER, NULL, NULL);
  button(sc, m, "server-filters-back", "Back", 7, true, ACT_CLOSE_MENU, NULL, NULL);
  return finish_menu(s, ID_BROWSER_OPTS);
}
static qa_ui_menu build_browser_details(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = begin(s);
  qa_app_startup_server_browser *browser = m->o.browser;
  if (!browser) { back_button(sc, m); return finish_menu(s, ID_BROWSER_DET); }
  size_t line_count = 0;
  char **lines = qa_app_startup_server_browser_details(browser, &line_count);
  for (size_t i = 0; i < line_count; i++) pool_add(&sc->current, lines[i]);
  pool_add(&sc->current, lines);
  /* Lines longer than 64 split into 64-unit chunks (UTF-16 units in the TS; bytes here). */
  size_t chunks = 0;
  for (size_t i = 0; i < line_count; i++) { size_t length = strlen(lines[i]); chunks += length <= 64 ? 1 : (length + 63) / 64; }
  double pages = qa_js_max(1, ceil((double)chunks / 8));
  m->page = qa_js_min(m->page, pages - 1);
  size_t skip = (size_t)(m->page * 8), shown = 0, seen = 0;
  for (size_t i = 0; i < line_count && shown < 8; i++) {
    size_t length = strlen(lines[i]);
    size_t parts = length <= 64 ? 1 : (length + 63) / 64;
    for (size_t part = 0; part < parts && shown < 8; part++, seen++) {
      if (seen < skip) continue;
      size_t take = length - part * 64 > 64 ? 64 : length - part * 64;
      char *chunk = sc_fmt(sc, "%.*s", (int)take, lines[i] + part * 64);
      char *id = sc_fmt(sc, "server-detail:%zu", shown);
      qa_ui_control *c = button(sc, m, id, chunk, (double)shown, true, ACT_NOOP, NULL, NULL);
      c->enabled = false;
      shown++;
    }
  }
  char *label = sc_fmt(sc, "Page %s/%s", QA_JS_NUMBER(m->page + 1), QA_JS_NUMBER(pages));
  button(sc, m, "server-detail-page", label, 8, true, ACT_BROWSER_DETAIL_PAGE, QA_JS_NUMBER(pages), NULL);
  back_button(sc, m);
  return finish_menu(s, ID_BROWSER_DET);
}

/* ---- controller options ------------------------------------------------------------ */
static qa_ui_skin ctrl_skin(void *ctx) { return ((qa_app_startup_menu *)ctx)->skin; }
static double ctrl_now(void *ctx) { qa_app_startup_menu *m = ctx; return m->o.now(m->o.ctx); }
static const qa_input_binding *ctrl_bindings(void *ctx, size_t *out_count) {
  (void)ctx; *out_count = 0; return NULL; /* bindings: () => [] */
}
static void ctrl_focus(void *ctx, qa_seat_input_focus focus, double now_ms) {
  (void)ctx; (void)focus; (void)now_ms; /* focus: () => undefined */
}
static void ctrl_sound(void *ctx, qa_ui_sound sound, qa_seat_id seat) {
  (void)seat; /* sound: sound => options.sound?.(sound) */
  qa_app_startup_menu *m = ctx;
  if (m->o.sound) m->o.sound(m->o.ctx, sound);
}
static void ctrl_script(void *ctx, qa_legacy_ui_script script, qa_seat_id seat) {
  (void)ctx; (void)script; (void)seat; /* executeScript: () => { throw } */
  qa_raise("Error", 0, 0, "Startup menu has no legacy scripts");
}
static const char *ctrl_clipboard(void *ctx) { qa_app_startup_menu *m = ctx; return m->o.clipboard(m->o.ctx); }
static double ctrl_measure(void *ctx, const char *text, double scale) { return measure(ctx, text, scale); }
static qa_native_ui_appearance ctrl_appearance(void *ctx) {
  qa_ui_preference_values values = appearance(ctx);
  return (qa_native_ui_appearance){values.menu_scale, values.text_scale, values.high_contrast, values.color_mode};
}
static const qa_app_arena_selection *arena_read(void *ctx) {
  return qa_app_startup_selection_base_arenas(((qa_app_startup_menu *)ctx)->o.model);
}
static void arena_choose(void *ctx, const char *map) {
  qa_app_startup_menu *m = ctx;
  set_str(&m->native_arena, map);
  qa_native_ui_controller_open_menu(m->controller, ID_DIFFICULTY);
}
static bool bindings_enabled(void *ctx) { return ((qa_app_startup_menu *)ctx)->binding_menu != NULL; }
static void bindings_activate(void *ctx) {
  qa_app_startup_menu *m = ctx;
  if (m->binding_menu) qa_native_ui_controller_open_menu(m->controller, m->binding_menu);
}
static bool gyro_enabled(void *ctx) { return ((qa_app_startup_menu *)ctx)->gyro_menu != NULL; }
static void gyro_activate(void *ctx) {
  qa_app_startup_menu *m = ctx;
  if (m->gyro_menu) qa_native_ui_controller_open_menu(m->controller, m->gyro_menu);
}
static const qa_ui_application_local_lobby *lobby_current(void *ctx) {
  qa_app_startup_menu *m = ctx;
  return m->o.lobby_current(m->o.ctx);
}
static const char *lobby_selection(void *ctx, qa_ui_local_lobby_selection *out) {
  qa_app_startup_menu *m = ctx;
  return m->o.lobby_selection(m->o.ctx, out);
}
static double lobby_seats(void *ctx) {
  qa_app_startup_menu *m = ctx;
  return m->o.lobby_seats(m->o.ctx);
}
static void register_slot(qa_app_startup_menu *m, int index, qa_ui_menu_id id, qa_ui_menu_factory_fn factory) {
  slot *s = &m->slots[index];
  s->m = m; s->index = index;
  s->handle = qa_native_ui_controller_register(m->controller, id, factory, s);
  if (s->handle < 0) qa_range_error("Out of memory");
}

qa_app_startup_menu *qa_app_startup_menu_new(qa_app_startup_menu_options options) {
  qa_app_startup_menu *m = calloc(1, sizeof *m);
  if (!m) return NULL;
  m->o = options;
  m->status = xdup("");
  for (int i = 0; i < SLOT_COUNT; i++) m->slots[i].handle = -1;
  set_fixed(m->field, sizeof m->field, "product");
  set_fixed(m->native_family, sizeof m->native_family, "q1");
  set_fixed(m->native_edition, sizeof m->native_edition, "classic");
  set_fixed(m->native_skill, sizeof m->native_skill, "1");
  m->host_draft.kind = "offline"; m->host_draft.port = 27910;
  set_fixed(m->host_port, sizeof m->host_port, "27910");
  set_fixed(m->browser_sort, sizeof m->browser_sort, "ping-low");
  /* text = new UiTextRenderer(seat); bind(art.skin.font, font); bind(menuTitleFont, titleFont) */
  qa_ui_text_renderer_init(&m->text, options.seat);
  if (!qa_ui_text_renderer_bind(&m->text, options.art->skin.font, options.font)
      || !qa_ui_text_renderer_bind(&m->text, qa_ui_menu_title_font, options.title_font)) qa_range_error("Out of memory");
  m->skin = qa_ui_menu_skin(options.art->skin.font);
  qa_native_ui_options controller_options;
  memset(&controller_options, 0, sizeof controller_options);
  controller_options.ctx = m;
  controller_options.seat = options.seat;
  controller_options.skin = ctrl_skin;
  controller_options.appearance = ctrl_appearance;
  controller_options.now = ctrl_now;
  controller_options.bindings = ctrl_bindings;
  if (options.clipboard) controller_options.clipboard = ctrl_clipboard;
  controller_options.focus = ctrl_focus;
  controller_options.sound = ctrl_sound;
  controller_options.measure_text = ctrl_measure;
  controller_options.execute_script = ctrl_script;
  m->controller = qa_native_ui_controller_create(&controller_options);
  if (!m->controller) { qa_ui_text_renderer_destroy(&m->text); free(m->status); free(m); return NULL; }
  register_slot(m, SLOT_MAIN, ID_MAIN, build_main);
  static const struct { const char *name; const char *label; } library_names[6] = {
    {"addons", "Add-ons \xe2\x80\x94 Quaddicted"}, {"demos", "Demos"}, {"movies", "Movies"},
    {"configurations", "Configurations"}, {"server-profiles", "Server profiles"},
    {"player-progress", "Achievements and progress"}};
  const qa_ui_library_menu_service *services[6] = {
    options.library_addons, options.library_demos, options.library_movies,
    options.library_configurations, options.library_server_profiles, options.library_player_progress};
  for (int i = 0; options.has_libraries && i < 6; i++) {
    if (!services[i]) continue;
    char id[96]; snprintf(id, sizeof id, "menu:library:%s", library_names[i].name);
    qa_ui_settings_menus page = qa_ui_register_library_menu(m->controller, id, library_names[i].label, services[i]);
    size_t at = m->library_page_count++;
    m->library_pages[at].name = library_names[i].name;
    m->library_pages[at].label = library_names[i].label;
    m->library_pages[at].root = page.root;
    m->library_pages[at].menus = page;
  }
  register_slot(m, SLOT_LIBRARY, ID_LIBRARY, build_library);
  register_slot(m, SLOT_FAMILY, ID_FAMILY, build_family);
  m->arena_menu = qa_app_register_arena_selection_menu(m->controller, ID_ARENAS,
      (qa_app_arena_selection_menu_service){m, arena_read, arena_choose});
  register_slot(m, SLOT_CAMPAIGN, ID_CAMPAIGN, build_campaign);
  register_slot(m, SLOT_DIFFICULTY, ID_DIFFICULTY, build_difficulty);
  register_slot(m, SLOT_BROWSER, ID_BROWSER, build_browser);
  register_slot(m, SLOT_BROWSER_OPTS, ID_BROWSER_OPTS, build_browser_options);
  register_slot(m, SLOT_BROWSER_DET, ID_BROWSER_DET, build_browser_details);
  if (options.has_lobby) {
    m->lobby_menus = qa_ui_register_local_lobby_menu(m->controller, lobby_current, m, lobby_selection, m, lobby_seats, m);
    m->lobby_menu = m->lobby_menus.root;
    m->has_lobby_menus = true;
  }
  m->mods_menus = qa_ui_register_mod_menu(m->controller, qa_app_startup_selection_mods(options.model));
  m->mods_menu = m->mods_menus.root;
  m->has_mods = true;
  register_slot(m, SLOT_SESSION, ID_SESSION, build_session);
  register_slot(m, SLOT_HOSTING, ID_HOSTING, build_hosting);
  if (options.llm) {
    m->llm_menus = qa_ui_register_llm_settings_menu(m->controller, options.llm);
    m->llm_root = m->llm_menus.root;
    m->has_llm = true;
  }
  register_slot(m, SLOT_OPTIONS, ID_OPTIONS, build_options);
  m->all_binding_count = options.setting_count + 2;
  m->all_bindings = calloc(m->all_binding_count, sizeof *m->all_bindings);
  if (!m->all_bindings) qa_range_error("Out of memory");
  qa_ui_setting_binding *bindings = &m->all_bindings[0];
  bindings->id = "ui:startup:bindings"; bindings->category = "input"; bindings->kind = "button";
  bindings->label = "Bindings (Player 1)"; bindings->ctx = m;
  bindings->enabled = bindings_enabled; bindings->activate = bindings_activate;
  if (options.setting_count) memcpy(&m->all_bindings[1], options.settings, options.setting_count * sizeof *m->all_bindings);
  qa_ui_setting_binding *gyro = &m->all_bindings[options.setting_count + 1];
  gyro->id = "ui:startup:gyro"; gyro->category = "input"; gyro->kind = "button";
  gyro->label = "Gyro controls"; gyro->ctx = m;
  gyro->enabled = gyro_enabled; gyro->activate = gyro_activate;
  m->settings_menus = qa_ui_register_settings_menus(m->controller, m->all_bindings, m->all_binding_count, NULL, NULL, NULL, NULL);
  m->has_settings = true;
  register_slot(m, SLOT_SOUND, ID_SOUND, build_sound);
  register_slot(m, SLOT_SELECT, ID_SELECT, build_select);
  register_slot(m, SLOT_ROSTER, ID_ROSTER, build_roster);
  register_slot(m, SLOT_LOAD, ID_LOAD, build_load);
  qa_app_startup_menu_ensure_active_menu(m);
  return m;
}

qa_native_ui_controller *qa_app_startup_menu_controller(qa_app_startup_menu *m) { return m->controller; }
void qa_app_startup_menu_open_main(qa_app_startup_menu *m) { qa_native_ui_controller_open_menu(m->controller, ID_MAIN); }
void qa_app_startup_menu_open_local_lobby(qa_app_startup_menu *m) {
  if (m->lobby_menu) qa_native_ui_controller_open_menu(m->controller, m->lobby_menu);
}
bool qa_app_startup_menu_is_busy(const qa_app_startup_menu *m) { return m->busy; }
const char *qa_app_startup_menu_status(const qa_app_startup_menu *m) { return m->status ? m->status : ""; }
void qa_app_startup_menu_set_status(qa_app_startup_menu *m, const char *text, bool busy) { set_status(m, text, busy); }
qa_ui_menu_id qa_app_startup_menu_ensure_active_menu(qa_app_startup_menu *m) {
  qa_ui_menu_id active = qa_native_ui_controller_active_menu(m->controller);
  if (active) return active;
  qa_native_ui_controller_open_menu(m->controller, ID_MAIN);
  return ID_MAIN;
}
void qa_app_startup_menu_resume_display_options(qa_app_startup_menu *m) {
  qa_native_ui_controller_open_menu(m->controller, ID_OPTIONS);
  qa_native_ui_controller_open_menu(m->controller, ID_DISPLAY);
}
void qa_app_startup_menu_resume_server_browser(qa_app_startup_menu *m) {
  if (m->o.browser) qa_native_ui_controller_open_menu(m->controller, ID_BROWSER);
}
/* TS bindGyro / bindInput register again (the controller rejects a second registration of the same menu id
 * as the TS register throws); a replaced registration is disposed. */
void qa_app_startup_menu_bind_gyro(qa_app_startup_menu *m, const qa_ui_gyro_settings *settings) {
  qa_ui_settings_menus menus = qa_ui_register_gyro_settings_menu(m->controller, settings, NULL);
  if (m->has_gyro) qa_ui_settings_menus_dispose(&m->gyro_menus);
  m->gyro_menus = menus;
  m->gyro_menu = menus.root;
  m->has_gyro = true;
}
void qa_app_startup_menu_bind_input(qa_app_startup_menu *m, qa_seat_input *input,
    const qa_ui_binding_action *source, size_t source_count,
    const qa_ui_binding_action *(*get_source)(void *ctx, size_t *out_count), void *source_ctx,
    const qa_ui_binding_reset *reset) {
  qa_ui_settings_menus menus = qa_ui_register_binding_menus(m->controller, input, source, source_count, get_source, source_ctx, reset);
  if (m->has_bindings) qa_ui_settings_menus_dispose(&m->binding_menus);
  m->binding_menus = menus;
  m->binding_menu = menus.root;
  m->has_bindings = true;
}

/* ---- input ------------------------------------------------------------------------- */
typedef struct menu_input_call { qa_app_startup_menu *m; qa_seat_input_event event; bool handled; } menu_input_call;
static void menu_input_body(void *ctx) {
  menu_input_call *call = ctx;
  call->handled = qa_native_ui_controller_input(call->m->controller, call->event);
  qa_app_startup_menu_ensure_active_menu(call->m);
}
static double dropdown_scroll_limit(qa_app_startup_menu *m, double visible) {
  size_t count = 0;
  const qa_app_startup_selection_row *rows = safe_rows(m, &count);
  const qa_app_startup_selection_row *row = find_row(rows, count, m->dd.stage);
  size_t total = row ? dd_entries(m, row) : 0;
  return qa_js_max(0, (double)total - visible);
}
bool qa_app_startup_menu_input(qa_app_startup_menu *m, qa_seat_input_event event) {
  release_retired(m);
  if (m->busy) return true;
  qa_ui_menu_id active = qa_native_ui_controller_active_menu(m->controller);
  bool is_key = streq(event.kind, "key");
  if (streq(active, ID_SESSION) && m->dd.active) {
    if (is_key && event.down && event.code == QA_KEY_ESCAPE) {
      if (!retreat_stage(m)) { close_dropdown(m); m->preview_armed = false; }
      return true;
    }
    if (is_key && (event.code == QA_KEY_TAB || event.code == QA_KEY_UP || event.code == QA_KEY_DOWN
        || event.code == QA_KEY_LEFT || event.code == QA_KEY_RIGHT || event.code == QA_KEY_PAGE_UP
        || event.code == QA_KEY_PAGE_DOWN || event.code == QA_KEY_HOME || event.code == QA_KEY_END))
      m->preview_armed = true;
    if (is_key && event.down && (event.code == QA_KEY_PAGE_DOWN || event.code == QA_KEY_PAGE_UP)) {
      double visible = m->dd.has_edition ? 7 : 8;
      double limit = dropdown_scroll_limit(m, visible);
      prepare_builder_scroll(m, qa_js_max(0, qa_js_min(limit, m->dd.scroll + (event.code == QA_KEY_PAGE_DOWN ? visible : -visible))));
      return true;
    }
    if (streq(event.kind, "mouse-wheel") && event.delta.y != 0) {
      double visible = m->dd.has_edition ? 7 : 8;
      double limit = dropdown_scroll_limit(m, visible);
      prepare_builder_scroll(m, qa_js_max(0, qa_js_min(limit, m->dd.scroll + (event.delta.y < 0 ? 1 : -1))));
      return true;
    }
  }
  if (streq(event.kind, "mouse-wheel") && event.delta.y != 0 && streq(active, ID_LOAD)) {
    const qa_app_startup_save_list *saves = m->o.saves(m->o.ctx);
    m->save_page = qa_js_max(0, qa_js_min(ceil((double)saves->row_count / 5) - 1, m->save_page + (event.delta.y < 0 ? 1 : -1)));
    return true;
  }
  if (streq(active, ID_MAIN) && is_key && event.code == QA_KEY_ESCAPE) return true;
  menu_input_call call = {m, event, false};
  qa_caught_error caught;
  if (!qa_try(menu_input_body, &call, &caught)) {
    set_status(m, caught.message, false);
    return true;
  }
  return call.handled;
}

/* ---- draw -------------------------------------------------------------------------- */
typedef struct command_list { qa_ui_draw_command *items; size_t count, cap; } command_list;
static void list_push(command_list *list, qa_ui_draw_command command) {
  list->items = xgrow(list->items, &list->cap, list->count + 1, sizeof *list->items);
  list->items[list->count++] = command;
}
typedef struct draw_state {
  qa_app_startup_menu *m; scratch *sc;
  command_list commands, overlay; bool to_overlay;
  qa_ui_skin_colors colors;
} draw_state;
static qa_ui_draw_command text_command(qa_app_startup_menu *m, const char *value, double x, double y, double scale,
                                       qa_vec4 color, bool heading) {
  qa_ui_draw_command command;
  memset(&command, 0, sizeof command);
  command.kind = QA_UI_DRAW_TEXT;
  command.origin = (qa_vec2){x, y};
  command.text = value;
  command.font = heading ? qa_ui_menu_title_font : m->o.art->skin.font;
  command.scale = scale;
  command.color = color;
  command.align = "left";
  command.shadow = true;
  return command;
}
/* draw()'s text(value, x, y, scale, accent, heading). */
static void draw_text(draw_state *d, const char *value, double x, double y, double scale, bool accent, bool heading) {
  list_push(d->to_overlay ? &d->overlay : &d->commands,
            text_command(d->m, value, x, y, scale, accent ? d->colors.accent : d->colors.text, heading));
}
static qa_ui_draw_command fill_command(qa_rect rect, qa_vec4 color) {
  qa_ui_draw_command command;
  memset(&command, 0, sizeof command);
  command.kind = QA_UI_DRAW_FILL;
  command.rect = rect; command.color = color;
  return command;
}
typedef struct render_sink {
  qa_app_startup_menu *m;
  void (*emit)(void *ctx, const qa_render_command *command);
  void (*material)(void *ctx, const qa_material_text_draw *draw);
  void *ctx;
} render_sink;
/* picture: resource => characterTurntable?.picture(resource) ?? art.picture(resource) */
static qa_picture_asset sink_picture(void *ctx, qa_resource_id resource) {
  qa_app_startup_menu *m = ((render_sink *)ctx)->m;
  qa_picture_asset asset;
  memset(&asset, 0, sizeof asset);
  asset.kind = QA_PICTURE_IMAGE;
  const qa_image_picture *viewer = m->o.character_turntable
      ? qa_app_character_turntable_picture(m->o.character_turntable, resource) : NULL;
  asset.image = viewer ? *viewer : qa_native_ui_art_picture(m->o.art, resource);
  return asset;
}
static void sink_emit(void *ctx, const qa_render_command *command) {
  render_sink *sink = ctx;
  sink->emit(sink->ctx, command);
}
static void sink_material(void *ctx, const qa_material_text_draw *draw) {
  render_sink *sink = ctx;
  sink->material(sink->ctx, draw);
}
static qa_ui_render_services render_services(render_sink *sink) {
  qa_ui_render_services services;
  memset(&services, 0, sizeof services);
  services.ctx = sink;
  services.text = &sink->m->text;
  services.white.kind = QA_PICTURE_IMAGE;
  services.white.image = sink->m->o.art->white;
  services.picture = sink_picture;
  services.emit = sink_emit;
  services.material = sink_material;
  return services;
}
static const char *title_for(qa_app_startup_menu *m, qa_ui_menu_id active, scratch *sc) {
  if (streq(active, ID_MAIN)) return "QUAKE";
  if (streq(active, ID_SESSION)) return "Custom game";
  if (streq(active, ID_HOSTING)) return "Host a game";
  if (streq(active, ID_LIBRARY)) return "Library";
  if (streq(active, ID_FAMILY)) return "Play a game";
  if (streq(active, ID_CAMPAIGN))
    return streq(m->native_family, "q1") ? "Quake" : streq(m->native_family, "q2") ? "Quake II" : "Quake III Arena";
  if (streq(active, ID_DIFFICULTY)) return "Difficulty";
  if (streq(active, ID_ROSTER)) return "Custom roster";
  if (streq(active, ID_SELECT)) {
    const qa_app_startup_selection_row *row = selection_row(m);
    return row ? sc_str(sc, row->label) : "Choose";
  }
  if (streq(active, m->gyro_menu)) return "Gyro controls";
  if (streq(active, ID_BROWSER)) return "Find servers";
  if (streq(active, ID_BROWSER_DET)) return "Server details";
  if (streq(active, ID_BROWSER_OPTS)) return "Server filters";
  if (streq(active, ID_OPTIONS)) return "Options";
  if (streq(active, ID_DISPLAY)) return "Display";
  if (streq(active, ID_SOUND)) return "Sound";
  if (streq(active, ID_CONTROLS)) return "Controls";
  return "Load Game";
}
/* TS `!active?.startsWith(prefix)`: a null active passes. */
static bool not_prefixed(const char *active, const char *prefix) {
  return active == NULL || strncmp(active, prefix, strlen(prefix)) != 0;
}
static void draw_session(draw_state *d) {
  qa_app_startup_menu *m = d->m;
  scratch *sc = d->sc;
  builder_layout spec;
  builder_layout_build(sc, m, &spec);
  qa_seat_ui_state state = qa_native_ui_controller_state(m->controller);
  const char *focused_id = streq(state.focus.kind, "menu") ? state.focus.control : NULL;
  const qa_vec4 line = {0.6, 0.39, 0.18, 0.55}, dark = {0.02, 0.03, 0.04, 0.97};
  if (!spec.has_dropdown) {
    list_push(&d->commands, fill_command((qa_rect){320, 120, 1, 264}, (qa_vec4){0.6, 0.39, 0.18, 0.3}));
  } else {
    double x = spec.dropdown.x;
    list_push(&d->commands, fill_command((qa_rect){x - 8, 110, 264, 272}, dark));
    list_push(&d->commands, fill_command((qa_rect){x - 8, 110, 264, 1}, line));
    list_push(&d->commands, fill_command((qa_rect){x - 8, 381, 264, 1}, line));
    list_push(&d->commands, fill_command((qa_rect){x - 8, 110, 1, 272}, line));
    list_push(&d->commands, fill_command((qa_rect){x + 255, 110, 1, 272}, line));
    /* title.toUpperCase() (ASCII letters; the row labels are ASCII) */
    char *upper = sc_str(sc, spec.dropdown.title);
    for (char *p = upper; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
    draw_text(d, upper, x, 116, qa_ui_menu_text.section, true, false);
  }
  if (spec.has_viewer) {
    qa_rect panel = spec.viewer.panel;
    list_push(&d->commands, fill_command(panel, dark));
    list_push(&d->commands, fill_command((qa_rect){panel.x, panel.y, panel.width, 1}, line));
    list_push(&d->commands, fill_command((qa_rect){panel.x, panel.y + panel.height - 1, panel.width, 1}, line));
    list_push(&d->commands, fill_command((qa_rect){panel.x, panel.y, 1, panel.height}, line));
    list_push(&d->commands, fill_command((qa_rect){panel.x + panel.width - 1, panel.y, 1, panel.height}, line));
    qa_ui_draw_command image;
    memset(&image, 0, sizeof image);
    image.kind = QA_UI_DRAW_IMAGE;
    image.resource = spec.viewer.resource;
    image.rect = spec.viewer.rect;
    image.tex_coords[0] = (qa_vec2){0, 0}; image.tex_coords[1] = (qa_vec2){1, 1};
    image.color = (qa_vec4){1, 1, 1, 1};
    list_push(&d->commands, image);
    draw_text(d, "CHARACTER", panel.x + 8, panel.y + 6, qa_ui_menu_text.section, true, false);
    double label_scale = fit_scale(m, spec.viewer.label, 232, qa_ui_menu_text.small);
    draw_text(d, spec.viewer.label, panel.x + (panel.width - measure(m, spec.viewer.label, label_scale)) / 2, 344, label_scale,
              false, false);
  }
  for (size_t i = 0; i < spec.head_count; i++)
    draw_text(d, spec.heads[i].label, spec.heads[i].x, spec.heads[i].y, qa_ui_menu_text.section, true, false);
  d->to_overlay = true;
  for (size_t i = 0; i < spec.field_count; i++) {
    builder_field *field = &spec.fields[i];
    bool focused = streq(focused_id, field->control->id);
    draw_text(d, field->label, field->rect.x + 10, field->rect.y + 5, qa_ui_menu_text.field, focused, false);
    if (!field->has_value) continue;
    double scale = fit_scale(m, field->value_text, field->value_width, qa_ui_menu_text.field);
    draw_text(d, field->value_text, field->rect.x + field->rect.width - 10 - measure(m, field->value_text, scale),
              field->rect.y + 5, scale, focused, false);
  }
  if (spec.has_viewer) {
    qa_rect rect = spec.viewer.control->rect;
    list_push(&d->overlay, text_command(m, "Done", rect.x + 10, rect.y + 3, qa_ui_menu_text.field,
        streq(focused_id, spec.viewer.control->id) ? d->colors.accent : d->colors.text, false));
  }
  for (size_t i = 0; i < spec.footer_count; i++) {
    footer_entry *entry = &spec.footer[i];
    qa_vec4 color = streq(focused_id, entry->control->id) ? d->colors.accent
        : entry->control->enabled ? d->colors.text : d->colors.disabled;
    list_push(&d->overlay, text_command(m, entry->label, entry->rect.x + 10, entry->rect.y + 4, qa_ui_menu_text.field, color, false));
  }
  if (spec.has_dropdown) {
    builder_dropdown *dd = &spec.dropdown;
    if (dd->has_toggle) {
      const struct { qa_ui_control *control; bool selected; const char *label; } toggles[2] = {
        {dd->classic, dd->classic_selected, "Classic"}, {dd->rerelease, dd->rerelease_selected, "Rerelease"}};
      for (int i = 0; i < 2; i++) {
        qa_rect rect = toggles[i].control->rect;
        list_push(&d->overlay, text_command(m, toggles[i].label, rect.x + 10, rect.y + 3, qa_ui_menu_text.field,
            toggles[i].selected || streq(focused_id, toggles[i].control->id) ? d->colors.accent : d->colors.text, false));
      }
    }
    for (size_t i = 0; i < dd->item_count; i++) {
      builder_item *item = &dd->items[i];
      qa_rect rect = item->control->rect;
      double scale = dd->item_scale;
      const char *item_text = measure(m, item->label, scale) <= rect.width - 20 ? item->label
          : fit(sc, m, item->label, rect.width - 20, scale);
      qa_vec4 color = item->header || item->selected ? d->colors.accent : item->control->enabled ? d->colors.text : d->colors.disabled;
      list_push(&d->overlay, text_command(m, item_text, rect.x + 10 + (item->indent ? 16 : 0), rect.y + 5, scale, color, false));
    }
    if (dd->scroll_total > dd->scroll_visible) {
      double track_height = dd->scroll_visible * 27 - 1;
      double thumb_height = qa_js_max(24, (track_height * dd->scroll_visible) / dd->scroll_total);
      double thumb_y = dd->items_top + ((track_height - thumb_height) * dd->scroll_offset) / (dd->scroll_total - dd->scroll_visible);
      list_push(&d->overlay, fill_command((qa_rect){dd->x + 240, dd->items_top, 6, track_height}, (qa_vec4){0.6, 0.39, 0.18, 0.25}));
      list_push(&d->overlay, fill_command((qa_rect){dd->x + 240, thumb_y, 6, thumb_height}, (qa_vec4){0.6, 0.39, 0.18, 0.9}));
    }
  }
  d->to_overlay = false;
}
static void transform_all(command_list *list, qa_ui_transform transform) {
  for (size_t i = 0; i < list->count; i++) list->items[i] = qa_ui_transform_command(list->items[i], transform);
}
static void free_items(void *list) { free(((command_list *)list)->items); }
void qa_app_startup_menu_draw(qa_app_startup_menu *m, qa_ui_draw_context context,
    void (*emit)(void *ctx, const qa_render_command *command),
    void (*material)(void *ctx, const qa_material_text_draw *draw), void *ctx) {
  release_retired(m);
  sc_begin(&m->draw_sc, &m->retired);
  scratch *sc = &m->draw_sc;
  qa_ui_preference_values values = appearance(m);
  if (!qa_ui_text_renderer_bind(&m->text, m->o.art->skin.font, streq(values.typeface, "bold") ? m->o.title_font : m->o.font))
    qa_range_error("Out of memory");
  draw_state d;
  memset(&d, 0, sizeof d);
  d.m = m; d.sc = sc;
  qa_cleanup cleanup_commands, cleanup_overlay;
  qa_cleanup_push(&cleanup_commands, free_items, &d.commands);
  qa_cleanup_push(&cleanup_overlay, free_items, &d.overlay);
  d.colors = qa_ui_accessible_colors(qa_ui_menu_skin(m->o.art->skin.font).colors, values.high_contrast, values.color_mode);
  qa_ui_menu_id active = qa_native_ui_controller_active_menu(m->controller);
  qa_ui_transform transform = qa_ui_fit(context.binding.safe_area, values.menu_scale);
  qa_ui_draw_command backdrop = qa_ui_menu_backdrop(context);
  qa_ui_draw_command panel = qa_ui_menu_panel_scaled(context, streq(active, ID_MAIN), values.menu_scale);
  if (values.high_contrast && panel.kind == QA_UI_DRAW_FILL) panel.color = d.colors.panel;
  const char *title = title_for(m, active, sc);
  if (not_prefixed(active, "menu:settings:") && not_prefixed(active, "menu:library:") && not_prefixed(active, "menu:bindings:")
      && not_prefixed(active, "menu:mods:") && not_prefixed(active, "menu:local:"))
    draw_text(&d, title, 64, 44, streq(active, ID_MAIN) ? qa_ui_menu_text.main_title : qa_ui_menu_text.title, true, true);
  if (streq(active, m->binding_menu)) {
    char *line = sc_fmt(sc, "Player 1 - %s", selected_expectation(m)->title);
    draw_text(&d, fit(sc, m, line, 512, qa_ui_menu_text.small), 64, 443, qa_ui_menu_text.small, false, false);
  }
  const qa_app_startup_native_preset *native_preset = find_preset(m, m->native_preset_id);
  if (streq(active, ID_DIFFICULTY) && native_preset) {
    char *line = native_preset->family == QA_GAME_FAMILY_Q3 ? sc_str(sc, native_preset->choice.label)
        : sc_fmt(sc, "%s \xc2\xb7 %s", native_preset->choice.label, streq(native_preset->edition, "rerelease") ? "Rerelease" : "Classic");
    draw_text(&d, fit(sc, m, line, 512, qa_ui_menu_text.label), 64, 86, qa_ui_menu_text.label, false, false);
  }
  if (streq(active, ID_CAMPAIGN)) {
    size_t total = native_campaigns(m);
    for (size_t i = 0; i < total; i++)
      if (m->campaigns[i]->choice.unavailable) {
        if (m->campaigns[i]->choice.unavailable[0] != '\0')
          draw_text(&d, fit(sc, m, m->campaigns[i]->choice.unavailable, 512, qa_ui_menu_text.label), 64, 395, qa_ui_menu_text.label,
                    true, false);
        break;
      }
  }
  if (streq(active, ID_SELECT) && m->status[0] == '\0') {
    qa_seat_ui_state state = qa_native_ui_controller_state(m->controller);
    const char *focused = streq(state.focus.kind, "menu") ? state.focus.control : NULL;
    const qa_app_startup_selection_row *row = selection_row(m);
    for (size_t i = 0; row && focused && i < row->choice_count; i++) {
      char *id = sc_fmt(sc, "ui:startup:choice:%s", row->choices[i].id);
      if (!streq(focused, id)) continue;
      if (row->choices[i].unavailable && row->choices[i].unavailable[0] != '\0')
        draw_text(&d, fit(sc, m, row->choices[i].unavailable, 512, qa_ui_menu_text.small), 64, 442, qa_ui_menu_text.small, true, false);
      break;
    }
  }
  if (streq(active, ID_ROSTER)) draw_text(&d, "Map counts shown. * Custom override.", 64, 442, qa_ui_menu_text.small, false, false);
  list_push(&d.commands, fill_command((qa_rect){64, 104, streq(active, ID_MAIN) ? 224 : 512, 1}, (qa_vec4){0.6, 0.39, 0.18, 0.65}));
  if (streq(active, ID_SESSION)) draw_session(&d);
  if (streq(active, ID_BROWSER) && m->o.browser && m->status[0] == '\0') {
    qa_app_startup_server_browser *browser = m->o.browser;
    size_t count = 0;
    const qa_browser_entry **rows = qa_app_startup_server_browser_rows(browser, &count);
    const char *key = qa_app_startup_server_browser_selected(browser);
    const qa_browser_entry *selected = NULL;
    for (size_t i = 0; i < count && !selected; i++) {
      char *address = qa_net_address_key(&rows[i]->address, true);
      if (streq(address, key)) selected = rows[i];
      free(address);
    }
    if (count == 0) draw_text(&d, sc_str(sc, qa_app_startup_server_browser_empty_message(browser)), 64, 432, qa_ui_menu_text.small, false, false);
    else {
      const char *status = qa_app_startup_server_browser_status(browser);
      char *line = selected == NULL || !selected->has_status ? sc_str(sc, status)
          : sc_fmt(sc, "%s \xe2\x80\x94 %s", selected->status.map, status);
      draw_text(&d, fit(sc, m, line, 512, qa_ui_menu_text.label), 64, 432, qa_ui_menu_text.label, false, false);
    }
  }
  if (streq(active, ID_LOAD)) {
    const qa_app_startup_save_list *saves = m->o.saves(m->o.ctx);
    if (saves->row_count == 0) {
      draw_text(&d, "No saved games", 64, 138, qa_ui_menu_text.body, false, false);
      draw_text(&d, "Your saved games will appear here.", 64, 174, qa_ui_menu_text.label, false, false);
    }
    d.to_overlay = true;
    qa_seat_ui_state state = qa_native_ui_controller_state(m->controller);
    size_t start = (size_t)(m->save_page * 5);
    for (size_t i = start; i < saves->row_count && i < start + 5; i++) {
      const qa_app_startup_save_row *save = &saves->rows[i];
      size_t index = i - start;
      char *line = sc_fmt(sc, "%s  -  %s", save->game, locale_string(sc, save->saved_at_milliseconds));
      draw_text(&d, fit(sc, m, line, 486, qa_ui_menu_text.small), 74, 144 + (double)index * 46, qa_ui_menu_text.small, false, false);
      if (save->unavailable && qa_ui_contains((qa_rect){64, 118 + (double)index * 46, 512, 42}, state.cursor))
        draw_text(&d, fit(sc, m, save->unavailable, 512, qa_ui_menu_text.small), 64, 360, qa_ui_menu_text.small, true, false);
    }
    if (saves->error) draw_text(&d, fit(sc, m, saves->error, 512, qa_ui_menu_text.small), 64, 390, qa_ui_menu_text.small, false, false);
  }
  d.to_overlay = false;
  if (m->status[0] != '\0') draw_text(&d, fit(sc, m, m->status, 512, qa_ui_menu_text.small), 64, 442, qa_ui_menu_text.small, true, false);
  else if (streq(active, ID_SESSION)) {
    builder_layout spec;
    builder_layout_build(sc, m, &spec);
    qa_vec2 cursor = qa_native_ui_controller_state(m->controller).cursor;
    for (size_t i = 0; spec.has_dropdown && i < spec.dropdown.item_count; i++) {
      builder_item *item = &spec.dropdown.items[i];
      if (!qa_ui_contains(item->control->rect, cursor)) continue;
      if (item->unavailable && item->unavailable[0] != '\0')
        draw_text(&d, fit(sc, m, item->unavailable, 512, qa_ui_menu_text.small), 64, 442, qa_ui_menu_text.small, true, false);
      break;
    }
  }
  render_sink sink = {m, emit, material, ctx};
  qa_ui_render_services services = render_services(&sink);
  /* renderUiCommands(context, [backdrop, panel, ...commands.map(transform)]) */
  transform_all(&d.commands, transform);
  list_push(&d.commands, backdrop);
  list_push(&d.commands, panel);
  memmove(d.commands.items + 2, d.commands.items, (d.commands.count - 2) * sizeof *d.commands.items);
  d.commands.items[0] = backdrop;
  d.commands.items[1] = panel;
  qa_ui_render_commands(context, d.commands.items, d.commands.count, &services);
  size_t controller_count = 0;
  const qa_ui_draw_command *controller_commands = qa_native_ui_controller_draw(m->controller, context, &controller_count);
  qa_ui_render_commands(context, controller_commands, controller_count, &services);
  transform_all(&d.overlay, transform);
  qa_ui_render_commands(context, d.overlay.items, d.overlay.count, &services);
  qa_cleanup_pop(&cleanup_overlay);
  qa_cleanup_pop(&cleanup_commands);
  free(d.commands.items); free(d.overlay.items);
}

/* ---- captionCommands --------------------------------------------------------------- */
static void caption_emit(void *ctx, const qa_render_command *command) {
  qa_app_startup_menu *m = ctx;
  m->captions = xgrow(m->captions, &m->caption_cap, m->caption_count + 1, sizeof *m->captions);
  m->captions[m->caption_count++] = *command;
}
static void caption_material(void *ctx, const qa_material_text_draw *draw) {
  (void)ctx; (void)draw; /* material: () => { throw } */
  qa_raise("Error", 0, 0, "Caption text requested a material draw");
}
static double caption_measure(void *ctx, const char *text, double scale) { return measure(ctx, text, scale); }
static void free_captions(void *draws) { qa_ui_caption_commands_free(draws); }
const qa_render_command *qa_app_startup_menu_caption_commands(qa_app_startup_menu *m,
    const qa_active_caption *captions, size_t caption_count, qa_ui_draw_context context, size_t *out_count) {
  m->caption_count = 0;
  *out_count = 0;
  qa_ui_preference_values values = appearance(m);
  if (!values.captions) return m->captions;
  if (!qa_ui_text_renderer_bind(&m->text, m->o.art->skin.font, streq(values.typeface, "bold") ? m->o.title_font : m->o.font))
    qa_range_error("Out of memory");
  qa_rect area = context.binding.safe_area;
  qa_rect region = {area.x + 8, area.y + area.height * 0.65, area.width - 16, area.height * 0.3};
  double scale = values.text_scale * qa_js_min(area.width / 640, area.height / 480);
  qa_ui_caption_commands draws;
  if (!qa_ui_get_caption_commands(captions, caption_count, region, m->o.art->skin.font, scale, caption_measure, m, 0, 8, &draws))
    qa_range_error("Out of memory");
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free_captions, &draws);
  render_sink sink = {m, caption_emit, caption_material, m};
  qa_ui_render_services services = render_services(&sink);
  qa_ui_render_commands(context, draws.commands, draws.count, &services);
  qa_cleanup_pop(&cleanup);
  qa_ui_caption_commands_free(&draws);
  *out_count = m->caption_count;
  return m->captions;
}

/* ---- close / destroy --------------------------------------------------------------- */
/* TS close(): closeAll, every disposer in registration order, text.clear(). */
static void dispose_class(qa_app_startup_menu *m) {
  if (m->disposed) return;
  m->disposed = true;
  qa_native_ui_controller_close_all(m->controller);
  if (m->slots[SLOT_MAIN].handle >= 0) qa_native_ui_controller_unregister(m->controller, m->slots[SLOT_MAIN].handle);
  for (size_t i = 0; i < m->library_page_count; i++) qa_ui_settings_menus_dispose(&m->library_pages[i].menus);
  static const int before_lobby[] = {SLOT_LIBRARY, SLOT_FAMILY};
  for (size_t i = 0; i < 2; i++) qa_native_ui_controller_unregister(m->controller, m->slots[before_lobby[i]].handle);
  qa_app_arena_selection_menu_destroy(m->arena_menu);
  m->arena_menu = NULL;
  static const int browser_slots[] = {SLOT_CAMPAIGN, SLOT_DIFFICULTY, SLOT_BROWSER, SLOT_BROWSER_OPTS, SLOT_BROWSER_DET};
  for (size_t i = 0; i < 5; i++) qa_native_ui_controller_unregister(m->controller, m->slots[browser_slots[i]].handle);
  if (m->has_lobby_menus) qa_ui_settings_menus_dispose(&m->lobby_menus);
  if (m->has_mods) qa_ui_settings_menus_dispose(&m->mods_menus);
  qa_native_ui_controller_unregister(m->controller, m->slots[SLOT_SESSION].handle);
  qa_native_ui_controller_unregister(m->controller, m->slots[SLOT_HOSTING].handle);
  if (m->has_llm) qa_ui_settings_menus_dispose(&m->llm_menus);
  qa_native_ui_controller_unregister(m->controller, m->slots[SLOT_OPTIONS].handle);
  if (m->has_settings) qa_ui_settings_menus_dispose(&m->settings_menus);
  static const int last_slots[] = {SLOT_SOUND, SLOT_SELECT, SLOT_ROSTER, SLOT_LOAD};
  for (size_t i = 0; i < 4; i++) qa_native_ui_controller_unregister(m->controller, m->slots[last_slots[i]].handle);
  if (m->has_gyro) qa_ui_settings_menus_dispose(&m->gyro_menus);
  if (m->has_bindings) qa_ui_settings_menus_dispose(&m->binding_menus);
  for (int i = 0; i < SLOT_COUNT; i++) m->slots[i].handle = -1;
  qa_ui_text_renderer_clear(&m->text);
}
void qa_app_startup_menu_close(qa_app_startup_menu *m) { dispose_class(m); }
void qa_app_startup_menu_destroy(qa_app_startup_menu *m) {
  if (!m) return;
  dispose_class(m);
  qa_native_ui_controller_destroy(m->controller);
  qa_ui_text_renderer_destroy(&m->text);
  for (int i = 0; i < SLOT_COUNT; i++) sc_destroy(&m->slots[i].sc);
  sc_destroy(&m->draw_sc);
  pool_destroy(&m->retired);
  dd_clear_collapsed(&m->dd);
  free(m->status); free(m->monster_classname); free(m->native_preset_id);
  free(m->native_arena); free(m->campaigns); free(m->entries); free(m->all_bindings); free(m->captions);
  free(m);
}
