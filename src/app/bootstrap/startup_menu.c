/* Port of src/app/bootstrap/startup-menu.ts: the StartupMenu class
 * body over the landed NativeUiController. Menu factories are rebuilt
 * on every controller read exactly as the TS factory closures are;
 * per-menu scratch owns the returned controls/strings until that
 * menu's next factory call (the controller header's factory rule).
 * Async model preparations run synchronously: the TS setStatus(busy)
 * window collapses, final status/navigation outcomes are identical.
 * TS input() catches controller errors into status; the C controller
 * aborts with the identical message (qa_range_error convention), so
 * that catch has no counterpart. draw()/captionCommands() and the
 * character-turntable viewer are not ported here (see NOTES).
 * The pre-existing host-delegation API above the options section is
 * kept unchanged for its callers. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/startup_menu.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/key_codes.h"
#include "qa/network/common/endpoint.h"
#include "qa/ui/common/layout.h"
#include "qa/ui/common/menu_theme.h"
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
       ACT_BUILDER_STAGE_BACK, ACT_BUILDER_EDITION, ACT_INLINE_SELECT,
       ACT_FOOTER_HOSTING, ACT_FOOTER_MODS, ACT_FOOTER_BROWSE, ACT_FOOTER_LOBBY,
       ACT_BROWSER_PROTOCOL, ACT_BROWSER_ADDRESS, ACT_BROWSER_QUERY,
       ACT_BROWSER_SCAN, ACT_BROWSER_FAVORITE, ACT_BROWSER_FILTER,
       ACT_BROWSER_ENTRY, ACT_BROWSER_PAGE, ACT_BROWSER_FAV_ONLY,
       ACT_BROWSER_CONNECT, ACT_BROWSER_DETAILS, ACT_BROWSER_SORT,
       ACT_BROWSER_HIDE_EMPTY, ACT_BROWSER_HIDE_FULL, ACT_BROWSER_MASTER,
       ACT_BROWSER_DISCOVER, ACT_BROWSER_DETAIL_PAGE, ACT_NOOP };

typedef struct action {
  qa_app_startup_menu *m; int kind; const char *a; const char *b;
} action;

typedef struct scratch {
  qa_ui_control *controls; size_t count, cap;
  char **strings; size_t scount, scap;
  action *actions; size_t acount, acap;
  qa_ui_choice *choices; size_t chcount, chcap;
  const qa_ui_control_id *scroll_ids; size_t scroll_cap;
  char (*bufs)[64]; size_t bufcount, bufcap;
  void **allocs; size_t alloc_count, alloc_cap; /* per-control owned blocks */
  qa_ui_menu_scroll scroll;
} scratch;

typedef struct slot { qa_app_startup_menu *m; int index; int handle; scratch sc; } slot;

typedef struct entry {
  bool header; const char *group; bool expanded;
  const qa_app_startup_menu_choice *choice;
} entry;

struct qa_app_startup_menu {
  /* Legacy host-delegation mode (pre-existing API). */
  qa_app_startup_menu_host host;
  bool legacy, in_game, closed, failed;
  /* TS StartupMenu class state. */
  qa_app_startup_menu_options o;
  qa_native_ui_controller *controller;
  qa_ui_skin skin;
  slot slots[SLOT_COUNT];
  char *status; bool busy;
  char field[32]; double page;
  int monster_kind; /* 0 none, 1 source, 2 class */
  char *monster_classname;
  double save_page, campaign_page;
  char native_family[4], native_edition[16], native_skill[32];
  char *native_preset_id, *native_arena;
  qa_app_startup_menu_hosting host_draft; char host_kind[24]; char host_port[16];
  bool dropdown_active, preview_armed;
  char dd_field[32], dd_stage[32], dd_edition[16]; bool dd_has_edition;
  double dd_scroll; char **dd_collapsed; size_t dd_collapsed_count;
  qa_ui_menu_id gyro_menu, binding_menu, lobby_menu, mods_menu, llm_root;
  struct { const char *name; const char *label; qa_ui_menu_id root;
           qa_ui_settings_menus menus; bool has; } library_pages[6];
  size_t library_page_count;
  qa_app_arena_selection_menu *arena_menu;
  qa_ui_settings_menus settings_menus, mods_menus, lobby_menus, gyro_menus, binding_menus;
  qa_ui_llm_settings_menus llm_menus;
  bool has_settings, has_mods, has_lobby_menus, has_gyro, has_bindings, has_llm, disposed;
  qa_ui_setting_binding synthetic[2];
  qa_ui_setting_binding *all_bindings; size_t all_binding_count;
  /* Rebuilt-per-call scratch views. */
  const qa_app_startup_menu_preset **campaigns; size_t campaign_cap;
  entry *entries; size_t entry_cap;
  qa_app_startup_menu_row sel_row; bool sel_has;
  char browser_sort[32]; /* TS browser.sortOrder mirror (no C getter) */
};

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
static void set_str(char **dst, const char *src) { free(*dst); *dst = src ? xdup(src) : NULL; }
static void set_fixed(char *dst, size_t size, const char *src) {
  snprintf(dst, size, "%s", src ? src : "");
}
static bool streq(const char *a, const char *b) {
  if (a == b) return true;
  if (!a || !b) return false;
  return strcmp(a, b) == 0;
}

/* Scratch is a pool freed only at destroy: the controller keeps
 * menu ids passed to open_menu (and focus control ids) borrowed
 * across factory rebuilds, so per-rebuild frees would dangle. The
 * controls array itself is reused, per the controller factory rule
 * (valid until the next factory call). */
static void sc_reset(scratch *sc) {
  sc->count = 0; sc->bufcount = 0;
}
static void sc_free(scratch *sc) {
  for (size_t i = 0; i < sc->scount; i++) free(sc->strings[i]);
  for (size_t i = 0; i < sc->alloc_count; i++) free(sc->allocs[i]);
  free(sc->controls); free(sc->strings); free(sc->actions);
  free(sc->choices); free((void *)sc->scroll_ids); free(sc->bufs); free(sc->allocs);
  memset(sc, 0, sizeof *sc);
}
static void *sc_alloc(scratch *sc, size_t size) {
  void *block = calloc(1, size ? size : 1);
  if (!block) qa_range_error("Out of memory");
  sc->allocs = xgrow(sc->allocs, &sc->alloc_cap, sc->alloc_count + 1, sizeof *sc->allocs);
  sc->allocs[sc->alloc_count++] = block;
  return block;
}
static char *sc_str(scratch *sc, const char *s) {
  sc->strings = xgrow(sc->strings, &sc->scap, sc->scount + 1, sizeof *sc->strings);
  char *copy = xdup(s ? s : "");
  sc->strings[sc->scount++] = copy;
  return copy;
}
static char *sc_fmt(scratch *sc, const char *format, ...) {
  char buffer[2048];
  va_list args; va_start(args, format);
  vsnprintf(buffer, sizeof buffer, format, args);
  va_end(args);
  return sc_str(sc, buffer);
}
static action *sc_action(scratch *sc, qa_app_startup_menu *m, int kind, const char *a, const char *b) {
  action *act = sc_alloc(sc, sizeof *act); /* stable: controls keep ctx across later pushes */
  *act = (action){m, kind, a, b};
  return act;
}
static qa_ui_control *sc_push(scratch *sc) {
  sc->controls = xgrow(sc->controls, &sc->cap, sc->count + 1, sizeof *sc->controls);
  qa_ui_control *control = &sc->controls[sc->count++];
  memset(control, 0, sizeof *control);
  return control;
}
static char *sc_buf(scratch *sc) {
  sc->bufs = xgrow(sc->bufs, &sc->bufcap, sc->bufcount + 1, sizeof *sc->bufs);
  return sc->bufs[sc->bufcount++];
}

static double measure(qa_app_startup_menu *m, const char *text, double scale) {
  if (m->o.measure) return m->o.measure(m->o.ctx, text, scale);
  return (double)strlen(text) * scale; /* no font host supplied */
}
/* TS fit(): byte truncation (TS counts UTF-16 units; differs only for non-ASCII). */
static char *fit(scratch *sc, qa_app_startup_menu *m, const char *text, double width, double scale) {
  if (measure(m, text, scale) <= width) return sc_str(sc, text);
  size_t end = strlen(text);
  char *candidate = xdup(text);
  for (;;) {
    if (end == 0) break;
    candidate[end] = '\0';
    char *trial = NULL;
    size_t len = end + 4;
    trial = malloc(len);
    if (!trial) qa_range_error("Out of memory");
    snprintf(trial, len, "%s...", candidate);
    bool fits = measure(m, trial, scale) <= width;
    free(trial);
    if (fits) break;
    end--;
  }
  candidate[end] = '\0';
  char *out = sc_fmt(sc, "%s...", candidate);
  free(candidate);
  return out;
}
static double fit_scale(qa_app_startup_menu *m, const char *value, double width, double maximum) {
  if (measure(m, value, maximum) <= width) return maximum;
  double low = 1.2, high = maximum;
  for (int i = 0; i < 12; i++) {
    double middle = (low + high) / 2;
    if (measure(m, value, middle) <= width) low = middle; else high = middle;
  }
  return low;
}

static const qa_app_startup_menu_row *safe_rows(qa_app_startup_menu *m, size_t *out_count) {
  *out_count = 0;
  if (!m->o.rows) return NULL;
  return m->o.rows(m->o.ctx, out_count); /* NULL = TS throw -> [] */
}
static const qa_app_startup_menu_row *find_row(const qa_app_startup_menu_row *rows, size_t count, const char *field) {
  if (!rows) return NULL;
  for (size_t i = 0; i < count; i++) if (streq(rows[i].field, field)) return &rows[i];
  return NULL;
}
static const qa_app_startup_menu_preset *find_preset(qa_app_startup_menu *m, const char *id) {
  size_t count = 0;
  const qa_app_startup_menu_preset *presets = m->o.presets ? m->o.presets(m->o.ctx, &count) : NULL;
  if (!presets || !id) return NULL;
  for (size_t i = 0; i < count; i++) if (streq(presets[i].id, id)) return &presets[i];
  return NULL;
}
static void set_status(qa_app_startup_menu *m, const char *text, bool busy) {
  set_str(&m->status, text ? text : "");
  m->busy = busy;
}

/* TS nativeCampaigns(): unified by title, edition picks the variant. */
static size_t native_campaigns(qa_app_startup_menu *m) {
  size_t count = 0;
  const qa_app_startup_menu_preset *presets = m->o.presets ? m->o.presets(m->o.ctx, &count) : NULL;
  size_t out = 0;
  if (!presets) return 0;
  for (size_t i = 0; i < count; i++) {
    if (!streq(presets[i].family, m->native_family)) continue;
    bool seen = false;
    for (size_t j = 0; j < i; j++)
      if (streq(presets[j].family, m->native_family) && streq(presets[j].label, presets[i].label)) seen = true;
    if (seen) continue;
    const qa_app_startup_menu_preset *picked = &presets[i];
    for (size_t j = 0; j < count; j++)
      if (streq(presets[j].family, m->native_family) && streq(presets[j].label, presets[i].label)
          && streq(presets[j].edition, m->native_edition)) { picked = &presets[j]; break; }
    m->campaigns = xgrow(m->campaigns, &m->campaign_cap, out + 1, sizeof *m->campaigns);
    m->campaigns[out++] = picked;
  }
  return out;
}

static void variant_key(const qa_app_startup_menu_choice *choice, char *buffer, size_t size) {
  if (!choice->edition) snprintf(buffer, size, "%s", choice->id);
  else snprintf(buffer, size, "%s|%s", choice->group ? choice->group : "", choice->label);
}
static const qa_app_startup_menu_choice *edition_variant(const qa_app_startup_menu_row *row,
    const qa_app_startup_menu_choice *choice, const char *edition) {
  char key[1024]; variant_key(choice, key, sizeof key);
  for (size_t i = 0; i < row->choice_count; i++) {
    char other[1024]; variant_key(&row->choices[i], other, sizeof other);
    if (streq(row->choices[i].edition, edition) && strcmp(other, key) == 0) return &row->choices[i];
  }
  return NULL;
}
static bool collapsed_has(qa_app_startup_menu *m, const char *group) {
  for (size_t i = 0; i < m->dd_collapsed_count; i++) if (streq(m->dd_collapsed[i], group)) return true;
  return false;
}
/* TS dropdownEntries(): unified edition list + group headers. */
static size_t dropdown_entries(qa_app_startup_menu *m, const qa_app_startup_menu_row *row,
                               const char *edition, bool use_collapsed_state,
                               char *const *collapsed, size_t collapsed_count) {
  const qa_app_startup_menu_choice **visible = NULL;
  size_t visible_count = 0, visible_cap = 0, out = 0;
  for (size_t i = 0; i < row->choice_count; i++) {
    const qa_app_startup_menu_choice *choice = &row->choices[i];
    if (!choice->edition) {
      visible = xgrow(visible, &visible_cap, visible_count + 1, sizeof *visible);
      visible[visible_count++] = choice;
      continue;
    }
    char key[1024]; variant_key(choice, key, sizeof key);
    size_t existing = (size_t)-1;
    for (size_t j = 0; j < visible_count; j++) {
      char other[1024]; variant_key(visible[j], other, sizeof other);
      if (strcmp(other, key) == 0) existing = j;
    }
    if (existing == (size_t)-1) {
      visible = xgrow(visible, &visible_cap, visible_count + 1, sizeof *visible);
      visible[visible_count++] = choice;
    } else if (edition && streq(choice->edition, edition)) visible[existing] = choice;
  }
  for (size_t i = 0; i < visible_count; i++) {
    if (visible[i]->group) continue;
    m->entries = xgrow(m->entries, &m->entry_cap, out + 1, sizeof *m->entries);
    m->entries[out++] = (entry){false, NULL, false, visible[i]};
  }
  for (size_t i = 0; i < visible_count; i++) {
    const char *group = visible[i]->group;
    if (!group) continue;
    bool listed = false;
    for (size_t j = 0; j < i; j++) if (streq(visible[j]->group, group)) listed = true;
    if (listed) continue;
    bool is_collapsed = false;
    if (use_collapsed_state) is_collapsed = collapsed_has(m, group);
    else for (size_t j = 0; j < collapsed_count; j++) if (streq(collapsed[j], group)) is_collapsed = true;
    m->entries = xgrow(m->entries, &m->entry_cap, out + 1, sizeof *m->entries);
    m->entries[out++] = (entry){true, group, !is_collapsed, NULL};
    if (!is_collapsed)
      for (size_t j = 0; j < visible_count; j++) if (streq(visible[j]->group, group)) {
        m->entries = xgrow(m->entries, &m->entry_cap, out + 1, sizeof *m->entries);
        m->entries[out++] = (entry){false, NULL, false, visible[j]};
      }
  }
  free(visible);
  return out;
}
static void dd_clear_collapsed(qa_app_startup_menu *m) {
  for (size_t i = 0; i < m->dd_collapsed_count; i++) free(m->dd_collapsed[i]);
  free(m->dd_collapsed); m->dd_collapsed = NULL; m->dd_collapsed_count = 0;
}
/* TS dropdownOpenState(row). */
static void dropdown_open_state(qa_app_startup_menu *m, const qa_app_startup_menu_row *row) {
  const qa_app_startup_menu_choice *current = NULL;
  bool has_classic = false, has_rerelease = false;
  for (size_t i = 0; i < row->choice_count; i++) {
    if (streq(row->choices[i].id, row->value)) current = &row->choices[i];
    if (streq(row->choices[i].edition, "classic")) has_classic = true;
    if (streq(row->choices[i].edition, "rerelease")) has_rerelease = true;
  }
  int edition_count = (has_classic ? 1 : 0) + (has_rerelease ? 1 : 0);
  m->dd_has_edition = edition_count > 1;
  set_fixed(m->dd_edition, sizeof m->dd_edition,
            m->dd_has_edition ? (current && current->edition ? current->edition : "classic") : "");
  dd_clear_collapsed(m);
  for (size_t i = 0; i < row->choice_count; i++) {
    const char *group = row->choices[i].group;
    if (!group || streq(group, current ? current->group : NULL)) continue;
    if (collapsed_has(m, group)) continue;
    char **grown = realloc(m->dd_collapsed, (m->dd_collapsed_count + 1) * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    m->dd_collapsed = grown;
    m->dd_collapsed[m->dd_collapsed_count++] = xdup(group);
  }
  size_t count = dropdown_entries(m, row, m->dd_has_edition ? m->dd_edition : NULL, true, NULL, 0);
  double visible_count = m->dd_has_edition ? 7 : 8;
  long selected = 0;
  for (size_t i = 0; i < count; i++)
    if (!m->entries[i].header && streq(m->entries[i].choice->id, row->value)) { selected = (long)i; break; }
  long header = -1;
  for (long i = selected - 1; i >= 0; i--) if (m->entries[i].header) { header = i; break; }
  long anchored = header >= 0 && (double)(selected - header) < visible_count ? header : selected;
  double max_scroll = (double)count - visible_count; if (max_scroll < 0) max_scroll = 0;
  double scroll = (double)anchored; if (scroll > max_scroll) scroll = max_scroll; if (scroll < 0) scroll = 0;
  m->dd_scroll = scroll;
}
static const char *const *stage_chain(const char *field, size_t *out_count) {
  static const char *const map_chain[] = {"mapProduct", "map"};
  static const char *const char_chain[] = {"character", "model", "skin", "topColor", "bottomColor"};
  if (streq(field, "mapProduct")) { *out_count = 2; return map_chain; }
  if (streq(field, "character")) { *out_count = 5; return char_chain; }
  *out_count = 0; return NULL; /* single-field chain handled by caller */
}
static void prepare_builder_scroll(qa_app_startup_menu *m, double scroll) {
  if (!m->dropdown_active) return;
  m->dd_scroll = scroll;
  if (!streq(m->dd_stage, "map") || !m->o.prepare_map_choices) return;
  bool prepared = false;
  set_status(m, "Checking map rules...", true);
  const char *error = m->o.prepare_map_choices(m->o.ctx, scroll, 8, &prepared);
  if (!prepared) { /* TS: status stays until the promise settles; no promise here. */
    set_status(m, m->status, true); return; }
  set_status(m, error ? error : "", false);
}
static bool advance_stage(qa_app_startup_menu *m) {
  if (!m->dropdown_active) return false;
  size_t chain_count = 0;
  const char *const *chain = stage_chain(m->dd_field, &chain_count);
  const char *single = m->dd_field;
  if (!chain) { chain = &single; chain_count = 1; }
  size_t at = (size_t)-1;
  for (size_t i = 0; i < chain_count; i++) if (streq(chain[i], m->dd_stage)) at = i;
  if (at == (size_t)-1) return false;
  size_t count = 0;
  const qa_app_startup_menu_row *rows = safe_rows(m, &count);
  for (size_t i = at + 1; i < chain_count; i++) {
    const qa_app_startup_menu_row *row = find_row(rows, count, chain[i]);
    if (!row) continue;
    set_fixed(m->dd_stage, sizeof m->dd_stage, chain[i]);
    dropdown_open_state(m, row);
    if (streq(chain[i], "map")) prepare_builder_scroll(m, m->dd_scroll);
    return true;
  }
  return false;
}
static bool retreat_stage(qa_app_startup_menu *m) {
  if (!m->dropdown_active || streq(m->dd_stage, m->dd_field)) return false;
  size_t chain_count = 0;
  const char *const *chain = stage_chain(m->dd_field, &chain_count);
  if (!chain) return false;
  size_t at = (size_t)-1;
  for (size_t i = 0; i < chain_count; i++) if (streq(chain[i], m->dd_stage)) at = i;
  if (at == (size_t)-1 || at == 0) return false;
  size_t count = 0;
  const qa_app_startup_menu_row *rows = safe_rows(m, &count);
  for (size_t i = at; i-- > 0;) {
    const qa_app_startup_menu_row *row = find_row(rows, count, chain[i]);
    if (!row) continue;
    set_fixed(m->dd_stage, sizeof m->dd_stage, chain[i]);
    dropdown_open_state(m, row);
    return true;
  }
  return false;
}
static void prepare_choice_page(qa_app_startup_menu *m, double page) {
  m->page = page;
  if (m->monster_kind != 0 || !streq(m->field, "map") || !m->o.prepare_map_choices) return;
  bool prepared = false;
  set_status(m, "Checking map rules...", true);
  const char *error = m->o.prepare_map_choices(m->o.ctx, page * 7, 7, &prepared);
  if (!prepared) return;
  set_status(m, error ? error : "", false);
}
static void open_roster(qa_app_startup_menu *m) {
  set_status(m, "Reading this map's monster roster...", true);
  const char *error = m->o.prepare_monster_roster ? m->o.prepare_monster_roster(m->o.ctx) : NULL;
  if (error) { set_status(m, error, false); return; }
  set_status(m, "", false);
  qa_native_ui_controller_open_menu(m->controller, ID_ROSTER);
}
/* TS selectionRow(). */
static const qa_app_startup_menu_row *selection_row(qa_app_startup_menu *m) {
  m->sel_has = false;
  if (m->monster_kind == 1) {
    if (!m->o.monster_source_row) return NULL;
    m->sel_row = m->o.monster_source_row(m->o.ctx);
    m->sel_has = true; return &m->sel_row;
  }
  if (m->monster_kind == 2) {
    size_t count = 0;
    const qa_app_startup_menu_monster_row *rows =
        m->o.monster_roster_rows ? m->o.monster_roster_rows(m->o.ctx, &count) : NULL;
    if (!rows) return NULL;
    for (size_t i = 0; i < count; i++) if (streq(rows[i].classname, m->monster_classname)) {
      m->sel_row = (qa_app_startup_menu_row){rows[i].classname, rows[i].label, rows[i].value,
                                             rows[i].choices, rows[i].choice_count};
      m->sel_has = true; return &m->sel_row;
    }
    return NULL;
  }
  size_t count = 0;
  const qa_app_startup_menu_row *rows = safe_rows(m, &count);
  const qa_app_startup_menu_row *row = find_row(rows, count, m->field);
  if (!row) return NULL;
  m->sel_row = *row; m->sel_has = true;
  return &m->sel_row;
}

static void on_activate(void *ctx, qa_seat_id seat);
static void on_select(void *ctx, qa_seat_id seat, const char *choice);
static void on_checked(void *ctx, qa_seat_id seat, bool checked);
static void on_change_text(void *ctx, qa_seat_id seat, const char *text);
static void on_submit_text(void *ctx, qa_seat_id seat, const char *text);

/* TS button(id, label, row, activate, wide). */
static qa_ui_control *button(scratch *sc, qa_app_startup_menu *m, const char *id,
                              const char *label, double row, bool wide,
                              int kind, const char *a, const char *b) {
  qa_ui_control *c = sc_push(sc);
  c->id = sc_fmt(sc, "ui:startup:%s", id);
  c->kind = "button";
  c->label = sc_str(sc, label);
  c->rect = (qa_rect){64, 114 + row * 33, wide ? 512 : 224, 29};
  c->visible = true;
  c->enabled = !m->busy;
  c->ctx = sc_action(sc, m, kind, a ? sc_str(sc, a) : NULL, b ? sc_str(sc, b) : NULL);
  c->activate_seat = on_activate;
  return c;
}
static qa_ui_control *back_button(scratch *sc, qa_app_startup_menu *m) {
  return button(sc, m, "back", "Back", 9, true, ACT_CLOSE_MENU, NULL, NULL);
}
static qa_ui_control *choice_control(scratch *sc, qa_app_startup_menu *m, const char *id,
                                     const char *label, qa_rect rect, bool enabled, bool visible,
                                     const qa_ui_choice *choices, size_t choice_count,
                                     const char *selected, int kind, const char *a) {
  qa_ui_control *c = sc_push(sc);
  c->id = sc_str(sc, id);
  c->kind = "choice";
  c->label = sc_str(sc, label);
  c->rect = rect; c->visible = visible; c->enabled = enabled;
  qa_ui_choice *mine = sc_alloc(sc, (choice_count ? choice_count : 1) * sizeof *mine);
  for (size_t i = 0; i < choice_count; i++)
    mine[i] = (qa_ui_choice){sc_str(sc, choices[i].id), sc_str(sc, choices[i].label)};
  c->choices = mine; c->choice_count = choice_count;
  c->selected = selected ? sc_str(sc, selected) : NULL;
  c->ctx = sc_action(sc, m, kind, a ? sc_str(sc, a) : NULL, NULL);
  c->select_choice = on_select;
  return c;
}
static qa_ui_control *text_control(scratch *sc, qa_app_startup_menu *m, const char *id,
                                   const char *label, const char *text, double max_len,
                                   qa_rect rect, bool enabled, int kind) {
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
/* TS row(row, index) used by the sound menu. */
static void push_row(scratch *sc, qa_app_startup_menu *m, const qa_app_startup_menu_row *row, double index) {
  qa_rect rect = {64, 114 + index * 33, 512, 29};
  if (streq(row->field, "grapple")) {
    qa_ui_choice stack[64]; size_t n = 0;
    for (size_t i = 0; i < row->choice_count && n < 64; i++)
      if (!row->choices[i].unavailable) stack[n++] = (qa_ui_choice){row->choices[i].id, row->choices[i].label};
    choice_control(sc, m, "ui:startup:grapple", row->label, rect, !m->busy, true,
                   stack, n, row->value, ACT_GRAPPLE, NULL);
    return;
  }
  if (streq(row->field, "grenades")) {
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
  const char *selected = row->value;
  for (size_t i = 0; i < row->choice_count; i++)
    if (streq(row->choices[i].id, row->value)) selected = row->choices[i].label;
  char *label = fit(sc, m, sc_fmt(sc, "%s: %s", row->label, selected), 486, qa_ui_menu_text.body);
  button(sc, m, row->field, label, index, true, ACT_ROW_SELECT, row->field, NULL);
}

static void on_activate(void *ctx, qa_seat_id seat) {
  (void)seat;
  action *act = ctx;
  qa_app_startup_menu *m = act->m;
  void *octx = m->o.ctx;
  switch (act->kind) {
    case ACT_OPEN:
      if (act->b) set_status(m, "", m->busy); /* TS clears status only on some entries */
      qa_native_ui_controller_open_menu(m->controller, act->a); break;
    case ACT_CLOSE_MENU: qa_native_ui_controller_close_menu(m->controller); break;
    case ACT_QUIT: if (m->o.quit) m->o.quit(octx); break;
    case ACT_PLAY: if (m->o.play) m->o.play(octx); break;
    case ACT_LOAD_MENU:
      m->save_page = 0;
      qa_native_ui_controller_open_menu(m->controller, ID_LOAD);
      if (m->o.refresh_saves) m->o.refresh_saves(octx);
      break;
    case ACT_FAMILY:
      set_fixed(m->native_family, sizeof m->native_family, act->a);
      m->campaign_page = 0; set_status(m, "", m->busy);
      qa_native_ui_controller_open_menu(m->controller, ID_CAMPAIGN);
      break;
    case ACT_CUSTOM:
      set_status(m, "", m->busy);
      qa_native_ui_controller_open_menu(m->controller, ID_SESSION);
      break;
    case ACT_EDITION:
      set_fixed(m->native_edition, sizeof m->native_edition, act->a);
      m->campaign_page = 0; set_status(m, "", m->busy);
      break;
    case ACT_PRESET: {
      const qa_app_startup_menu_preset *preset = find_preset(m, act->a);
      if (!preset) break;
      set_str(&m->native_preset_id, preset->id);
      set_fixed(m->native_skill, sizeof m->native_skill, preset->default_skill);
      set_str(&m->native_arena, NULL);
      set_status(m, "", m->busy);
      if (!streq(preset->id, "q3-baseq3")) {
        qa_native_ui_controller_open_menu(m->controller, ID_DIFFICULTY);
        break;
      }
      set_status(m, "Loading arenas...", true);
      const char *error = m->o.refresh_base_arenas ? m->o.refresh_base_arenas(octx) : NULL;
      if (error) { set_status(m, error, false); break; }
      set_status(m, "", false);
      qa_native_ui_controller_open_menu(m->controller, ID_ARENAS);
      break;
    }
    case ACT_CAMPAIGN_PAGE: {
      size_t total = native_campaigns(m);
      size_t count = 0;
      const qa_app_startup_menu_preset *presets = m->o.presets ? m->o.presets(octx, &count) : NULL;
      bool toggle = false, has_c = false, has_r = false;
      if (presets) for (size_t i = 0; i < count; i++) if (streq(presets[i].family, m->native_family)) {
        if (streq(presets[i].edition, "classic")) has_c = true;
        if (streq(presets[i].edition, "rerelease")) has_r = true;
      }
      toggle = has_c && has_r;
      double per_page = toggle ? 7 : 8;
      double pages = ceil((double)total / per_page); if (pages < 1) pages = 1;
      m->campaign_page = fmod(m->campaign_page + 1, pages);
      break;
    }
    case ACT_DIFFICULTY: set_fixed(m->native_skill, sizeof m->native_skill, act->a); break;
    case ACT_PLAY_PRESET:
      if (m->o.play_preset && m->native_preset_id)
        m->o.play_preset(octx, m->native_preset_id, strtod(m->native_skill, NULL), m->native_arena);
      break;
    case ACT_SAVE_LOAD: if (m->o.load) m->o.load(octx, act->a); break;
    case ACT_SAVE_PAGE: {
      const qa_app_startup_save_list *saves = m->o.saves ? m->o.saves(octx) : NULL;
      double pages = saves ? ceil((double)saves->row_count / 5) : 1;
      if (pages < 1) pages = 1;
      m->save_page = fmod(m->save_page + 1, pages);
      break;
    }
    case ACT_REFRESH_SAVES: if (m->o.refresh_saves) m->o.refresh_saves(octx); break;
    case ACT_HOST_APPLY: {
      qa_app_startup_menu_hosting value = m->host_draft;
      value.kind = m->host_kind;
      if (!streq(m->host_kind, "offline")) value.port = strtod(m->host_port, NULL);
      const char *error = m->o.set_hosting ? m->o.set_hosting(octx, value) : NULL;
      if (error) { set_status(m, error, false); break; }
      m->host_draft = value;
      set_status(m, "", m->busy);
      qa_native_ui_controller_close_menu(m->controller);
      break;
    }
    case ACT_SELECT_CHOICE: {
      if (m->monster_kind == 2) { if (m->o.select_monster) m->o.select_monster(octx, m->monster_classname, act->a); }
      else if (m->monster_kind == 1) { if (m->o.select_monster_source) m->o.select_monster_source(octx, act->a); }
      else if (m->o.select) m->o.select(octx, m->field, act->a);
      set_status(m, "", m->busy);
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
      const qa_app_startup_menu_row *rows = safe_rows(m, &count);
      const qa_app_startup_menu_row *row = find_row(rows, count, act->a);
      if (!row) break;
      m->dropdown_active = true;
      set_fixed(m->dd_field, sizeof m->dd_field, act->a);
      set_fixed(m->dd_stage, sizeof m->dd_stage, act->a);
      dropdown_open_state(m, row);
      m->preview_armed = false;
      break;
    }
    case ACT_BUILDER_CHOICE: {
      char stage[32]; set_fixed(stage, sizeof stage, m->dd_stage);
      size_t count = 0;
      const qa_app_startup_menu_row *rows = safe_rows(m, &count);
      const qa_app_startup_menu_row *fresh = find_row(rows, count, stage);
      bool was_selected = fresh && streq(fresh->value, act->a);
      (void)was_selected; /* no turntable: TS closes when advance fails */
      if (m->o.select) m->o.select(octx, stage, act->a);
      set_status(m, "", m->busy);
      if (!advance_stage(m)) { m->dropdown_active = false; m->preview_armed = false; }
      if (streq(stage, "enemies") && streq(act->a, "custom")) open_roster(m);
      break;
    }
    case ACT_BUILDER_GROUP: {
      if (!m->dropdown_active) break;
      if (collapsed_has(m, act->a)) {
        size_t w = 0;
        for (size_t i = 0; i < m->dd_collapsed_count; i++) {
          if (streq(m->dd_collapsed[i], act->a)) { free(m->dd_collapsed[i]); continue; }
          m->dd_collapsed[w++] = m->dd_collapsed[i];
        }
        m->dd_collapsed_count = w;
      } else {
        char **grown = realloc(m->dd_collapsed, (m->dd_collapsed_count + 1) * sizeof *grown);
        if (!grown) qa_range_error("Out of memory");
        m->dd_collapsed = grown;
        m->dd_collapsed[m->dd_collapsed_count++] = xdup(act->a);
      }
      break;
    }
    case ACT_BUILDER_STAGE_BACK: (void)retreat_stage(m); break;
    case ACT_BUILDER_EDITION: {
      if (!m->dropdown_active) break;
      char stage[32]; set_fixed(stage, sizeof stage, m->dd_stage);
      set_fixed(m->dd_edition, sizeof m->dd_edition, act->a);
      m->dd_has_edition = true;
      m->dd_scroll = 0;
      size_t count = 0;
      const qa_app_startup_menu_row *rows = safe_rows(m, &count);
      const qa_app_startup_menu_row *fresh = find_row(rows, count, stage);
      if (!fresh) break;
      const qa_app_startup_menu_choice *current = NULL;
      for (size_t i = 0; i < fresh->choice_count; i++)
        if (streq(fresh->choices[i].id, fresh->value)) current = &fresh->choices[i];
      if (!current || !current->edition || streq(current->edition, act->a)) break;
      const qa_app_startup_menu_choice *counterpart = edition_variant(fresh, current, act->a);
      if (counterpart && !streq(counterpart->id, current->id) && !counterpart->unavailable) {
        if (m->o.select) m->o.select(octx, stage, counterpart->id);
        set_status(m, "", m->busy);
      }
      break;
    }
    case ACT_FOOTER_HOSTING:
      if (m->o.hosting) m->host_draft = m->o.hosting(octx);
      set_fixed(m->host_kind, sizeof m->host_kind, m->host_draft.kind);
      snprintf(m->host_port, sizeof m->host_port, "%g", m->host_draft.port);
      set_status(m, "", m->busy);
      qa_native_ui_controller_open_menu(m->controller, ID_HOSTING);
      break;
    case ACT_FOOTER_MODS:
      if (m->mods_menu) qa_native_ui_controller_open_menu(m->controller, m->mods_menu);
      break;
    case ACT_FOOTER_BROWSE: qa_native_ui_controller_open_menu(m->controller, ID_BROWSER); break;
    case ACT_FOOTER_LOBBY: qa_app_startup_menu_open_local_lobby(m); break;
    case ACT_BROWSER_QUERY: qa_app_startup_server_browser_query(m->o.browser); break;
    case ACT_BROWSER_SCAN: qa_app_startup_server_browser_scan(m->o.browser); break;
    case ACT_BROWSER_FAVORITE: qa_app_startup_server_browser_favorite(m->o.browser); break;
    case ACT_BROWSER_ENTRY: qa_app_startup_server_browser_select(m->o.browser, act->a); break;
    case ACT_BROWSER_PAGE: {
      size_t count = 0;
      (void)qa_app_startup_server_browser_rows(m->o.browser, &count);
      double pages = ceil((double)count / 3); if (pages < 1) pages = 1;
      m->page = fmod(m->page + 1, pages);
      break;
    }
    case ACT_BROWSER_FAV_ONLY:
      qa_app_startup_server_browser_set_favorites_only(m->o.browser,
          !qa_app_startup_server_browser_favorites_only(m->o.browser));
      m->page = 0;
      break;
    case ACT_BROWSER_CONNECT:
      if (m->o.connect) m->o.connect(octx, qa_app_startup_server_browser_connection(m->o.browser));
      break;
    case ACT_BROWSER_DETAILS:
      m->page = 0;
      qa_native_ui_controller_open_menu(m->controller, ID_BROWSER_DET);
      break;
    case ACT_BROWSER_DISCOVER: qa_app_startup_server_browser_discover(m->o.browser); break;
    case ACT_BROWSER_DETAIL_PAGE: {
      size_t line_count = 0;
      char **lines = qa_app_startup_server_browser_details(m->o.browser, &line_count);
      size_t chunks = 0;
      for (size_t i = 0; i < line_count; i++) {
        size_t length = strlen(lines[i]);
        chunks += length ? (length + 63) / 64 : 1;
        free(lines[i]);
      }
      free(lines);
      double pages = ceil((double)chunks / 8); if (pages < 1) pages = 1;
      m->page = fmod(m->page + 1, pages);
      break;
    }
    default: break; /* ACT_NOOP and detail lines */
  }
}
static void on_select(void *ctx, qa_seat_id seat, const char *choice) {
  (void)seat;
  action *act = ctx;
  qa_app_startup_menu *m = act->m;
  switch (act->kind) {
    case ACT_HOST_KIND:
      if (streq(choice, "offline") || streq(choice, "native-server") || streq(choice, "unified-server"))
        set_fixed(m->host_kind, sizeof m->host_kind, choice);
      break;
    case ACT_HOST_Q1:
      if (streq(choice, "15") || streq(choice, "666") || streq(choice, "999")) {
        m->host_draft.has_q1_protocol = true;
        m->host_draft.q1_protocol_version = strtod(choice, NULL);
      }
      break;
    case ACT_GRAPPLE: if (m->o.select) m->o.select(m->o.ctx, "grapple", choice); break;
    case ACT_FIELD_SELECT: if (m->o.select) m->o.select(m->o.ctx, act->a, choice); break;
    case ACT_TEAM: if (m->o.team_write) m->o.team_write(m->o.ctx, act->a, choice); break;
    case ACT_BROWSER_PROTOCOL:
      qa_app_startup_server_browser_choose(m->o.browser, choice); m->page = 0; break;
    case ACT_BROWSER_SORT:
      qa_app_startup_server_browser_choose_sort(m->o.browser, choice);
      set_fixed(m->browser_sort, sizeof m->browser_sort, choice);
      m->page = 0; break;
    default: break;
  }
}
static void on_checked(void *ctx, qa_seat_id seat, bool checked) {
  (void)seat;
  action *act = ctx;
  qa_app_startup_menu *m = act->m;
  switch (act->kind) {
    case ACT_GRENADES:
      if (m->o.select) m->o.select(m->o.ctx, "grenades", checked ? "enabled" : "disabled");
      break;
    case ACT_BROWSER_HIDE_EMPTY:
      qa_app_startup_server_browser_set_hide_empty(m->o.browser, checked); m->page = 0; break;
    case ACT_BROWSER_HIDE_FULL:
      qa_app_startup_server_browser_set_hide_full(m->o.browser, checked); m->page = 0; break;
    default: break;
  }
}
static void on_change_text(void *ctx, qa_seat_id seat, const char *text) {
  (void)seat;
  action *act = ctx;
  qa_app_startup_menu *m = act->m;
  switch (act->kind) {
    case ACT_HOST_PORT: set_fixed(m->host_port, sizeof m->host_port, text); break;
    case ACT_BROWSER_ADDRESS: qa_app_startup_server_browser_set_address(m->o.browser, text); break;
    case ACT_BROWSER_FILTER:
      qa_app_startup_server_browser_set_filter(m->o.browser, text); m->page = 0; break;
    case ACT_BROWSER_MASTER: qa_app_startup_server_browser_set_master_address(m->o.browser, text); break;
    default: break;
  }
}
static void on_submit_text(void *ctx, qa_seat_id seat, const char *text) {
  (void)seat; (void)text;
  action *act = ctx;
  qa_app_startup_menu *m = act->m;
  if (act->kind == ACT_BROWSER_ADDRESS) qa_app_startup_server_browser_query(m->o.browser);
}

static void noop_open(void *ctx, qa_seat_id seat) { (void)ctx; (void)seat; }
static qa_ui_menu finish_menu(slot *s, qa_ui_menu_id id) {
  qa_ui_menu menu = {0};
  menu.id = id; menu.title = ""; menu.full_screen = false;
  menu.controls = s->sc.controls; menu.control_count = s->sc.count;
  menu.open = noop_open; menu.close = noop_open;
  return menu;
}
static qa_ui_menu finish_scroll(slot *s, qa_ui_menu_id id, qa_rect rect, double row_height, size_t scroll_controls) {
  scratch *sc = &s->sc;
  const qa_ui_control_id *ids = sc_alloc(sc, (scroll_controls ? scroll_controls : 1) * sizeof *ids);
  for (size_t i = 0; i < scroll_controls; i++) ((qa_ui_control_id *)ids)[i] = sc->controls[i].id;
  sc->scroll = (qa_ui_menu_scroll){rect, (double)scroll_controls * row_height, ids, scroll_controls};
  qa_ui_menu menu = finish_menu(s, id);
  menu.scroll = &sc->scroll;
  return menu;
}

static qa_ui_menu build_main(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = &s->sc;
  sc_reset(sc);
  button(sc, m, "native", "Play a game", 0, false, ACT_OPEN, ID_FAMILY, "clear");
  button(sc, m, "load", "Load Game", 1, false, ACT_LOAD_MENU, NULL, NULL);
  button(sc, m, "options", "Options", 2, false, ACT_OPEN, ID_OPTIONS, NULL);
  if (m->o.has_libraries) button(sc, m, "library", "Library", 3, false, ACT_OPEN, ID_LIBRARY, NULL);
  button(sc, m, "quit", "Quit", m->o.has_libraries ? 4 : 3, false, ACT_QUIT, NULL, NULL);
  return finish_menu(s, ID_MAIN);
}
static qa_ui_menu build_library(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = &s->sc;
  sc_reset(sc);
  for (size_t i = 0; i < m->library_page_count; i++) {
    char id[128]; snprintf(id, sizeof id, "library:%s", m->library_pages[i].name);
    button(sc, m, id, m->library_pages[i].label, (double)i, true, ACT_OPEN, m->library_pages[i].root, NULL);
  }
  back_button(sc, m);
  return finish_menu(s, ID_LIBRARY);
}
static qa_ui_menu build_family(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = &s->sc;
  sc_reset(sc);
  static const struct { const char *family; const char *label; } games[] = {
    {"q1", "Quake"}, {"q2", "Quake II"}, {"q3", "Quake III"}};
  size_t count = 0;
  const qa_app_startup_menu_preset *presets = m->o.presets ? m->o.presets(m->o.ctx, &count) : NULL;
  for (size_t i = 0; i < 3; i++) {
    char id[32]; snprintf(id, sizeof id, "game:%s", games[i].family);
    qa_ui_control *c = button(sc, m, id, games[i].label, (double)i, true, ACT_FAMILY, games[i].family, NULL);
    bool any = false;
    if (presets) for (size_t j = 0; j < count; j++) if (streq(presets[j].family, games[i].family)) any = true;
    c->enabled = !m->busy && any;
  }
  button(sc, m, "custom", "Custom game", 3, true, ACT_CUSTOM, NULL, NULL);
  back_button(sc, m);
  return finish_menu(s, ID_FAMILY);
}
static qa_ui_menu build_campaign(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = &s->sc;
  sc_reset(sc);
  size_t count = 0;
  const qa_app_startup_menu_preset *presets = m->o.presets ? m->o.presets(m->o.ctx, &count) : NULL;
  bool has_c = false, has_r = false;
  if (presets) for (size_t i = 0; i < count; i++) if (streq(presets[i].family, m->native_family)) {
    if (streq(presets[i].edition, "classic")) has_c = true;
    if (streq(presets[i].edition, "rerelease")) has_r = true;
  }
  bool toggle = has_c && has_r;
  double offset = toggle ? 1 : 0;
  size_t total = native_campaigns(m);
  double per_page = 8 - offset;
  double pages = ceil((double)total / per_page); if (pages < 1) pages = 1;
  if (m->campaign_page > pages - 1) m->campaign_page = pages - 1;
  size_t start = (size_t)(m->campaign_page * per_page);
  for (size_t i = start; i < total && i < start + (size_t)per_page; i++) {
    const qa_app_startup_menu_preset *preset = m->campaigns[i];
    char *label = fit(sc, m, sc_fmt(sc, "%s%s", preset->label, preset->unavailable ? " (unavailable)" : ""),
                      486, qa_ui_menu_text.body);
    char id[256]; snprintf(id, sizeof id, "preset:%s", preset->id);
    qa_ui_control *c = button(sc, m, id, label, offset + (double)(i - start), true, ACT_PRESET, preset->id, NULL);
    c->enabled = !m->busy && !preset->unavailable;
  }
  if (toggle) {
    static const char *const editions[] = {"classic", "rerelease"};
    for (int i = 0; i < 2; i++) {
      char id[64]; snprintf(id, sizeof id, "edition-%s", editions[i]);
      char *label = sc_fmt(sc, "%s%s", streq(m->native_edition, editions[i]) ? "> " : "",
                           i == 0 ? "Classic" : "Rerelease");
      qa_ui_control *c = button(sc, m, id, label, 0, true, ACT_EDITION, editions[i], NULL);
      c->rect = (qa_rect){i == 0 ? 64 : 328, 114, 248, 29};
    }
  }
  if (pages > 1) {
    char *label = sc_fmt(sc, "More campaigns (%g/%g)", m->campaign_page + 1, pages);
    button(sc, m, "campaign-page", label, 8, true, ACT_CAMPAIGN_PAGE, NULL, NULL);
  }
  back_button(sc, m);
  return finish_menu(s, ID_CAMPAIGN);
}
static qa_ui_menu build_difficulty(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = &s->sc;
  sc_reset(sc);
  const qa_app_startup_menu_preset *preset = find_preset(m, m->native_preset_id);
  if (!preset) { back_button(sc, m); return finish_menu(s, ID_DIFFICULTY); }
  for (size_t i = 0; i < preset->difficulty_count; i++) {
    const qa_app_startup_menu_choice *d = &preset->difficulties[i];
    char id[256]; snprintf(id, sizeof id, "difficulty:%s", d->id);
    char *label = sc_fmt(sc, "%s%s", streq(m->native_skill, d->id) ? "> " : "", d->label);
    qa_ui_control *c = button(sc, m, id, label, (double)i, true, ACT_DIFFICULTY, d->id, NULL);
    c->enabled = !m->busy && !d->unavailable;
  }
  if (streq(preset->id, "q3-missionpack") && m->o.has_team_arena) {
    static const char *const sides[] = {"player", "opponent"};
    size_t choice_count = 0;
    const qa_ui_choice *choices = m->o.team_choices ? m->o.team_choices(m->o.ctx, &choice_count) : NULL;
    const char *player = "", *opponent = "";
    if (m->o.team_read) m->o.team_read(m->o.ctx, &player, &opponent);
    for (int i = 0; i < 2; i++) {
      char id[64]; snprintf(id, sizeof id, "ui:startup:team:%s", sides[i]);
      choice_control(sc, m, id, i == 0 ? "Your team" : "Opponent",
                     qa_ui_menu_row_default(5.0 + i), !m->busy, true,
                     choices, choice_count, i == 0 ? player : opponent, ACT_TEAM, sides[i]);
    }
  }
  qa_ui_control *play = button(sc, m, "play-preset", "Play", 7, true, ACT_PLAY_PRESET, NULL, NULL);
  play->enabled = !m->busy && !preset->unavailable && m->o.play_preset != NULL;
  back_button(sc, m);
  return finish_menu(s, ID_DIFFICULTY);
}
static qa_ui_menu build_options(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = &s->sc;
  sc_reset(sc);
  button(sc, m, "display", "Display", 0, true, ACT_OPEN, ID_DISPLAY, NULL);
  button(sc, m, "sound", "Sound", 1, true, ACT_OPEN, ID_SOUND, NULL);
  button(sc, m, "controls", "Controls", 2, true, ACT_OPEN, ID_CONTROLS, NULL);
  bool accessibility = false;
  for (size_t i = 0; i < m->o.setting_count; i++)
    if (streq(m->o.settings[i].category, "accessibility")) accessibility = true;
  if (accessibility) button(sc, m, "accessibility", "Accessibility", 3, true, ACT_OPEN, ID_ACCESS, NULL);
  button(sc, m, "all-options", "All options", 4, true, ACT_OPEN, "menu:settings:root", NULL);
  if (m->has_llm) button(sc, m, "llm", "LLM options", 5, true, ACT_OPEN, m->llm_root, NULL);
  back_button(sc, m);
  return finish_menu(s, ID_OPTIONS);
}
static qa_ui_menu build_hosting(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = &s->sc;
  sc_reset(sc);
  static const qa_ui_choice kinds[] = {{"offline", "Local only"}, {"native-server", "Native game clients"},
                                       {"unified-server", "This client: mixed games"}};
  choice_control(sc, m, "ui:startup:host-kind", "Connections", qa_ui_menu_row_default(0),
                 !m->busy, true, kinds, 3, m->host_kind, ACT_HOST_KIND, NULL);
  text_control(sc, m, "ui:startup:host-port", "Port", m->host_port, 5, qa_ui_menu_row_default(1),
               !m->busy && !streq(m->host_kind, "offline"), ACT_HOST_PORT);
  static const qa_ui_choice protocols[] = {{"15", "NetQuake (15)"}, {"666", "FitzQuake (666)"}, {"999", "RMQ (999)"}};
  char version[16];
  snprintf(version, sizeof version, "%g", m->host_draft.has_q1_protocol ? m->host_draft.q1_protocol_version : 15);
  choice_control(sc, m, "ui:startup:host-q1-protocol", "Quake protocol", qa_ui_menu_row_default(2),
                 !m->busy, streq(m->host_kind, "native-server") && m->host_draft.has_q1_protocol,
                 protocols, 3, version, ACT_HOST_Q1, NULL);
  const char *mode = m->o.mode ? m->o.mode(m->o.ctx) : NULL;
  const char *family = m->o.product_family ? m->o.product_family(m->o.ctx) : NULL;
  const char *note = streq(mode, "singleplayer") && !streq(m->host_kind, "offline")
      ? (streq(family, "q3") ? "Uses Deathmatch. Change rules in Combat." : "Uses Co-op. Change rules in Combat.")
      : "Choose game rules and difficulty in Combat.";
  qa_ui_control *info = button(sc, m, "host-mode", note, 3, true, ACT_NOOP, NULL, NULL);
  info->enabled = false;
  button(sc, m, "host-apply", "Apply", 4, true, ACT_HOST_APPLY, NULL, NULL);
  back_button(sc, m);
  return finish_menu(s, ID_HOSTING);
}
static qa_ui_menu build_sound(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = &s->sc;
  sc_reset(sc);
  size_t count = 0;
  const qa_app_startup_menu_row *rows = safe_rows(m, &count);
  static const char *const fields[] = {"environment", "doppler"};
  size_t index = 0;
  for (int i = 0; i < 2; i++) {
    const qa_app_startup_menu_row *row = find_row(rows, count, fields[i]);
    if (!row) continue;
    push_row(sc, m, row, (double)index);
    sc->controls[sc->count - 1].rect = (qa_rect){64, 114 + (double)index * 36, 496, 32};
    index++;
  }
  for (size_t i = 0; i < m->o.setting_count; i++) {
    if (!streq(m->o.settings[i].category, "audio")) continue;
    qa_rect rect = {64, 114 + (double)index * 36, 496, 32};
    qa_ui_control control = qa_ui_setting_control(&m->o.settings[i], rect, m->o.seat, sc_buf(sc), 64);
    *sc_push(sc) = control;
    index++;
  }
  size_t scroll_controls = sc->count;
  back_button(sc, m);
  return finish_scroll(s, ID_SOUND, (qa_rect){64, 114, 512, 288}, 36, scroll_controls);
}
static qa_ui_menu build_select(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = &s->sc;
  sc_reset(sc);
  const qa_app_startup_menu_row *row = selection_row(m);
  size_t total = row ? row->choice_count : 0;
  for (size_t i = 0; i < total; i++) {
    const qa_app_startup_menu_choice *choice = &row->choices[i];
    char *label = fit(sc, m, sc_fmt(sc, "%s%s%s", streq(row->value, choice->id) ? "> " : "",
                                    choice->label, choice->unavailable ? " (unavailable)" : ""),
                      466, qa_ui_menu_text.body);
    char id[512]; snprintf(id, sizeof id, "choice:%s", choice->id);
    qa_ui_control *c = button(sc, m, id, label, (double)i, true, ACT_SELECT_CHOICE, choice->id, NULL);
    c->enabled = !choice->unavailable && !m->busy;
  }
  back_button(sc, m);
  return finish_scroll(s, ID_SELECT, (qa_rect){64, 114, 512, 297}, 33, total);
}
static qa_ui_menu build_roster(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = &s->sc;
  sc_reset(sc);
  qa_app_startup_menu_row source = m->o.monster_source_row
      ? m->o.monster_source_row(m->o.ctx) : (qa_app_startup_menu_row){0};
  const char *source_label = source.value;
  for (size_t i = 0; i < source.choice_count; i++)
    if (streq(source.choices[i].id, source.value)) source_label = source.choices[i].label;
  char *label = fit(sc, m, sc_fmt(sc, "Monster source: %s", source_label ? source_label : ""), 486, qa_ui_menu_text.body);
  qa_ui_control *head = button(sc, m, "monster-source", label, 0, true, ACT_ROSTER_SOURCE, NULL, NULL);
  head->rect = (qa_rect){64, 78, 512, 26};
  size_t count = 0;
  const qa_app_startup_menu_monster_row *rows =
      m->o.monster_roster_rows ? m->o.monster_roster_rows(m->o.ctx, &count) : NULL;
  size_t first = sc->count;
  if (rows) for (size_t i = 0; i < count; i++) {
    char id[512]; snprintf(id, sizeof id, "monster:%s", rows[i].classname ? rows[i].classname : "default");
    char *text = fit(sc, m, sc_fmt(sc, "%s: %s", rows[i].label, rows[i].effective_label), 486, qa_ui_menu_text.body);
    qa_ui_control *c = button(sc, m, id, text, (double)i, true, ACT_ROSTER_CLASS, rows[i].classname, NULL);
    c->rect = (qa_rect){64, 114 + (double)i * 33, 512, 29};
  }
  size_t scroll_controls = sc->count - first;
  back_button(sc, m);
  /* TS scroll lists only the roster rows, which sit after the source head. */
  scratch *scc = sc;
  const qa_ui_control_id *ids = sc_alloc(scc, (scroll_controls ? scroll_controls : 1) * sizeof *ids);
  for (size_t i = 0; i < scroll_controls; i++) ((qa_ui_control_id *)ids)[i] = scc->controls[first + i].id;
  scc->scroll = (qa_ui_menu_scroll){{64, 114, 512, 297}, (double)scroll_controls * 33, ids, scroll_controls};
  qa_ui_menu menu = finish_menu(s, ID_ROSTER);
  menu.scroll = &scc->scroll;
  return menu;
}
static qa_ui_menu build_load(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = &s->sc;
  sc_reset(sc);
  const qa_app_startup_save_list *saves = m->o.saves ? m->o.saves(m->o.ctx) : NULL;
  size_t total = saves ? saves->row_count : 0;
  double pages = ceil((double)total / 5); if (pages < 1) pages = 1;
  if (m->save_page > pages - 1) m->save_page = pages - 1;
  size_t start = (size_t)(m->save_page * 5);
  for (size_t i = start; saves && i < total && i < start + 5; i++) {
    const qa_app_startup_save_row *save = &saves->rows[i];
    char id[512]; snprintf(id, sizeof id, "save:%s", save->id);
    char *label = fit(sc, m, sc_fmt(sc, "%s  -  %s", save->label, save->map), 486, qa_ui_menu_text.body);
    qa_ui_control *c = button(sc, m, id, label, (double)(i - start), true, ACT_SAVE_LOAD, save->id, NULL);
    c->rect = (qa_rect){64, 118 + (double)(i - start) * 46, 512, 42};
    c->enabled = !save->unavailable && !m->busy;
  }
  if (pages > 1) button(sc, m, "older-saves", "Older saves", 7, true, ACT_SAVE_PAGE, NULL, NULL);
  button(sc, m, "refresh-saves", "Refresh", 8, true, ACT_REFRESH_SAVES, NULL, NULL);
  back_button(sc, m);
  return finish_menu(s, ID_LOAD);
}

/* TS builderLayout() control set (heads/labels/values are draw()-only). */
static qa_ui_menu build_session(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = &s->sc;
  sc_reset(sc);
  size_t count = 0;
  const qa_app_startup_menu_row *rows = safe_rows(m, &count);
  bool dd = m->dropdown_active;
  double panel_x = -1;
  if (dd) {
    static const char *const right[] = {"product", "mapProduct", "map", "enemies", "character",
                                        "model", "skin", "topColor", "bottomColor", "movement", "weapons"};
    panel_x = 64;
    for (size_t i = 0; i < sizeof right / sizeof *right; i++)
      if (streq(m->dd_field, right[i])) panel_x = 328;
  }
  /* Dropdown items first (TS controls order), then toggle, fields, footer. */
  size_t dd_item_count = 0;
  const qa_app_startup_menu_row *dd_row = NULL;
  bool staged = false;
  double dd_visible = 0, items_top = 0;
  if (dd) {
    dd_row = find_row(rows, count, m->dd_stage);
    if (!dd_row) { m->dropdown_active = false; dd = false; }
  }
  if (dd && dd_row) {
    size_t total = dropdown_entries(m, dd_row, m->dd_has_edition ? m->dd_edition : NULL, true, NULL, 0);
    staged = !streq(m->dd_stage, m->dd_field);
    dd_visible = (m->dd_has_edition ? 7 : 8) - (staged ? 1 : 0);
    items_top = m->dd_has_edition ? 164 : 138;
    double max_scroll = (double)total - dd_visible; if (max_scroll < 0) max_scroll = 0;
    double scroll = m->dd_scroll; if (scroll > max_scroll) scroll = max_scroll; if (scroll < 0) scroll = 0;
    /* TS itemScale (consumed by draw(); kept for the measure contract). */
    double item_scale = qa_ui_menu_text.field;
    for (size_t i = 0; i < total; i++) {
      double candidate = fit_scale(m, m->entries[i].header ? m->entries[i].group : m->entries[i].choice->label,
                                   216, qa_ui_menu_text.field);
      if (candidate < item_scale) item_scale = candidate;
    }
    (void)item_scale;
    if (staged) {
      qa_ui_control *c = sc_push(sc);
      c->id = "ui:startup:stage-back"; c->kind = "button"; c->label = "";
      c->rect = (qa_rect){panel_x, items_top, 248, 26};
      c->visible = true; c->enabled = !m->busy;
      c->ctx = sc_action(sc, m, ACT_BUILDER_STAGE_BACK, NULL, NULL);
      c->activate_seat = on_activate;
      dd_item_count++;
    }
    for (size_t i = (size_t)scroll; i < total && i < (size_t)scroll + (size_t)dd_visible; i++) {
      entry *e = &m->entries[i];
      qa_rect rect = {panel_x, items_top + ((double)(i - (size_t)scroll) + (staged ? 1 : 0)) * 27, 248, 26};
      qa_ui_control *c = sc_push(sc);
      c->kind = "button"; c->label = ""; c->rect = rect; c->visible = true;
      if (e->header) {
        c->id = sc_fmt(sc, "ui:startup:group:%s", e->group);
        c->enabled = !m->busy;
        c->ctx = sc_action(sc, m, ACT_BUILDER_GROUP, sc_str(sc, e->group), NULL);
      } else {
        const qa_app_startup_menu_choice *choice = e->choice;
        if (choice->unavailable) {
          char key[1024]; variant_key(choice, key, sizeof key);
          for (size_t j = 0; j < dd_row->choice_count; j++) {
            char other[1024]; variant_key(&dd_row->choices[j], other, sizeof other);
            if (strcmp(other, key) == 0 && !dd_row->choices[j].unavailable) { choice = &dd_row->choices[j]; break; }
          }
        }
        const qa_app_startup_menu_choice *current = NULL;
        for (size_t j = 0; j < dd_row->choice_count; j++)
          if (streq(dd_row->choices[j].id, dd_row->value)) current = &dd_row->choices[j];
        char key_a[1024] = "", key_b[1024] = "";
        if (current) variant_key(current, key_a, sizeof key_a);
        variant_key(choice, key_b, sizeof key_b);
        bool selected = current && strcmp(key_a, key_b) == 0;
        c->id = selected ? sc_fmt(sc, "ui:startup:%s", dd_row->field)
                         : sc_fmt(sc, "ui:startup:choice:%s", choice->id);
        c->enabled = !choice->unavailable && !m->busy;
        c->ctx = sc_action(sc, m, ACT_BUILDER_CHOICE, sc_str(sc, choice->id), NULL);
      }
      c->activate_seat = on_activate;
      dd_item_count++;
    }
    (void)dd_item_count;
    if (m->dd_has_edition) {
      qa_ui_control *classic = button(sc, m, "edition-classic", "", 0, false, ACT_BUILDER_EDITION, "classic", NULL);
      classic->rect = (qa_rect){panel_x, 136, 120, 22};
      qa_ui_control *rerelease = button(sc, m, "edition-rerelease", "", 0, false, ACT_BUILDER_EDITION, "rerelease", NULL);
      rerelease->rect = (qa_rect){panel_x + 128, 136, 120, 22};
    }
  }
  static const struct { double x; const char *fields[4]; bool picker[4]; size_t n; } sections[] = {
    {64, {"product", "mapProduct", "enemies", NULL}, {true, true, true, false}, 3},
    {64, {"character", "movement", "weapons", NULL}, {true, true, true, false}, 3},
    {328, {"seats", "mode", "rules", "skill"}, {false, false, true, false}, 4},
    {328, {"grapple", "grappleStyle", "grenades", NULL}, {false, true, false, false}, 3},
  };
  double cursor_left = 110, cursor_right = 110;
  for (size_t si = 0; si < 4; si++) {
    if (dd && sections[si].x == panel_x) continue;
    size_t present = 0;
    for (size_t i = 0; i < sections[si].n; i++)
      if (find_row(rows, count, sections[si].fields[i])) present++;
    if (present == 0) continue;
    double start_y = sections[si].x == 64 ? cursor_left : cursor_right;
    size_t index = 0;
    for (size_t i = 0; i < sections[si].n; i++) {
      const qa_app_startup_menu_row *row = find_row(rows, count, sections[si].fields[i]);
      if (!row) continue;
      qa_rect rect = {sections[si].x, start_y + 15 + (double)index * 23, 248, 22};
      bool enabled = !m->busy && !dd;
      if (sections[si].picker[i]) {
        qa_ui_control *c = button(sc, m, row->field, "", 0, false, ACT_BUILDER_OPEN, row->field, NULL);
        c->rect = rect; c->enabled = enabled;
      } else {
        qa_ui_choice stack[128]; size_t n = 0;
        bool any_enabled_choice = false;
        for (size_t j = 0; j < row->choice_count && n < 128; j++) {
          if (row->choices[j].unavailable) continue;
          stack[n++] = (qa_ui_choice){row->choices[j].id, ""};
          if (streq(row->choices[j].id, "enabled")) any_enabled_choice = true;
        }
        bool field_enabled = streq(row->field, "grenades")
            ? enabled && (streq(row->value, "enabled") || any_enabled_choice) : enabled;
        char id[128]; snprintf(id, sizeof id, "ui:startup:%s", row->field);
        choice_control(sc, m, id, "", rect, field_enabled, true, stack, n, row->value,
                       ACT_FIELD_SELECT, row->field);
      }
      index++;
    }
    double next = start_y + 15 + (double)present * 23 + 8;
    if (sections[si].x == 64) cursor_left = next; else cursor_right = next;
  }
  /* Footer. */
  {
    bool footer_enabled = !dd;
    struct footer_item { int kind; const char *id; double width; };
    struct footer_item items[4];
    size_t n = 0;
    items[n++] = (struct footer_item){ACT_FOOTER_HOSTING, "hosting", 0};
    if (m->mods_menu) items[n++] = (struct footer_item){ACT_FOOTER_MODS, "mods", 72};
    if (m->o.browser) items[n++] = (struct footer_item){ACT_FOOTER_BROWSE, "browse", 118};
    if (m->lobby_menu) items[n++] = (struct footer_item){ACT_FOOTER_LOBBY, "local-lobby", 102};
    double fixed = 0;
    for (size_t i = 0; i < n; i++) fixed += items[i].width;
    double gaps = (double)(n - 1) * 8, x = 64;
    for (size_t i = 0; i < n; i++) {
      double width = items[i].width == 0 ? 512 - fixed - gaps : items[i].width;
      qa_ui_control *c = button(sc, m, items[i].id, "", 0, false, items[i].kind, NULL, NULL);
      c->rect = (qa_rect){x, 388, width, 24};
      c->enabled = footer_enabled && c->enabled;
      x += width + 8;
    }
    qa_ui_control *back = button(sc, m, "back", "", 0, false, ACT_CLOSE_MENU, NULL, NULL);
    back->rect = (qa_rect){64, 416, 248, 26}; back->enabled = footer_enabled;
    qa_ui_control *play = button(sc, m, "play", "", 0, false, ACT_PLAY, NULL, NULL);
    play->rect = (qa_rect){328, 416, 248, 26}; play->enabled = footer_enabled;
  }
  return finish_menu(s, ID_SESSION);
}

static qa_ui_control *browser_button(scratch *sc, qa_app_startup_menu *m, const char *id,
                                     const char *label, double x, double y, double width,
                                     int kind, const char *a) {
  qa_ui_control *c = button(sc, m, id, label, 0, false, kind, a, NULL);
  c->rect = (qa_rect){x, y, width, 30};
  return c;
}
static qa_ui_menu build_browser(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = &s->sc;
  sc_reset(sc);
  qa_app_startup_server_browser *browser = m->o.browser;
  if (!browser) { back_button(sc, m); return finish_menu(s, ID_BROWSER); }
  static const qa_ui_choice protocols[] = {{"q1", "Quake"}, {"qw", "QuakeWorld"},
                                           {"q2", "Quake II"}, {"q3", "Quake III Arena"}};
  choice_control(sc, m, "ui:startup:server-protocol", "Game", (qa_rect){64, 108, 512, 30},
                 true, true, protocols, 4, qa_app_startup_server_browser_protocol(browser),
                 ACT_BROWSER_PROTOCOL, NULL);
  text_control(sc, m, "ui:startup:server-address", "Address",
               qa_app_startup_server_browser_address(browser), 255, (qa_rect){64, 144, 512, 30},
               !m->busy, ACT_BROWSER_ADDRESS);
  browser_button(sc, m, "server-query", "Query", 64, 180, 160, ACT_BROWSER_QUERY, NULL);
  browser_button(sc, m, "server-lan", "Find LAN", 240, 180, 160, ACT_BROWSER_SCAN, NULL);
  browser_button(sc, m, "server-favorite", "Favorite", 416, 180, 160, ACT_BROWSER_FAVORITE, NULL);
  text_control(sc, m, "ui:startup:server-filter", "Filter",
               qa_app_startup_server_browser_filter(browser), 255, (qa_rect){64, 216, 320, 30},
               !m->busy, ACT_BROWSER_FILTER);
  bool filters_on = qa_app_startup_server_browser_hide_empty(browser)
      || qa_app_startup_server_browser_hide_full(browser);
  browser_button(sc, m, "server-sort-filter", filters_on ? "Filters on" : "Sort/filter",
                 400, 216, 176, ACT_OPEN, ID_BROWSER_OPTS);
  size_t total = 0;
  const qa_browser_entry **entries = qa_app_startup_server_browser_rows(browser, &total);
  double pages = ceil((double)total / 3); if (pages < 1) pages = 1;
  if (m->page > pages - 1) m->page = pages - 1;
  size_t start = (size_t)(m->page * 3);
  for (size_t i = start; entries && i < total && i < start + 3; i++) {
    const qa_browser_entry *e = entries[i];
    bool favorite = false;
    for (size_t j = 0; j < e->source_count; j++) if (streq(e->sources[j], "favorite")) favorite = true;
    char *address = qa_app_browser_address(&e->address);
    const char *name = e->has_status && e->status.name && e->status.name[0] ? e->status.name : address;
    char counts[64], ping[32] = "";
    if (e->has_status) snprintf(counts, sizeof counts, "%g/%g", e->status.players, e->status.max_players);
    else snprintf(counts, sizeof counts, "?");
    if (e->has_ping) snprintf(ping, sizeof ping, "%gms", (double)lround(e->ping_ms));
    char *label = fit(sc, m, sc_fmt(sc, "%s%s  %s  %s", favorite ? "* " : "", name, counts, ping),
                      490, qa_ui_menu_text.body);
    char *key = qa_net_address_key(&e->address, true);
    char id[512]; snprintf(id, sizeof id, "server:%s", key);
    browser_button(sc, m, id, label, 64, 252 + (double)(i - start) * 34, 512, ACT_BROWSER_ENTRY, key);
    free(key); free(address);
  }
  char *page_label = sc_fmt(sc, "Page %g/%g", m->page + 1, pages);
  browser_button(sc, m, "server-page", page_label, 64, 358, 240, ACT_BROWSER_PAGE, NULL);
  browser_button(sc, m, "server-favorites",
                 qa_app_startup_server_browser_favorites_only(browser) ? "Favorites only" : "All servers",
                 320, 358, 256, ACT_BROWSER_FAV_ONLY, NULL);
  browser_button(sc, m, "server-connect", "Connect", 64, 396, 160, ACT_BROWSER_CONNECT, NULL);
  browser_button(sc, m, "server-details", "Details", 240, 396, 160, ACT_BROWSER_DETAILS, NULL);
  browser_button(sc, m, "server-back", "Back", 416, 396, 160, ACT_CLOSE_MENU, NULL);
  return finish_menu(s, ID_BROWSER);
}
static qa_ui_menu build_browser_options(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = &s->sc;
  sc_reset(sc);
  qa_app_startup_server_browser *browser = m->o.browser;
  if (!browser) { back_button(sc, m); return finish_menu(s, ID_BROWSER_OPTS); }
  qa_ui_choice sorts[8];
  for (int i = 0; i < 8; i++) sorts[i] = (qa_ui_choice){qa_app_browser_sort_orders[i].id, qa_app_browser_sort_orders[i].label};
  choice_control(sc, m, "ui:startup:server-sort", "Sort", (qa_rect){64, 118, 512, 30}, true, true,
                 sorts, 8, m->browser_sort, ACT_BROWSER_SORT, NULL);
  static const struct { const char *id; const char *label; double y; int kind; } toggles[] = {
    {"ui:startup:server-hide-empty", "Hide empty", 152, ACT_BROWSER_HIDE_EMPTY},
    {"ui:startup:server-hide-full", "Hide full", 186, ACT_BROWSER_HIDE_FULL}};
  for (int i = 0; i < 2; i++) {
    qa_ui_control *c = sc_push(sc);
    c->id = toggles[i].id; c->kind = "toggle"; c->label = toggles[i].label;
    c->rect = (qa_rect){64, toggles[i].y, 512, 30};
    c->enabled = true; c->visible = true;
    c->checked = toggles[i].kind == ACT_BROWSER_HIDE_EMPTY
        ? qa_app_startup_server_browser_hide_empty(browser)
        : qa_app_startup_server_browser_hide_full(browser);
    c->ctx = sc_action(sc, m, toggles[i].kind, NULL, NULL);
    c->change_checked = on_checked;
  }
  text_control(sc, m, "ui:startup:server-master", "Master address or HTTP list",
               qa_app_startup_server_browser_master_address(browser), 2048,
               (qa_rect){64, 226, 512, 30}, true, ACT_BROWSER_MASTER);
  button(sc, m, "server-discover", "Find Internet servers", 5, true, ACT_BROWSER_DISCOVER, NULL, NULL);
  button(sc, m, "server-filters-back", "Back", 7, true, ACT_CLOSE_MENU, NULL, NULL);
  return finish_menu(s, ID_BROWSER_OPTS);
}
static qa_ui_menu build_browser_details(void *ctx) {
  slot *s = ctx; qa_app_startup_menu *m = s->m; scratch *sc = &s->sc;
  sc_reset(sc);
  qa_app_startup_server_browser *browser = m->o.browser;
  if (!browser) { back_button(sc, m); return finish_menu(s, ID_BROWSER_DET); }
  size_t line_count = 0;
  char **lines = qa_app_startup_server_browser_details(browser, &line_count);
  /* TS chunks lines at 64 chars (UTF-16 units; bytes here). */
  size_t chunks = 0;
  for (size_t i = 0; i < line_count; i++) chunks += (strlen(lines[i]) + 63) / 64 ? (strlen(lines[i]) + 63) / 64 : 1;
  double pages = ceil((double)chunks / 8); if (pages < 1) pages = 1;
  if (m->page > pages - 1) m->page = pages - 1;
  size_t skip = (size_t)(m->page * 8), shown = 0, seen = 0;
  for (size_t i = 0; i < line_count && shown < 8; i++) {
    size_t length = strlen(lines[i]);
    size_t parts = length ? (length + 63) / 64 : 1;
    for (size_t part = 0; part < parts && shown < 8; part++, seen++) {
      if (seen < skip) continue;
      char chunk[65];
      size_t take = length - part * 64 > 64 ? 64 : length - part * 64;
      memcpy(chunk, lines[i] + part * 64, take); chunk[take] = '\0';
      char id[64]; snprintf(id, sizeof id, "server-detail:%zu", shown);
      qa_ui_control *c = button(sc, m, id, chunk, (double)shown, true, ACT_NOOP, NULL, NULL);
      c->enabled = false;
      shown++;
    }
  }
  for (size_t i = 0; i < line_count; i++) free(lines[i]);
  free(lines);
  char *label = sc_fmt(sc, "Page %g/%g", m->page + 1, pages);
  button(sc, m, "server-detail-page", label, 8, true, ACT_BROWSER_DETAIL_PAGE, NULL, NULL);
  back_button(sc, m);
  return finish_menu(s, ID_BROWSER_DET);
}

static qa_ui_skin ctrl_skin(void *ctx) { return ((qa_app_startup_menu *)ctx)->skin; }
static double ctrl_now(void *ctx) {
  qa_app_startup_menu *m = ctx;
  return m->o.now(m->o.ctx);
}
static const qa_input_binding *ctrl_bindings(void *ctx, size_t *out_count) {
  (void)ctx; *out_count = 0; return NULL; /* TS bindings: () => [] */
}
static void ctrl_focus(void *ctx, qa_seat_input_focus focus, double now_ms) {
  (void)ctx; (void)focus; (void)now_ms; /* TS focus: () => undefined */
}
static void ctrl_sound(void *ctx, qa_ui_sound sound, qa_seat_id seat) {
  (void)seat;
  qa_app_startup_menu *m = ctx;
  if (m->o.sound) m->o.sound(m->o.ctx, sound);
}
static void ctrl_script(void *ctx, qa_legacy_ui_script script, qa_seat_id seat) {
  (void)ctx; (void)script; (void)seat;
  qa_range_error("Startup menu has no legacy scripts");
}
static const char *ctrl_clipboard(void *ctx) {
  qa_app_startup_menu *m = ctx;
  return m->o.clipboard(m->o.ctx);
}
static double ctrl_measure(void *ctx, const char *text, double scale) {
  return measure((qa_app_startup_menu *)ctx, text, scale);
}
static const qa_app_arena_selection *arena_read(void *ctx) {
  qa_app_startup_menu *m = ctx;
  return m->o.base_arenas ? m->o.base_arenas(m->o.ctx) : NULL;
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
  set_fixed(m->field, sizeof m->field, "product");
  set_fixed(m->native_family, sizeof m->native_family, "q1");
  set_fixed(m->native_edition, sizeof m->native_edition, "classic");
  set_fixed(m->native_skill, sizeof m->native_skill, "1");
  m->host_draft = (qa_app_startup_menu_hosting){"offline", 27910, false, 0};
  set_fixed(m->host_kind, sizeof m->host_kind, "offline");
  set_fixed(m->host_port, sizeof m->host_port, "27910");
  set_fixed(m->browser_sort, sizeof m->browser_sort, "ping-low");
  m->skin = qa_ui_menu_skin(options.skin_font);
  qa_native_ui_options controller_options = {0};
  controller_options.ctx = m;
  controller_options.seat = options.seat;
  controller_options.skin = ctrl_skin;
  controller_options.now = ctrl_now;
  controller_options.bindings = ctrl_bindings;
  controller_options.focus = ctrl_focus;
  controller_options.sound = ctrl_sound;
  controller_options.execute_script = ctrl_script;
  if (options.clipboard) controller_options.clipboard = ctrl_clipboard;
  controller_options.measure_text = ctrl_measure;
  m->controller = qa_native_ui_controller_create(&controller_options);
  if (!m->controller) { free(m->status); free(m); return NULL; }
  register_slot(m, SLOT_MAIN, ID_MAIN, build_main);
  static const struct { const char *name; const char *label; } library_names[6] = {
    {"addons", "Add-ons — Quaddicted"}, {"demos", "Demos"}, {"movies", "Movies"},
    {"configurations", "Configurations"}, {"server-profiles", "Server profiles"},
    {"player-progress", "Achievements and progress"}};
  const qa_ui_library_menu_service *services[6] = {
    options.library_addons, options.library_demos, options.library_movies,
    options.library_configurations, options.library_server_profiles, options.library_player_progress};
  for (int i = 0; i < 6; i++) {
    if (!services[i]) continue;
    char id[96]; snprintf(id, sizeof id, "menu:library:%s", library_names[i].name);
    /* register_library_menu copies the id/title (its header). */
    qa_ui_settings_menus page = qa_ui_register_library_menu(m->controller, id, library_names[i].label, services[i]);
    size_t at = m->library_page_count++;
    m->library_pages[at].name = library_names[i].name;
    m->library_pages[at].label = library_names[i].label;
    m->library_pages[at].root = page.root;
    m->library_pages[at].menus = page;
    m->library_pages[at].has = true;
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
    m->lobby_menus = qa_ui_register_local_lobby_menu(m->controller, lobby_current, m,
                                                     lobby_selection, m, lobby_seats, m);
    m->lobby_menu = m->lobby_menus.root;
    m->has_lobby_menus = true;
  }
  if (options.mods) {
    m->mods_menus = qa_ui_register_mod_menu(m->controller, options.mods);
    m->mods_menu = m->mods_menus.root;
    m->has_mods = true;
  }
  register_slot(m, SLOT_SESSION, ID_SESSION, build_session);
  register_slot(m, SLOT_HOSTING, ID_HOSTING, build_hosting);
  if (options.llm) {
    m->llm_menus = qa_ui_register_llm_settings_menu(m->controller, options.llm);
    m->llm_root = m->llm_menus.root;
    m->has_llm = true;
  }
  register_slot(m, SLOT_OPTIONS, ID_OPTIONS, build_options);
  m->synthetic[0] = (qa_ui_setting_binding){0};
  m->synthetic[0].id = "ui:startup:bindings";
  m->synthetic[0].label = "Bindings (Player 1)";
  m->synthetic[0].category = "input";
  m->synthetic[0].kind = "button";
  m->synthetic[0].ctx = m;
  m->synthetic[0].enabled = bindings_enabled;
  m->synthetic[0].activate = bindings_activate;
  m->synthetic[1] = (qa_ui_setting_binding){0};
  m->synthetic[1].id = "ui:startup:gyro";
  m->synthetic[1].label = "Gyro controls";
  m->synthetic[1].category = "input";
  m->synthetic[1].kind = "button";
  m->synthetic[1].ctx = m;
  m->synthetic[1].enabled = gyro_enabled;
  m->synthetic[1].activate = gyro_activate;
  m->all_binding_count = options.setting_count + 2;
  m->all_bindings = calloc(m->all_binding_count, sizeof *m->all_bindings);
  if (!m->all_bindings) qa_range_error("Out of memory");
  m->all_bindings[0] = m->synthetic[0];
  if (options.setting_count)
    memcpy(&m->all_bindings[1], options.settings, options.setting_count * sizeof *m->all_bindings);
  m->all_bindings[options.setting_count + 1] = m->synthetic[1];
  m->settings_menus = qa_ui_register_settings_menus(m->controller, m->all_bindings,
                                                    m->all_binding_count, NULL, NULL, NULL, NULL);
  m->has_settings = true;
  register_slot(m, SLOT_SOUND, ID_SOUND, build_sound);
  register_slot(m, SLOT_SELECT, ID_SELECT, build_select);
  register_slot(m, SLOT_ROSTER, ID_ROSTER, build_roster);
  register_slot(m, SLOT_LOAD, ID_LOAD, build_load);
  qa_app_startup_menu_ensure_active_menu(m);
  return m;
}

qa_native_ui_controller *qa_app_startup_menu_controller(qa_app_startup_menu *m) {
  if (m->controller) return m->controller;
  return m->host.controller ? m->host.controller(m->host.ctx) : NULL;
}
void qa_app_startup_menu_open_main(qa_app_startup_menu *m) {
  qa_native_ui_controller_open_menu(m->controller, ID_MAIN);
}
void qa_app_startup_menu_open_local_lobby(qa_app_startup_menu *m) {
  if (m->lobby_menu) qa_native_ui_controller_open_menu(m->controller, m->lobby_menu);
}
bool qa_app_startup_menu_is_busy(const qa_app_startup_menu *m) { return m->busy; }
const char *qa_app_startup_menu_status(const qa_app_startup_menu *m) { return m->status ? m->status : ""; }
void qa_app_startup_menu_set_status(qa_app_startup_menu *m, const char *text, bool busy) {
  set_status(m, text, busy);
}
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
void qa_app_startup_menu_bind_gyro(qa_app_startup_menu *m, const qa_ui_gyro_settings *settings) {
  m->gyro_menus = qa_ui_register_gyro_settings_menu(m->controller, settings, NULL);
  m->gyro_menu = m->gyro_menus.root;
  m->has_gyro = true;
}
void qa_app_startup_menu_bind_input(qa_app_startup_menu *m, qa_seat_input *input,
    const qa_ui_binding_action *source, size_t source_count,
    const qa_ui_binding_action *(*get_source)(void *ctx, size_t *out_count), void *source_ctx,
    const qa_ui_binding_reset *reset) {
  m->binding_menus = qa_ui_register_binding_menus(m->controller, input, source, source_count,
                                                  get_source, source_ctx, reset);
  m->binding_menu = m->binding_menus.root;
  m->has_bindings = true;
}

bool qa_app_startup_menu_input(qa_app_startup_menu *m, qa_seat_input_event event) {
  if (m->busy) return true;
  qa_ui_menu_id active = qa_native_ui_controller_active_menu(m->controller);
  bool is_key = streq(event.kind, "key");
  if (streq(active, ID_SESSION) && m->dropdown_active) {
    if (is_key && event.down && event.code == QA_KEY_ESCAPE) {
      if (!retreat_stage(m)) { m->dropdown_active = false; m->preview_armed = false; }
      return true;
    }
    if (is_key && (event.code == QA_KEY_TAB || event.code == QA_KEY_UP || event.code == QA_KEY_DOWN
        || event.code == QA_KEY_LEFT || event.code == QA_KEY_RIGHT || event.code == QA_KEY_PAGE_UP
        || event.code == QA_KEY_PAGE_DOWN || event.code == QA_KEY_HOME || event.code == QA_KEY_END))
      m->preview_armed = true;
    if (is_key && event.down && (event.code == QA_KEY_PAGE_DOWN || event.code == QA_KEY_PAGE_UP)) {
      size_t count = 0;
      const qa_app_startup_menu_row *rows = safe_rows(m, &count);
      const qa_app_startup_menu_row *row = find_row(rows, count, m->dd_stage);
      size_t total = row ? dropdown_entries(m, row, m->dd_has_edition ? m->dd_edition : NULL, true, NULL, 0) : 0;
      double visible = m->dd_has_edition ? 7 : 8;
      double max_scroll = (double)total - visible; if (max_scroll < 0) max_scroll = 0;
      double next = m->dd_scroll + (event.code == QA_KEY_PAGE_DOWN ? visible : -visible);
      if (next > max_scroll) next = max_scroll;
      if (next < 0) next = 0;
      prepare_builder_scroll(m, next);
      return true;
    }
    if (streq(event.kind, "mouse-wheel") && event.delta.y != 0) {
      size_t count = 0;
      const qa_app_startup_menu_row *rows = safe_rows(m, &count);
      const qa_app_startup_menu_row *row = find_row(rows, count, m->dd_stage);
      size_t total = row ? dropdown_entries(m, row, m->dd_has_edition ? m->dd_edition : NULL, true, NULL, 0) : 0;
      double visible = m->dd_has_edition ? 7 : 8;
      double max_scroll = (double)total - visible; if (max_scroll < 0) max_scroll = 0;
      double next = m->dd_scroll + (event.delta.y < 0 ? 1 : -1);
      if (next > max_scroll) next = max_scroll;
      if (next < 0) next = 0;
      prepare_builder_scroll(m, next);
      return true;
    }
  }
  if (streq(event.kind, "mouse-wheel") && event.delta.y != 0 && streq(active, ID_LOAD)) {
    const qa_app_startup_save_list *saves = m->o.saves ? m->o.saves(m->o.ctx) : NULL;
    double max_page = saves ? ceil((double)saves->row_count / 5) - 1 : 0;
    double next = m->save_page + (event.delta.y < 0 ? 1 : -1);
    if (next > max_page) next = max_page;
    if (next < 0) next = 0;
    m->save_page = next;
    return true;
  }
  if (streq(active, ID_MAIN) && is_key && event.code == QA_KEY_ESCAPE) return true;
  bool handled = qa_native_ui_controller_input(m->controller, event);
  qa_app_startup_menu_ensure_active_menu(m);
  return handled;
}

static void dispose_class(qa_app_startup_menu *m) {
  if (m->disposed || !m->controller) return;
  m->disposed = true;
  qa_native_ui_controller_close_all(m->controller);
  for (size_t i = 0; i < m->library_page_count; i++)
    if (m->library_pages[i].has) qa_ui_settings_menus_dispose(&m->library_pages[i].menus);
  qa_app_arena_selection_menu_destroy(m->arena_menu);
  m->arena_menu = NULL;
  if (m->has_lobby_menus) qa_ui_settings_menus_dispose(&m->lobby_menus);
  if (m->has_mods) qa_ui_settings_menus_dispose(&m->mods_menus);
  if (m->has_llm) qa_ui_settings_menus_dispose(&m->llm_menus);
  if (m->has_settings) qa_ui_settings_menus_dispose(&m->settings_menus);
  if (m->has_gyro) qa_ui_settings_menus_dispose(&m->gyro_menus);
  if (m->has_bindings) qa_ui_settings_menus_dispose(&m->binding_menus);
  for (int i = SLOT_COUNT - 1; i >= 0; i--)
    if (m->slots[i].handle >= 0 || m->slots[i].m) qa_native_ui_controller_unregister(m->controller, m->slots[i].handle);
}

/* --- Pre-existing host-delegation API (unchanged signatures). --- */
qa_app_startup_menu *qa_app_startup_menu_create(qa_app_startup_menu_host host) {
  qa_app_startup_menu *m = calloc(1, sizeof *m);
  if (!m) return NULL;
  m->host = host;
  m->legacy = true;
  m->status = xdup("");
  const char *error = host.build_menus(host.ctx);
  if (error != NULL) { free(m->status); free(m); qa_range_error("%s", error); }
  host.prepare_saves(host.ctx);
  return m;
}
void qa_app_startup_menu_destroy(qa_app_startup_menu *m) {
  if (!m) return;
  qa_app_startup_menu_close(m);
  if (m->controller) qa_native_ui_controller_destroy(m->controller);
  for (int i = 0; i < SLOT_COUNT; i++) sc_free(&m->slots[i].sc);
  dd_clear_collapsed(m);
  free(m->status); free(m->monster_classname); free(m->native_preset_id);
  free(m->native_arena); free(m->campaigns); free(m->entries); free(m->all_bindings);
  free(m);
}
void qa_app_startup_menu_graphic_navigation(qa_app_startup_menu *m, const qa_menu_key_event *event) {
  if (m->host.graphic_navigation) m->host.graphic_navigation(m->host.ctx, event);
}
bool qa_app_startup_menu_handle(qa_app_startup_menu *m, const qa_sdl_event *event) {
  if (m->closed || !m->host.handle_event) return false;
  return m->host.handle_event(m->host.ctx, event);
}
void qa_app_startup_menu_command(qa_app_startup_menu *m, const char *command) {
  if (m->host.record_command) m->host.record_command(m->host.ctx, command);
}
void qa_app_startup_menu_set_display_mode(qa_app_startup_menu *m, bool hidden) {
  if (m->host.set_display_mode) m->host.set_display_mode(m->host.ctx, hidden);
}
bool qa_app_startup_menu_open(qa_app_startup_menu *m) {
  if (m->closed) qa_range_error("Startup menu is closed");
  if (!m->legacy) { qa_app_startup_menu_ensure_active_menu(m); return true; }
  return m->host.open(m->host.ctx);
}
void qa_app_startup_menu_pump(qa_app_startup_menu *m) {
  if (m->closed) qa_range_error("Startup menu is closed");
  if (m->host.pump) m->host.pump(m->host.ctx);
}
void qa_app_startup_menu_prepare_frame(qa_app_startup_menu *m, qa_render_frame frame) {
  (void)m; (void)frame;
}
void qa_app_startup_menu_navigate(qa_app_startup_menu *m, const char *target) {
  if (m->host.navigate) m->host.navigate(m->host.ctx, target);
  else if (m->controller) qa_native_ui_controller_open_menu(m->controller, target);
}
void qa_app_startup_menu_close(qa_app_startup_menu *m) {
  if (!m->legacy) { dispose_class(m); m->closed = true; return; }
  if (m->closed) return;
  m->closed = true;
  m->host.close_saves(m->host.ctx);
  m->host.close_menu(m->host.ctx);
  m->host.close_services(m->host.ctx);
}





