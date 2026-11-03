/* Port of src/network/q3/browser-view.ts. GPL-2.0-or-later. */
#include "qa/network/q3/browser_view.h"
#include "qa/core/commands/text.h"
#include "qa/core/common_error.h"
#include "qa/core/cvars/info.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *xstrdup(const char *s) { size_t n = strlen(s); char *r = malloc(n + 1); if (r) memcpy(r, s, n + 1); return r; }
static char *xstrndup(const char *s, size_t n) { char *r = malloc(n + 1); if (r) { memcpy(r, s, n); r[n] = 0; } return r; }
typedef struct { bool has_address; qa_q3_mirror_network_address address; char *name; int64_t visible, ping; } row_t;
typedef struct {
  row_t *rows; size_t capacity;
  char **seen; size_t seen_count, seen_cap;
  qa_q3_mirror_network_address *overflow; size_t overflow_count, overflow_cap;
  int64_t count, generation, reset_generation; bool pending;
} list_t;
typedef struct { bool has_address; qa_q3_mirror_network_address address; qa_q3_mirror_discovery_handle handle; double start; char *info; bool published; } ping_slot_t;
typedef struct { bool has_address; qa_q3_mirror_network_address address; qa_q3_mirror_discovery_handle handle; double start; bool retrieved, print; } status_slot_t;
struct qa_q3_browser_view {
  qa_q3_browser_view_options options;
  list_t lists[4];
  ping_slot_t pings[32];
  status_slot_t statuses[16];
  bool closed;
  int64_t generation;
};
static size_t capacity_of(int64_t source) { return source == 2 ? 4096 : (source == 0 || source == 1 || source == 3) ? 128 : 0; }
static void row_clear(row_t *r) {
  qa_q3_mirror_address_destroy(&r->address); r->has_address = false;
  free(r->name); r->name = xstrdup("");
}
static void row_copy(row_t *dst, const row_t *src) {
  row_clear(dst);
  dst->has_address = src->has_address;
  if (src->has_address) qa_q3_mirror_address_copy(&dst->address, &src->address);
  free(dst->name); dst->name = xstrdup(src->name ? src->name : "");
  dst->visible = src->visible; dst->ping = src->ping;
}
static void row_set_address(row_t *r, const qa_q3_mirror_network_address *a) {
  qa_q3_mirror_address_destroy(&r->address);
  memset(&r->address, 0, sizeof r->address);
  qa_q3_mirror_address_copy(&r->address, a);
  r->has_address = true;
}
static bool addr_same(const row_t *r, const qa_q3_mirror_network_address *a) {
  if (!r->has_address || !a) return false;
  char *k1 = qa_q3_mirror_address_key(&r->address, true);
  char *k2 = qa_q3_mirror_address_key(a, true);
  bool eq = k1 && k2 && strcmp(k1, k2) == 0;
  free(k1); free(k2);
  return eq;
}
static bool rows_same(const row_t *a, const qa_q3_mirror_network_address *b) { return addr_same(a, b); }
static char *copy_string(const char *text, int64_t size) {
  if (size < 1) qa_common_error(QA_COMMON_ERROR_FATAL, "Q_strncpyz: destsize < 1");
  size_t n = strlen(text);
  if ((int64_t)n > size - 1) n = (size_t)(size - 1);
  return xstrndup(text, n);
}
static int compare_text(const char *left, const char *right) {
  for (size_t i = 0;; i++) {
    int a = (int8_t)(unsigned char)left[i], b = (int8_t)(unsigned char)right[i];
    if (left[i] == 0) a = 0;
    if (right[i] == 0) b = 0;
    if (a != b) {
      if (a >= 97 && a <= 122) a -= 32;
      if (b >= 97 && b <= 122) b -= 32;
      if (a != b) return a < b ? -1 : 1;
    }
    if (a == 0) return 0;
  }
}
static void view_entry_at(qa_q3_browser_view *v, int64_t generation) {
  if (v->closed || generation != v->generation) qa_raise("Error", 0, 0, "Q3 browser view operation completed after close");
  v->options.browser.assert_open(v->options.browser.ctx);
}
static void view_entry(qa_q3_browser_view *v) { view_entry_at(v, v->generation); }
/* Cleanup adapters: host callbacks (assertOpen, resolve, print, save) raise, and
 * a catching caller must lose nothing held here. */
static void release_address(void *arg) { qa_q3_mirror_address_destroy(arg); }
static void release_string(void *arg) { char **text = arg; free(*text); *text = NULL; }
typedef struct { qa_q3_browser_view *v; } info_print_t;
static void info_print(void *ctx, const char *text) {
  info_print_t *p = ctx;
  if (p->v->options.print) p->v->options.print(p->v->options.print_ctx, text);
  view_entry(p->v);
}
/* TS info(pairs): setInfoValue over each pair in order; the print hook reenters entry(). */
static void set_info(qa_q3_browser_view *v, char **result, const char *key, const char *value) {
  info_print_t pa = { v };
  qa_info_options o = { QA_COMMAND_DIALECT_Q3, 1024, QA_CVAR_INFO_CLIENT_USERINFO, false, info_print, &pa };
  char *next = qa_set_info_value(*result, key, value, &o);
  if (!next) qa_range_error("browser info allocation failed");
  free(*result); *result = next;
}
static bool seen_has(const list_t *l, const char *key) {
  for (size_t i = 0; i < l->seen_count; i++) if (strcmp(l->seen[i], key) == 0) return true;
  return false;
}
static void seen_add(list_t *l, const char *key) {
  if (seen_has(l, key)) return;
  if (l->seen_count == l->seen_cap) {
    size_t nc = l->seen_cap ? l->seen_cap * 2 : 16;
    char **ns = realloc(l->seen, nc * sizeof *ns);
    if (!ns) qa_range_error("browser seen allocation failed");
    l->seen = ns; l->seen_cap = nc;
  }
  l->seen[l->seen_count++] = xstrdup(key);
}
static void seen_delete(list_t *l, const char *key) {
  for (size_t i = 0; i < l->seen_count; i++) if (strcmp(l->seen[i], key) == 0) {
    free(l->seen[i]);
    memmove(l->seen + i, l->seen + i + 1, (l->seen_count - i - 1) * sizeof(char *));
    l->seen_count--; return;
  }
}
static void seen_clear(list_t *l) {
  for (size_t i = 0; i < l->seen_count; i++) free(l->seen[i]);
  l->seen_count = 0;
}
static bool key_in(const char *key, char **keys, size_t n) {
  for (size_t i = 0; i < n; i++) if (strcmp(keys[i], key) == 0) return true;
  return false;
}
static list_t *view_list(qa_q3_browser_view *v, int64_t source) {
  view_entry(v);
  if (source != 0 && source != 1 && source != 2 && source != 3) return NULL;
  list_t *list = &v->lists[source];
  qa_q3_browser_list_state state = v->options.browser.q3_list(v->options.browser.ctx, source);
  list->pending = state.pending;
  if (list->generation != state.generation) {
    bool resetting = state.reset_generation != list->reset_generation;
    size_t prev_count = resetting ? 0 : (size_t)list->count;
    row_t *previous = calloc(prev_count ? prev_count : 1, sizeof *previous);
    if (!previous) qa_range_error("browser list allocation failed");
    for (size_t i = 0; i < prev_count; i++) { previous[i].name = xstrdup(""); row_copy(&previous[i], &list->rows[i]); }
    if (resetting) {
      seen_clear(list);
      for (size_t i = 0; i < list->overflow_count; i++) qa_q3_mirror_address_destroy(&list->overflow[i]);
      list->overflow_count = 0;
      for (size_t i = 0; i < list->capacity; i++) {
        qa_q3_mirror_address_destroy(&list->rows[i].address);
        list->rows[i].has_address = false;
        free(list->rows[i].name); list->rows[i].name = xstrdup("");
        list->rows[i].ping = 0;
      }
    }
    int64_t count = 0;
    if (source == 2) {
      size_t addr_count = state.address_count < 8192 ? state.address_count : 8192;
      char **keys = calloc(addr_count ? addr_count : 1, sizeof(char *));
      if (!keys) qa_range_error("browser list allocation failed");
      for (size_t i = 0; i < addr_count; i++) keys[i] = qa_q3_mirror_address_key(&state.addresses[i], true);
      for (size_t i = 0; i < list->seen_count;) {
        if (!key_in(list->seen[i], keys, addr_count)) seen_delete(list, list->seen[i]); else i++;
      }
      for (size_t i = 0; i < prev_count; i++) {
        if (!previous[i].has_address) continue;
        char *k = qa_q3_mirror_address_key(&previous[i].address, true);
        bool has = k && key_in(k, keys, addr_count);
        free(k);
        if (has) row_copy(&list->rows[count++], &previous[i]);
      }
      /* overflow filter: keys has and not in active rows */
      size_t kept = 0;
      for (size_t i = 0; i < list->overflow_count; i++) {
        char *k = qa_q3_mirror_address_key(&list->overflow[i], true);
        bool active = false;
        for (int64_t j = 0; j < count; j++) if (rows_same(&list->rows[j], &list->overflow[i])) active = true;
        bool keep = k && key_in(k, keys, addr_count) && !active;
        free(k);
        if (keep) {
          if (kept != i) { list->overflow[kept] = list->overflow[i]; memset(&list->overflow[i], 0, sizeof list->overflow[i]); }
          kept++;
        } else qa_q3_mirror_address_destroy(&list->overflow[i]);
      }
      list->overflow_count = kept;
      for (size_t i = 0; i < addr_count; i++) {
        if (seen_has(list, keys[i])) continue;
        seen_add(list, keys[i]);
        if ((size_t)count < list->capacity) {
          int64_t visible = list->rows[count].visible;
          row_clear(&list->rows[count]);
          row_set_address(&list->rows[count], &state.addresses[i]);
          list->rows[count].visible = visible; list->rows[count].ping = -1;
          count++;
        } else if (list->overflow_count < 4096) {
          if (list->overflow_count == list->overflow_cap) {
            size_t nc = list->overflow_cap ? list->overflow_cap * 2 : 16;
            qa_q3_mirror_network_address *no = realloc(list->overflow, nc * sizeof *no);
            if (!no) qa_range_error("browser overflow allocation failed");
            list->overflow = no; list->overflow_cap = nc;
          }
          memset(&list->overflow[list->overflow_count], 0, sizeof list->overflow[0]);
          qa_q3_mirror_address_copy(&list->overflow[list->overflow_count++], &state.addresses[i]);
        }
      }
      for (size_t i = 0; i < addr_count; i++) free(keys[i]);
      free(keys);
    } else {
      size_t addr_count = state.address_count < list->capacity ? state.address_count : list->capacity;
      for (size_t i = 0; i < addr_count; i++) {
        const row_t *old = NULL;
        for (size_t j = 0; j < prev_count; j++) if (rows_same(&previous[j], &state.addresses[i])) { old = &previous[j]; break; }
        int64_t visible = list->rows[count].visible;
        if (old) row_copy(&list->rows[count], old);
        else {
          row_clear(&list->rows[count]);
          row_set_address(&list->rows[count], &state.addresses[i]);
          list->rows[count].visible = visible; list->rows[count].ping = -1;
        }
        count++;
      }
    }
    list->count = count; list->generation = state.generation; list->reset_generation = state.reset_generation;
    for (size_t i = 0; i < prev_count; i++) { qa_q3_mirror_address_destroy(&previous[i].address); free(previous[i].name); }
    free(previous);
  }
  return list;
}
static row_t *view_row(qa_q3_browser_view *v, int64_t source, int64_t index) {
  list_t *list = view_list(v, source);
  if (!list || index < 0 || (size_t)index >= list->capacity) return NULL;
  return &list->rows[index];
}
static const qa_server_status *row_status(qa_q3_browser_view *v, const row_t *row) {
  if (!row->has_address) return NULL;
  const qa_server_status *status = NULL;
  qa_q3_mirror_server_browser *core = &v->options.browser.q3_core;
  if (!core->entry_status(core->ctx, &row->address, &status)) return NULL;
  return status;
}
static char *row_name(qa_q3_browser_view *v, const row_t *row) {
  const qa_server_status *status = row_status(v, row);
  const char *base = NULL;
  char *key = NULL;
  if (status && status->name && status->name[0]) base = status->name;
  else if (row->name && row->name[0]) base = row->name;
  else if (!row->has_address) base = "";
  else { key = qa_q3_mirror_address_key(&row->address, true); base = key; }
  char *text = qa_source_command_text(base ? base : "");
  free(key);
  if (!text) return xstrdup("");
  if (strlen(text) > 31) text[31] = 0;
  return text;
}
static const char *rule_get(const qa_server_status *s, const char *key) {
  if (!s) return "";
  for (size_t i = 0; i < s->rules.count; i++) if (strcmp(s->rules.entries[i].key, key) == 0) return s->rules.entries[i].value;
  return "";
}
static char *slice31(const char *s) {
  size_t n = strlen(s); if (n > 31) n = 31;
  return xstrndup(s, n);
}
bool qa_q3_browser_view_init(qa_q3_browser_view **out, const qa_q3_browser_view_options *options) {
  qa_q3_browser_view *v = calloc(1, sizeof *v);
  if (!v) return false;
  v->options = *options;
  for (int s = 0; s < 4; s++) {
    list_t *l = &v->lists[s];
    l->capacity = capacity_of(s);
    l->rows = calloc(l->capacity, sizeof *l->rows);
    if (!l->rows) { qa_q3_browser_view_destroy(v); return false; }
    for (size_t i = 0; i < l->capacity; i++) l->rows[i].name = xstrdup("");
    l->generation = -1; l->reset_generation = -1;
  }
  for (int i = 0; i < 32; i++) v->pings[i].info = xstrdup("");
  for (int i = 0; i < 16; i++) v->statuses[i].retrieved = true;
  *out = v; return true;
}
void qa_q3_browser_view_destroy(qa_q3_browser_view *v) {
  if (!v) return;
  for (int s = 0; s < 4; s++) {
    list_t *l = &v->lists[s];
    for (size_t i = 0; i < l->capacity; i++) { qa_q3_mirror_address_destroy(&l->rows[i].address); free(l->rows[i].name); }
    free(l->rows);
    seen_clear(l); free(l->seen);
    for (size_t i = 0; i < l->overflow_count; i++) qa_q3_mirror_address_destroy(&l->overflow[i]);
    free(l->overflow);
  }
  for (int i = 0; i < 32; i++) { qa_q3_mirror_address_destroy(&v->pings[i].address); free(v->pings[i].info); }
  for (int i = 0; i < 16; i++) qa_q3_mirror_address_destroy(&v->statuses[i].address);
  free(v);
}
const qa_q3_browser_view_options *qa_q3_browser_view_get_options(const qa_q3_browser_view *v) { return &v->options; }
int64_t qa_q3_browser_view_get_server_count(qa_q3_browser_view *v, int64_t source) {
  list_t *list = view_list(v, source);
  return !list ? 0 : list->pending ? -1 : list->count;
}
char *qa_q3_browser_view_get_server_address_string(qa_q3_browser_view *v, int64_t source, int64_t index, int64_t size, qa_q3_browser_write_fn write, void *write_ctx) {
  row_t *row = view_row(v, source, index);
  if (!row) return xstrdup("");
  char *key = row->has_address ? qa_q3_mirror_address_key(&row->address, true) : NULL;
  if (row->has_address && !key) qa_range_error("browser address allocation failed");
  const char *text = row->has_address ? key : "bot";
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_string, &key);
  if (write) write(write_ctx, text);
  char *r = copy_string(text, size);
  qa_cleanup_pop(&guard);
  free(key);
  return r;
}
#define INFO_PAIRS 12
typedef struct { char *values[INFO_PAIRS]; char *result; } info_scratch;
static void info_scratch_release(void *arg) {
  info_scratch *sc = arg;
  for (int i = 0; i < INFO_PAIRS; i++) { free(sc->values[i]); sc->values[i] = NULL; }
  free(sc->result); sc->result = NULL;
}
static char *owned_number(int64_t value) {
  char num[32];
  snprintf(num, sizeof num, "%lld", (long long)value);
  return xstrdup(num);
}
static char *owned_slice31_source(const char *text) {
  char *t = qa_source_command_text(text);
  if (!t) return NULL;
  if (strlen(t) > 31) t[31] = 0;
  return t;
}
char *qa_q3_browser_view_get_server_info(qa_q3_browser_view *v, int64_t source, int64_t index, int64_t size, qa_q3_browser_write_fn write, void *write_ctx) {
  row_t *row = view_row(v, source, index);
  if (!row) return xstrdup("");
  static const char *const keys[INFO_PAIRS] = { "hostname", "mapname", "clients", "sv_maxclients", "ping", "minping",
    "maxping", "game", "gametype", "nettype", "addr", "punkbuster" };
  info_scratch sc;
  memset(&sc, 0, sizeof sc);
  qa_cleanup guard;
  qa_cleanup_push(&guard, info_scratch_release, &sc);
  /* TS builds every pair (status lookups included) before the first setInfoValue. */
  const qa_server_status *status = row_status(v, row);
  sc.values[0] = row_name(v, row);
  sc.values[1] = owned_slice31_source(status ? status->map : "");
  sc.values[2] = owned_number(status ? (int64_t)status->players : 0);
  sc.values[3] = owned_number(status ? (int64_t)status->max_players : 0);
  sc.values[4] = owned_number(row->ping);
  sc.values[5] = owned_number(qa_native_atoi(rule_get(status, "minping")));
  sc.values[6] = owned_number(qa_native_atoi(rule_get(status, "maxping")));
  sc.values[7] = owned_slice31_source(rule_get(status, "game"));
  sc.values[8] = owned_number(qa_native_atoi(rule_get(status, "gametype")));
  sc.values[9] = xstrdup(row->has_address ? "1" : "0");
  sc.values[10] = row->has_address ? qa_q3_mirror_address_key(&row->address, true) : xstrdup("bot");
  sc.values[11] = owned_number(qa_native_atoi(rule_get(status, "punkbuster")));
  sc.result = xstrdup("");
  for (int i = 0; i < INFO_PAIRS; i++) if (!sc.values[i]) qa_range_error("browser info allocation failed");
  if (!sc.result) qa_range_error("browser info allocation failed");
  for (int i = 0; i < INFO_PAIRS; i++) set_info(v, &sc.result, keys[i], sc.values[i]);
  if (write) write(write_ctx, sc.result);
  char *r = copy_string(sc.result, size);
  qa_cleanup_pop(&guard);
  info_scratch_release(&sc);
  return r;
}
int64_t qa_q3_browser_view_get_server_ping(qa_q3_browser_view *v, int64_t source, int64_t index) {
  row_t *row = view_row(v, source, index);
  return row ? row->ping : -1;
}
void qa_q3_browser_view_mark_server_visible_value(qa_q3_browser_view *v, int64_t source, int64_t index, int64_t visible) {
  list_t *list = view_list(v, source);
  if (!list) return;
  if (index == -1) for (size_t i = 0; i < list->capacity; i++) list->rows[i].visible = visible;
  else if (index >= 0 && (size_t)index < list->capacity) list->rows[index].visible = visible;
}
int64_t qa_q3_browser_view_server_visibility_value(qa_q3_browser_view *v, int64_t source, int64_t index) {
  row_t *row = view_row(v, source, index);
  return row ? row->visible : 0;
}
void qa_q3_browser_view_reset_pings(qa_q3_browser_view *v, int64_t source) {
  list_t *list = view_list(v, source);
  if (list) for (size_t i = 0; i < list->capacity; i++) list->rows[i].ping = -1;
}
int64_t qa_q3_browser_view_compare_servers(qa_q3_browser_view *v, int64_t source, int64_t key, int64_t direction, int64_t first, int64_t second) {
  row_t *a = view_row(v, source, first), *b = view_row(v, source, second);
  if (!a || !b) return 0;
  int64_t compared = 0;
  if (key == 0) {
    char *na = row_name(v, a), *nb = row_name(v, b);
    if (!na || !nb) { free(na); free(nb); qa_range_error("browser name allocation failed"); }
    compared = compare_text(na, nb); free(na); free(nb);
  } else if (key == 1) {
    const qa_server_status *sa = row_status(v, a), *sb = row_status(v, b);
    char *ma = slice31(sa ? sa->map : ""), *mb = slice31(sb ? sb->map : "");
    if (!ma || !mb) { free(ma); free(mb); qa_range_error("browser map allocation failed"); }
    compared = compare_text(ma, mb); free(ma); free(mb);
  } else {
    int64_t left = 0, right = 0;
    if (key == 2 || key == 3) {
      const qa_server_status *sa = row_status(v, a);
      left = key == 2 ? (sa ? (int64_t)sa->players : 0) : qa_native_atoi(rule_get(sa, "gametype"));
      const qa_server_status *sb = row_status(v, b);
      right = key == 2 ? (sb ? (int64_t)sb->players : 0) : qa_native_atoi(rule_get(sb, "gametype"));
    } else if (key == 4) { left = a->ping; right = b->ping; }
    compared = left < right ? -1 : left > right ? 1 : 0;
  }
  return direction == 0 || compared == 0 ? compared : -compared;
}
int64_t qa_q3_browser_view_add_server(qa_q3_browser_view *v, int64_t source, qa_q3_browser_string_fn name, void *name_ctx, qa_q3_browser_string_fn address, void *address_ctx) {
  list_t *list = view_list(v, source);
  if (!list || (size_t)list->count >= list->capacity) return -1;
  int64_t generation = v->generation;
  qa_q3_mirror_network_address resolved; memset(&resolved, 0, sizeof resolved);
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_address, &resolved);
  bool ok = v->options.browser.resolve_q3(v->options.browser.ctx, address(address_ctx), &resolved);
  view_entry_at(v, generation);
  if (!ok) qa_raise("Error", 0, 0, "LAN_AddServer: address resolution failed");
  for (int64_t i = 0; i < list->count; i++)
    if (rows_same(&list->rows[i], &resolved)) { qa_cleanup_pop(&guard); qa_q3_mirror_address_destroy(&resolved); return 0; }
  row_t *row = &list->rows[list->count];
  row_set_address(row, &resolved);
  char *text = qa_source_command_text(name(name_ctx));
  free(row->name); row->name = text ? slice31(text) : xstrdup(""); free(text);
  row->visible = 1;
  v->options.browser.add_q3(v->options.browser.ctx, source, &resolved);
  list->count++;
  char *k = qa_q3_mirror_address_key(&resolved, true);
  if (k) { seen_add(list, k); free(k); }
  list->generation = v->options.browser.q3_list(v->options.browser.ctx, source).generation;
  qa_cleanup_pop(&guard);
  qa_q3_mirror_address_destroy(&resolved);
  return 1;
}
void qa_q3_browser_view_remove_server(qa_q3_browser_view *v, int64_t source, qa_q3_browser_string_fn address, void *address_ctx) {
  list_t *list = view_list(v, source);
  if (!list) return;
  int64_t generation = v->generation;
  qa_q3_mirror_network_address resolved; memset(&resolved, 0, sizeof resolved);
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_address, &resolved);
  bool ok = v->options.browser.resolve_q3(v->options.browser.ctx, address(address_ctx), &resolved);
  view_entry_at(v, generation);
  if (!ok) qa_raise("Error", 0, 0, "LAN_RemoveServer: address resolution failed");
  int64_t index = -1;
  for (int64_t i = 0; i < list->count; i++) if (rows_same(&list->rows[i], &resolved)) { index = i; break; }
  if (index < 0) { qa_cleanup_pop(&guard); qa_q3_mirror_address_destroy(&resolved); return; }
  v->options.browser.remove_q3(v->options.browser.ctx, source, &resolved);
  for (int64_t next = index; next < list->count - 1; next++) row_copy(&list->rows[next], &list->rows[next + 1]);
  list->count--;
  char *k = qa_q3_mirror_address_key(&resolved, true);
  if (k) { seen_delete(list, k); free(k); }
  list->generation = v->options.browser.q3_list(v->options.browser.ctx, source).generation;
  qa_cleanup_pop(&guard);
  qa_q3_mirror_address_destroy(&resolved);
}
static void view_release(qa_q3_browser_view *v, qa_q3_mirror_discovery_handle handle) {
  if (handle != 0) v->options.browser.q3_core.release_request(v->options.browser.q3_core.ctx, handle);
}
static void publish_ping(qa_q3_browser_view *v, const qa_q3_mirror_network_address *address, int64_t ping) {
  for (int s = 0; s < 4; s++) {
    list_t *list = view_list(v, s);
    if (list) for (size_t i = 0; i < list->capacity; i++) if (rows_same(&list->rows[i], address)) list->rows[i].ping = ping;
  }
}
int64_t qa_q3_browser_view_get_ping_queue_count(qa_q3_browser_view *v) {
  view_entry(v);
  int64_t n = 0;
  for (int i = 0; i < 32; i++) if (v->pings[i].has_address) n++;
  return n;
}
void qa_q3_browser_view_clear_ping(qa_q3_browser_view *v, int64_t index) {
  view_entry(v);
  if (index < 0 || index >= 32) return;
  ping_slot_t *slot = &v->pings[index];
  view_release(v, slot->handle);
  qa_q3_mirror_address_destroy(&slot->address); slot->has_address = false; slot->handle = 0;
}
static ping_slot_t *ping_slot_at(qa_q3_browser_view *v, int64_t index) {
  if (index < 0 || index >= 32) qa_range_error("Server browser source index %lld outside %d", (long long)index, 32);
  return &v->pings[index];
}
typedef struct ping_info_scope { qa_q3_browser_view *v; const char *text; char *info; } ping_info_scope;
static void ping_info_body(void *ctx) {
  ping_info_scope *scope = ctx;
  info_print_t pa = { scope->v };
  qa_info_options o = { QA_COMMAND_DIALECT_Q3, 1024, QA_CVAR_INFO_CLIENT_USERINFO, false, info_print, &pa };
  scope->info = qa_set_info_value(scope->text, "nettype", "1", &o);
}
static int64_t ping_time(qa_q3_browser_view *v, ping_slot_t *slot) {
  qa_q3_mirror_request_result result;
  bool has = slot->handle != 0 && v->options.browser.q3_core.request_result(v->options.browser.q3_core.ctx, slot->handle, &result);
  if (has && result.kind == QA_Q3_REQ_COMPLETED) {
    size_t cap = 256, len = 0;
    char *text = calloc(cap, 1);
    if (!text) qa_range_error("ping info allocation failed");
    if (result.status) for (size_t i = 0; i < result.status->rules.count; i++) {
      size_t need = strlen(result.status->rules.entries[i].key) + strlen(result.status->rules.entries[i].value) + 3;
      if (len + need >= cap) {
        while (len + need >= cap) cap *= 2;
        char *grown = realloc(text, cap);
        if (!grown) { free(text); qa_range_error("ping info allocation failed"); }
        text = grown;
      }
      len += (size_t)snprintf(text + len, cap - len, "\\%s\\%s", result.status->rules.entries[i].key, result.status->rules.entries[i].value);
    }
    /* Any server on the network can send this text, so one too long for an info
     * string just leaves the row blank (TS catches RangeError only). */
    ping_info_scope scope = { v, text, NULL };
    qa_caught_error caught;
    bool ok = qa_try(ping_info_body, &scope, &caught);
    free(text);
    if (!ok && strcmp(caught.name, "RangeError") != 0) qa_rethrow(&caught);
    char *info = ok && scope.info ? scope.info : xstrdup("");
    if (!info) qa_range_error("ping info allocation failed");
    free(slot->info); slot->info = info;
    return (int64_t)trunc(result.ping_milliseconds) + 1;
  }
  return 0;
}
bool qa_q3_browser_view_get_ping(qa_q3_browser_view *v, int64_t index, int64_t size, qa_q3_browser_write_nullable_fn write, void *write_ctx, char **address_out, int64_t *time_out) {
  view_entry(v);
  ping_slot_t *slot = ping_slot_at(v, index);
  if (!slot->has_address) {
    if (write) write(write_ctx, NULL);
    *address_out = xstrdup(""); *time_out = 0;
    return *address_out != NULL;
  }
  char *text = qa_q3_mirror_address_key(&slot->address, true);
  if (!text) qa_range_error("browser address allocation failed");
  qa_cleanup text_guard;
  qa_cleanup_push(&text_guard, release_string, &text);
  if (write) write(write_ctx, text);
  char *address = copy_string(text, size);
  qa_cleanup_pop(&text_guard);
  free(text);
  /* The copied address is returned; release it if the timing callbacks raise. */
  qa_cleanup address_guard;
  qa_cleanup_push(&address_guard, release_string, &address);
  int64_t measured = ping_time(v, slot);
  int64_t elapsed = (int64_t)trunc(v->options.now(v->options.ctx) - slot->start);
  /* TS Math.max(100, maxPing()): NaN stays NaN, so the comparison fails and the elapsed time is reported. */
  double max_ping = v->options.max_ping(v->options.ctx);
  double threshold = isnan(max_ping) ? max_ping : max_ping > 100 ? max_ping : 100;
  *time_out = measured != 0 ? measured : (double)elapsed < threshold ? 0 : elapsed;
  publish_ping(v, &slot->address, measured);
  qa_cleanup_pop(&address_guard);
  *address_out = address;
  return true;
}
char *qa_q3_browser_view_source_ping_info(qa_q3_browser_view *v, int64_t index, int64_t size, qa_q3_browser_write_fn write, void *write_ctx) {
  view_entry(v);
  ping_slot_t *slot = ping_slot_at(v, index);
  if (!slot->has_address) return NULL;
  ping_time(v, slot);
  if (write) write(write_ctx, slot->info);
  return copy_string(slot->info, size);
}
static void start_ping(qa_q3_browser_view *v, ping_slot_t *slot, const qa_q3_mirror_network_address *address) {
  view_release(v, slot->handle);
  qa_q3_mirror_address_destroy(&slot->address);
  memset(&slot->address, 0, sizeof slot->address);
  qa_q3_mirror_address_copy(&slot->address, address);
  slot->has_address = true;
  slot->start = v->options.now(v->options.ctx);
  slot->published = false;
  /* A failed send (handle 0, TS null) leaves the slot pending to time out like a
   * lost packet; the source ignores send failures. */
  slot->handle = v->options.browser.q3_core.request(v->options.browser.q3_core.ctx, address, slot->start, QA_Q3_DISCOVERY_INFO);
}
bool qa_q3_browser_view_update_visible_pings(qa_q3_browser_view *v, int64_t source) {
  list_t *list = view_list(v, source);
  if (!list) return false;
  int64_t count = qa_q3_browser_view_get_ping_queue_count(v);
  bool active = false;
  int64_t row_count = list->count;
  for (int64_t i = 0; i < row_count; i++) {
    row_t *row = &list->rows[i];
    if (row->visible == 0) continue;
    if (source == 2 && row->ping == 0) {
      if (list->overflow_count > 0) {
        qa_q3_mirror_network_address addr = list->overflow[--list->overflow_count];
        row_set_address(row, &addr);
        qa_q3_mirror_address_destroy(&addr);
        free(row->name); row->name = xstrdup("");
        row->ping = -1;
      }
      continue;
    }
    if (row->ping != -1 || !row->has_address) continue;
    bool queued = false;
    for (int j = 0; j < 32; j++) if (v->pings[j].has_address && rows_same(row, &v->pings[j].address)) queued = true;
    if (queued) continue;
    /* cl_main.c tests the queue limit only for rows that need a ping, so later
     * ping 0 rows still consume overflow addresses. */
    if (count >= 32) break;
    ping_slot_t *slot = NULL;
    for (int j = 0; j < 32; j++) if (!v->pings[j].has_address) { slot = &v->pings[j]; break; }
    if (!slot) break;
    start_ping(v, slot, &row->address);
    count++; active = true;
  }
  if (count != 0) active = true;
  for (int i = 0; i < 32; i++) if (v->pings[i].has_address) {
    char *addr = NULL; int64_t time = 0;
    qa_q3_browser_view_get_ping(v, i, 1024, NULL, NULL, &addr, &time);
    free(addr);
    if (time != 0) qa_q3_browser_view_clear_ping(v, i);
  }
  return active;
}
void qa_q3_browser_view_local_servers(qa_q3_browser_view *v) {
  view_entry(v);
  v->options.browser.scan_q3(v->options.browser.ctx);
  view_list(v, 0);
}
typedef struct { qa_q3_browser_view *v; int64_t generation; } gen_assert_t;
static void gen_assert(void *ctx) { gen_assert_t *g = ctx; view_entry_at(g->v, g->generation); }
void qa_q3_browser_view_global_servers(qa_q3_browser_view *v, int64_t source, const char *remote, int64_t protocol, const char *const *keywords, size_t keyword_count) {
  view_entry(v);
  gen_assert_t ga = { v, v->generation };
  v->options.browser.request_q3_master(v->options.browser.ctx, source, remote, protocol, keywords, keyword_count, gen_assert, &ga);
  view_entry_at(v, ga.generation);
  view_list(v, source);
}
void qa_q3_browser_view_ping(qa_q3_browser_view *v, const char *address) {
  view_entry(v);
  int64_t generation = v->generation;
  qa_q3_mirror_network_address resolved; memset(&resolved, 0, sizeof resolved);
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_address, &resolved);
  bool ok = v->options.browser.resolve_q3(v->options.browser.ctx, address, &resolved);
  view_entry_at(v, generation);
  if (!ok) { qa_cleanup_pop(&guard); qa_q3_mirror_address_destroy(&resolved); return; }
  double now = v->options.now(v->options.ctx);
  ping_slot_t *slot = NULL;
  for (int i = 0; i < 32 && !slot; i++) {
    ping_slot_t *s = &v->pings[i];
    if (!s->has_address) { slot = s; break; }
    int64_t t = ping_time(v, s);
    if (t == 0 ? (now - s->start >= 500) : (ping_time(v, s) >= 500)) slot = s;
  }
  if (!slot) {
    slot = &v->pings[0];
    for (int i = 1; i < 32; i++) if (v->pings[i].start < slot->start) slot = &v->pings[i];
  }
  start_ping(v, slot, &resolved);
  publish_ping(v, &resolved, 0);
  qa_cleanup_pop(&guard);
  qa_q3_mirror_address_destroy(&resolved);
}
static bool status_same(const status_slot_t *s, const qa_q3_mirror_network_address *a) {
  if (!s->has_address) return false;
  char *k1 = qa_q3_mirror_address_key(&s->address, true);
  char *k2 = qa_q3_mirror_address_key(a, true);
  bool eq = k1 && k2 && strcmp(k1, k2) == 0;
  free(k1); free(k2);
  return eq;
}
static status_slot_t *status_slot_for(qa_q3_browser_view *v, const qa_q3_mirror_network_address *address) {
  for (int i = 0; i < 16; i++) if (status_same(&v->statuses[i], address)) return &v->statuses[i];
  for (int i = 0; i < 16; i++) if (v->statuses[i].retrieved) return &v->statuses[i];
  status_slot_t *oldest = &v->statuses[0];
  for (int i = 1; i < 16; i++) if (v->statuses[i].start < oldest->start) oldest = &v->statuses[i];
  return oldest;
}
/* Growable text with the TS `.slice(0, 8191)` steps applied where TS applies them. */
typedef struct { char *text; size_t len, cap; } status_buf;
static void status_append(status_buf *b, const char *text, size_t n) {
  if (b->len + n + 1 > b->cap) {
    size_t cap = b->cap ? b->cap : 256;
    while (b->len + n + 1 > cap) cap *= 2;
    char *grown = realloc(b->text, cap);
    if (!grown) qa_range_error("browser status allocation failed");
    b->text = grown; b->cap = cap;
  }
  memcpy(b->text + b->len, text, n); b->len += n; b->text[b->len] = 0;
}
static void status_slice(status_buf *b, size_t limit) {
  if (b->len > limit) { b->len = limit; b->text[limit] = 0; }
}
static char *status_text(const qa_server_status *status) {
  status_buf b = { NULL, 0, 0 };
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_string, &b.text);
  status_append(&b, "", 0);
  for (size_t i = 0; i < status->rules.count; i++) {
    status_append(&b, "\\", 1);
    status_append(&b, status->rules.entries[i].key, strlen(status->rules.entries[i].key));
    status_append(&b, "\\", 1);
    status_append(&b, status->rules.entries[i].value, strlen(status->rules.entries[i].value));
  }
  status_slice(&b, 8191);
  status_append(&b, "\\", 1);
  for (size_t i = 0; i < status->player_count; i++) {
    const qa_player_detail *player = &status->player_details[i];
    char head[64];
    int n = snprintf(head, sizeof head, "\\%lld %lld \"", (long long)player->score, (long long)player->ping);
    status_append(&b, head, (size_t)n);
    status_append(&b, player->name, strlen(player->name));
    status_append(&b, "\"", 1);
    status_slice(&b, 8191);
  }
  status_append(&b, "\\", 1);
  status_slice(&b, 8191);
  qa_cleanup_pop(&guard);
  return b.text;
}
static void start_status(qa_q3_browser_view *v, status_slot_t *slot, const qa_q3_mirror_network_address *address) {
  view_release(v, slot->handle);
  qa_q3_mirror_address_destroy(&slot->address);
  memset(&slot->address, 0, sizeof slot->address);
  qa_q3_mirror_address_copy(&slot->address, address);
  slot->has_address = true;
  slot->start = v->options.now(v->options.ctx);
  slot->retrieved = false;
  /* A failed send (handle 0) leaves the request unretrieved with no handle, so
   * the next call resends it. */
  slot->handle = v->options.browser.q3_core.request(v->options.browser.q3_core.ctx, address, slot->start, QA_Q3_DISCOVERY_STATUS);
}
char *qa_q3_browser_view_server_status(qa_q3_browser_view *v, const char *address, bool has_size, int64_t size, qa_q3_browser_write_fn write, void *write_ctx) {
  double left;
  double right;
  view_entry(v);
  if (!address) {
    for (int i = 0; i < 16; i++) {
      status_slot_t *slot = &v->statuses[i];
      view_release(v, slot->handle); slot->handle = 0;
      qa_q3_mirror_address_destroy(&slot->address); slot->has_address = false;
      slot->retrieved = true; slot->print = false;
    }
    return NULL;
  }
  int64_t generation = v->generation;
  qa_q3_mirror_network_address resolved; memset(&resolved, 0, sizeof resolved);
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_address, &resolved);
  bool ok = v->options.browser.resolve_q3(v->options.browser.ctx, address, &resolved);
  view_entry_at(v, generation);
  char *value = NULL;
  if (!ok) goto done;
  if (!has_size) {
    /* CL_ServerStatus only marks the slot retrieved, keeping its address and cached result. */
    status_slot_t *slot = status_slot_for(v, &resolved);
    slot->retrieved = true; slot->print = false;
    goto done;
  }
  status_slot_t *slot = status_slot_for(v, &resolved);
  if (!status_same(slot, &resolved) && !slot->retrieved) goto done;
  qa_q3_mirror_request_result result;
  bool has_result = slot->handle != 0 && v->options.browser.q3_core.request_result(v->options.browser.q3_core.ctx, slot->handle, &result);
  if (status_same(slot, &resolved) && has_result && result.kind == QA_Q3_REQ_COMPLETED && result.status) {
    char *text = status_text(result.status);
    qa_cleanup text_guard;
    qa_cleanup_push(&text_guard, release_string, &text);
    if (write) write(write_ctx, text);
    value = copy_string(text, size);
    qa_cleanup_pop(&text_guard);
    free(text);
    slot->retrieved = true; slot->start = 0;
    goto done;
  }
  if (!status_same(slot, &resolved) || slot->handle == 0
      || slot->start < (left = v->options.now(v->options.ctx), right = v->options.status_resend_time(v->options.ctx), left - right)) {
    slot->print = false;
    start_status(v, slot, &resolved);
  }
done:
  qa_cleanup_pop(&guard);
  qa_q3_mirror_address_destroy(&resolved);
  return value;
}
void qa_q3_browser_view_server_status_command(qa_q3_browser_view *v, const char *address) {
  view_entry(v);
  int64_t generation = v->generation;
  qa_q3_mirror_network_address resolved; memset(&resolved, 0, sizeof resolved);
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_address, &resolved);
  bool ok = v->options.browser.resolve_q3(v->options.browser.ctx, address, &resolved);
  view_entry_at(v, generation);
  if (ok) {
    status_slot_t *slot = status_slot_for(v, &resolved);
    start_status(v, slot, &resolved);
    slot->print = true;
  }
  qa_cleanup_pop(&guard);
  qa_q3_mirror_address_destroy(&resolved);
}
/* One TS options.print(text) call with an owned line. */
static void print_owned(qa_q3_browser_view *v, char *line) {
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_string, &line);
  if (v->options.print) v->options.print(v->options.print_ctx, line);
  qa_cleanup_pop(&guard);
  free(line);
}
/* `${key.padEnd(24, " ")}${value}\n` */
static void print_rule(qa_q3_browser_view *v, const char *key, const char *value) {
  size_t kn = strlen(key), vn = strlen(value), width = kn < 24 ? 24 : kn;
  char *line = malloc(width + vn + 2);
  if (!line) qa_range_error("browser print allocation failed");
  memcpy(line, key, kn); memset(line + kn, ' ', width - kn);
  memcpy(line + width, value, vn); line[width + vn] = '\n'; line[width + vn + 1] = 0;
  print_owned(v, line);
}
/* `${String(index).padEnd(2)}   ${String(score).padEnd(3)}    ${String(ping).padEnd(3)}   "${name}"\n` */
static void print_player(qa_q3_browser_view *v, size_t index, const qa_player_detail *player) {
  char head[96];
  int hn = snprintf(head, sizeof head, "%-2zu   %-3lld    %-3lld   \"", index, (long long)player->score, (long long)player->ping);
  size_t nn = strlen(player->name);
  char *line = malloc((size_t)hn + nn + 3);
  if (!line) qa_range_error("browser print allocation failed");
  memcpy(line, head, (size_t)hn); memcpy(line + hn, player->name, nn);
  memcpy(line + (size_t)hn + nn, "\"\n", 3);
  print_owned(v, line);
}
void qa_q3_browser_view_poll(qa_q3_browser_view *v) {
  view_entry(v);
  for (int i = 0; i < 32; i++) {
    ping_slot_t *slot = &v->pings[i];
    if (!slot->has_address || slot->published) continue;
    int64_t time = ping_time(v, slot);
    if (time != 0) { publish_ping(v, &slot->address, time); slot->published = true; }
  }
  for (int i = 0; i < 16; i++) {
    status_slot_t *slot = &v->statuses[i];
    if (!slot->print || slot->handle == 0) continue;
    qa_q3_mirror_request_result result;
    if (!v->options.browser.q3_core.request_result(v->options.browser.q3_core.ctx, slot->handle, &result)) continue;
    if (result.kind != QA_Q3_REQ_COMPLETED || !result.status) continue;
    if (v->options.print) v->options.print(v->options.print_ctx, "Server settings:\n");
    for (size_t j = 0; j < result.status->rules.count; j++)
      print_rule(v, result.status->rules.entries[j].key, result.status->rules.entries[j].value);
    if (v->options.print) v->options.print(v->options.print_ctx, "\nPlayers:\nnum: score: ping: name:\n");
    for (size_t j = 0; j < result.status->player_count; j++)
      print_player(v, j, &result.status->player_details[j]);
    slot->print = false; slot->retrieved = true;
    view_entry(v);
  }
}
void qa_q3_browser_cache_view_destroy(qa_q3_browser_cache_view *view) {
  for (size_t i = 0; i < view->list_count; i++) {
    for (size_t j = 0; j < view->lists[i].row_count; j++) {
      qa_q3_mirror_address_destroy(&view->lists[i].rows[j].address);
      free(view->lists[i].rows[j].name);
    }
    free(view->lists[i].rows);
  }
  free(view->lists);
  memset(view, 0, sizeof *view);
}
static void release_cache(void *arg) { qa_q3_browser_cache_view_destroy(arg); }
void qa_q3_browser_view_load_cached_servers(qa_q3_browser_view *v) {
  view_entry(v);
  gen_assert_t ga = { v, v->generation };
  qa_q3_browser_cache_view cache; memset(&cache, 0, sizeof cache);
  qa_cleanup cache_guard;
  qa_cleanup_push(&cache_guard, release_cache, &cache);
  bool has_cache = v->options.browser.load_q3_cache(v->options.browser.ctx, gen_assert, &ga, &cache);
  view_entry_at(v, ga.generation);
  static const int cached[3] = {1, 2, 3};
  for (int ci = 0; ci < 3; ci++) {
    int64_t source = cached[ci];
    list_t *list = view_list(v, source);
    if (!list) continue;
    const qa_q3_browser_cache_list *saved = NULL;
    if (has_cache) for (size_t i = 0; i < cache.list_count && !saved; i++) if (cache.lists[i].source == source) saved = &cache.lists[i];
    if (!saved) continue;
    if (source == 2) {
      qa_q3_browser_list_state canonical = v->options.browser.q3_list(v->options.browser.ctx, source);
      /* selected = saved rows whose address key is canonical */
      row_t *selected = calloc(saved->row_count ? saved->row_count : 1, sizeof *selected);
      if (!selected) qa_range_error("browser cache allocation failed");
      size_t sel_count = 0;
      for (size_t i = 0; i < saved->row_count; i++) {
        bool has = false;
        for (size_t j = 0; j < canonical.address_count; j++) {
          char *k1 = qa_q3_mirror_address_key(&saved->rows[i].address, true);
          char *k2 = qa_q3_mirror_address_key(&canonical.addresses[j], true);
          if (k1 && k2 && strcmp(k1, k2) == 0) has = true;
          free(k1); free(k2);
          if (has) break;
        }
        if (!has) continue;
        selected[sel_count].name = xstrdup(saved->rows[i].name ? saved->rows[i].name : "");
        selected[sel_count].has_address = true;
        qa_q3_mirror_address_copy(&selected[sel_count].address, &saved->rows[i].address);
        selected[sel_count].visible = saved->rows[i].visible;
        selected[sel_count].ping = saved->rows[i].ping;
        sel_count++;
      }
      list->count = sel_count < list->capacity ? (int64_t)sel_count : (int64_t)list->capacity;
      for (size_t i = 0; i < list->capacity; i++) {
        if (i < sel_count) row_copy(&list->rows[i], &selected[i]);
        else { row_clear(&list->rows[i]); list->rows[i].visible = 0; list->rows[i].ping = 0; }
      }
      for (size_t i = 0; i < sel_count; i++) { qa_q3_mirror_address_destroy(&selected[i].address); free(selected[i].name); }
      free(selected);
      seen_clear(list);
      for (size_t i = 0; i < canonical.address_count; i++) {
        char *k = qa_q3_mirror_address_key(&canonical.addresses[i], true);
        if (k) { seen_add(list, k); free(k); }
      }
      for (size_t i = 0; i < list->overflow_count; i++) qa_q3_mirror_address_destroy(&list->overflow[i]);
      list->overflow_count = 0;
    } else {
      for (size_t i = 0; i < list->capacity; i++) {
        const qa_q3_browser_cache_row *found = NULL;
        if ((int64_t)i < list->count) {
          for (size_t j = 0; j < saved->row_count; j++)
            if (rows_same(&list->rows[i], &saved->rows[j].address)) { found = &saved->rows[j]; break; }
        }
        if (found) {
          row_t copy; memset(&copy, 0, sizeof copy);
          copy.name = xstrdup(found->name ? found->name : "");
          copy.has_address = true;
          qa_q3_mirror_address_copy(&copy.address, &found->address);
          copy.visible = found->visible; copy.ping = found->ping;
          row_copy(&list->rows[i], &copy);
          qa_q3_mirror_address_destroy(&copy.address); free(copy.name);
        } else if ((int64_t)i >= list->count) {
          row_clear(&list->rows[i]); list->rows[i].visible = 0; list->rows[i].ping = 0;
        }
      }
    }
  }
  qa_cleanup_pop(&cache_guard);
  qa_q3_browser_cache_view_destroy(&cache);
}
void qa_q3_browser_view_save_servers_to_cache(qa_q3_browser_view *v) {
  view_entry(v);
  int64_t generation = v->generation;
  qa_q3_browser_cache_view view; memset(&view, 0, sizeof view);
  view.lists = calloc(3, sizeof *view.lists);
  if (!view.lists) qa_range_error("browser cache allocation failed");
  qa_cleanup guard;
  qa_cleanup_push(&guard, release_cache, &view);
  static const int cached[3] = {1, 2, 3};
  for (int ci = 0; ci < 3; ci++) {
    int64_t source = cached[ci];
    list_t *list = view_list(v, source);
    if (!list) continue;
    qa_q3_browser_cache_list *out = &view.lists[view.list_count++];
    out->source = source;
    out->rows = calloc(list->count ? (size_t)list->count : 1, sizeof *out->rows);
    if (!out->rows) qa_range_error("browser cache allocation failed");
    for (int64_t i = 0; i < list->count; i++) {
      row_t *row = &list->rows[i];
      if (!row->has_address) continue;
      qa_q3_browser_cache_row *cr = &out->rows[out->row_count++];
      memset(&cr->address, 0, sizeof cr->address);
      qa_q3_mirror_address_copy(&cr->address, &row->address);
      cr->name = xstrdup(row->name ? row->name : "");
      cr->visible = row->visible; cr->ping = row->ping;
    }
  }
  v->options.browser.save_q3_cache(v->options.browser.ctx, &view);
  qa_cleanup_pop(&guard);
  qa_q3_browser_cache_view_destroy(&view);
  view_entry_at(v, generation);
}
void qa_q3_browser_view_close(qa_q3_browser_view *v) {
  if (v->closed) return;
  v->closed = true; v->generation++;
  for (int i = 0; i < 32; i++) {
    view_release(v, v->pings[i].handle); v->pings[i].handle = 0;
    qa_q3_mirror_address_destroy(&v->pings[i].address); v->pings[i].has_address = false;
  }
  for (int i = 0; i < 16; i++) {
    view_release(v, v->statuses[i].handle); v->statuses[i].handle = 0;
    qa_q3_mirror_address_destroy(&v->statuses[i].address); v->statuses[i].has_address = false;
  }
}
