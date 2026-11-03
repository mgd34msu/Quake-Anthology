/* Port of src/app/bootstrap/startup-selection.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup_selection.h"
#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/app/bootstrap/base_arena_catalog.h"
#include "qa/app/bootstrap/base_arena_progression.h"
#include "qa/app/bootstrap/configuration.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/cvar_archives.h"
#include "qa/app/bootstrap/match_modes.h"
#include "qa/app/bootstrap/mod_selection.h"
#include "qa/app/bootstrap/q3_product.h"
#include "qa/app/bootstrap/qvm_grapple_selection.h"
#include "qa/app/bootstrap/startup_timing.h"
#include "qa/app/bootstrap/team_arena_skirmish.h"
#include "qa/compat/qvm/artifacts.h"
#include "qa/content/archive.h"
#include "qa/content/catalog/equipment.h"
#include "qa/content/catalog/launch.h"
#include "qa/content/catalog/products.h"
#include "qa/content/catalog/source_program.h"
#include "qa/content/catalog/timing.h"
#include "qa/content/catalog/weapons.h"
#include "qa/content/mods/selection.h"
#include "qa/content/monsters/definitions.h"
#include "qa/content/monsters/roster.h"
#include "qa/content/mounts.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q3/equipment/grapple_profiles.h"
#include "qa/content/user_data.h"
#include "qa/contracts/mods.h"
#include "qa/core/common_parse.h"
#include "qa/core/cvars/index.h"
#include "qa/core/q3_product_policy.h"
#include "qa/formats/bsp_kind.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/formats/q3_map/entities.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/network/q1/profile.h"
#include "qa/settings/config.h"

/* ---------------------------------------------------------------- */
/* Allocation, errors, strings.                                      */

static void *xmalloc(size_t size) {
  void *p = malloc(size ? size : 1);
  if (!p) qa_range_error("Out of memory");
  return p;
}
static void *xcalloc(size_t count, size_t size) {
  void *p = calloc(count ? count : 1, size ? size : 1);
  if (!p) qa_range_error("Out of memory");
  return p;
}
static void *xrealloc(void *p, size_t size) {
  void *next = realloc(p, size ? size : 1);
  if (!next) qa_range_error("Out of memory");
  return next;
}
static char *xdup(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *out = xmalloc(length + 1);
  memcpy(out, text, length + 1);
  return out;
}
static char *xvprintf(const char *format, va_list args) {
  va_list copy;
  va_copy(copy, args);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length < 0) qa_range_error("Out of memory");
  char *out = xmalloc((size_t)length + 1);
  vsnprintf(out, (size_t)length + 1, format, args);
  return out;
}
static char *xprintf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  char *out = xvprintf(format, args);
  va_end(args);
  return out;
}
/* TS `throw new Error(message)`. */
static _Noreturn void fail(const char *format, ...) {
  va_list args;
  va_start(args, format);
  char *message = xvprintf(format, args);
  va_end(args);
  char copy[QA_CAUGHT_MESSAGE_CAPACITY];
  snprintf(copy, sizeof copy, "%s", message);
  free(message);
  qa_raise("Error", 0, 0, "%s", copy);
}
static bool eq(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static char *lower_dup(const char *text) {
  char *out = qa_js_to_lower_case(text);
  if (!out) qa_range_error("Out of memory");
  return out;
}
static bool equals_ci(const char *a, const char *b) { return qa_js_equals_ignore_case(a, b); }
static bool starts_with(const char *text, const char *prefix) { return strncmp(text, prefix, strlen(prefix)) == 0; }
static bool ends_with(const char *text, const char *suffix) {
  size_t a = strlen(text), b = strlen(suffix);
  return a >= b && memcmp(text + a - b, suffix, b) == 0;
}
static const char *family_text(qa_game_family family) { return qa_game_family_name(family); }

/* ---------------------------------------------------------------- */
/* Arena: per-result storage, reset when the member is called again. */

typedef struct arena { void **items; size_t count, cap; } arena;
static void *arena_keep(arena *a, void *p) {
  if (a->count == a->cap) { a->cap = a->cap ? a->cap * 2 : 32; a->items = xrealloc(a->items, a->cap * sizeof *a->items); }
  a->items[a->count++] = p;
  return p;
}
static void *arena_alloc(arena *a, size_t size) { return arena_keep(a, xcalloc(1, size)); }
static char *arena_dup(arena *a, const char *text) { return text == NULL ? NULL : arena_keep(a, xdup(text)); }
static char *arena_printf(arena *a, const char *format, ...) {
  va_list args;
  va_start(args, format);
  char *out = xvprintf(format, args);
  va_end(args);
  return arena_keep(a, out);
}
static void arena_reset(arena *a) {
  for (size_t i = 0; i < a->count; i++) free(a->items[i]);
  a->count = 0;
}
static void arena_free(arena *a) { arena_reset(a); free(a->items); a->items = NULL; a->cap = 0; }
static void arena_cleanup(void *arg) { arena_free(arg); }

/* ---------------------------------------------------------------- */
/* Choices.                                                          */

typedef qa_app_startup_selection_choice choice_t;
typedef struct cvec { choice_t *items; size_t count, cap; } cvec;
static void cvec_push(cvec *v, choice_t c) {
  if (v->count == v->cap) { v->cap = v->cap ? v->cap * 2 : 16; v->items = xrealloc(v->items, v->cap * sizeof *v->items); }
  v->items[v->count++] = c;
}
static choice_t choice3(const char *id, const char *label, const char *unavailable) {
  choice_t c = {id, label != NULL ? label : id, unavailable, NULL, NULL};
  return c;
}
static choice_t choice1(const char *id) { return choice3(id, id, NULL); }
/* Copies a choice list into `a` (array and strings); the cvec is freed. */
static const choice_t *cvec_finish(cvec *v, arena *a, size_t *out_count) {
  choice_t *out = arena_alloc(a, (v->count ? v->count : 1) * sizeof *out);
  for (size_t i = 0; i < v->count; i++) {
    out[i].id = arena_dup(a, v->items[i].id);
    out[i].label = arena_dup(a, v->items[i].label);
    out[i].unavailable = arena_dup(a, v->items[i].unavailable);
    out[i].group = arena_dup(a, v->items[i].group);
    out[i].edition = arena_dup(a, v->items[i].edition);
  }
  *out_count = v->count;
  free(v->items);
  v->items = NULL; v->count = v->cap = 0;
  return out;
}
static const choice_t *choices_find(const choice_t *items, size_t count, const char *id) {
  for (size_t i = 0; i < count; i++) if (eq(items[i].id, id)) return &items[i];
  return NULL;
}

/* Persistent choice list (owned id/label/unavailable; group/edition are
 * static text). */
typedef struct pchoices { choice_t *items; size_t count; } pchoices;
static void pchoices_free(void *value) {
  pchoices *list = value;
  if (!list) return;
  for (size_t i = 0; i < list->count; i++) {
    free((char *)list->items[i].id); free((char *)list->items[i].label); free((char *)list->items[i].unavailable);
  }
  free(list->items);
  free(list);
}
static pchoices *pchoices_from(const choice_t *items, size_t count) {
  pchoices *list = xcalloc(1, sizeof *list);
  list->items = xcalloc(count, sizeof *list->items);
  list->count = count;
  for (size_t i = 0; i < count; i++) {
    list->items[i].id = xdup(items[i].id);
    list->items[i].label = xdup(items[i].label);
    list->items[i].unavailable = xdup(items[i].unavailable);
    list->items[i].group = items[i].group;
    list->items[i].edition = items[i].edition;
  }
  return list;
}

/* ---------------------------------------------------------------- */
/* Insertion-ordered string map (JS Map/Set over string keys).       */

typedef struct smap {
  char **keys; void **values; size_t count, cap;
  size_t *slots; size_t slot_cap; /* index + 1; 0 empty */
  void (*free_value)(void *value);
} smap;
static uint64_t hash_text(const char *text) {
  uint64_t h = 1469598103934665603ull;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) { h ^= *p; h *= 1099511628211ull; }
  return h;
}
static void smap_reindex(smap *m) {
  size_t cap = 16;
  while (cap < m->count * 2 + 2) cap *= 2;
  free(m->slots);
  m->slots = xcalloc(cap, sizeof *m->slots);
  m->slot_cap = cap;
  for (size_t i = 0; i < m->count; i++) {
    size_t at = (size_t)(hash_text(m->keys[i]) & (cap - 1));
    while (m->slots[at] != 0) at = (at + 1) & (cap - 1);
    m->slots[at] = i + 1;
  }
}
static size_t smap_index(const smap *m, const char *key) {
  if (m->slot_cap == 0) return SIZE_MAX;
  size_t at = (size_t)(hash_text(key) & (m->slot_cap - 1));
  while (m->slots[at] != 0) {
    size_t index = m->slots[at] - 1;
    if (strcmp(m->keys[index], key) == 0) return index;
    at = (at + 1) & (m->slot_cap - 1);
  }
  return SIZE_MAX;
}
static bool smap_has(const smap *m, const char *key) { return smap_index(m, key) != SIZE_MAX; }
static void *smap_get(const smap *m, const char *key) {
  size_t index = smap_index(m, key);
  return index == SIZE_MAX ? NULL : m->values[index];
}
static void smap_set(smap *m, const char *key, void *value) {
  size_t index = smap_index(m, key);
  if (index != SIZE_MAX) {
    if (m->free_value && m->values[index] != value) m->free_value(m->values[index]);
    m->values[index] = value;
    return;
  }
  if (m->count == m->cap) {
    m->cap = m->cap ? m->cap * 2 : 16;
    m->keys = xrealloc(m->keys, m->cap * sizeof *m->keys);
    m->values = xrealloc(m->values, m->cap * sizeof *m->values);
  }
  m->keys[m->count] = xdup(key);
  m->values[m->count] = value;
  m->count++;
  if (m->count * 2 + 2 > m->slot_cap) smap_reindex(m);
  else {
    size_t at = (size_t)(hash_text(key) & (m->slot_cap - 1));
    while (m->slots[at] != 0) at = (at + 1) & (m->slot_cap - 1);
    m->slots[at] = m->count;
  }
}
static void smap_delete(smap *m, const char *key) {
  size_t index = smap_index(m, key);
  if (index == SIZE_MAX) return;
  free(m->keys[index]);
  if (m->free_value) m->free_value(m->values[index]);
  memmove(m->keys + index, m->keys + index + 1, (m->count - index - 1) * sizeof *m->keys);
  memmove(m->values + index, m->values + index + 1, (m->count - index - 1) * sizeof *m->values);
  m->count--;
  smap_reindex(m);
}
static void smap_clear(smap *m) {
  for (size_t i = 0; i < m->count; i++) {
    free(m->keys[i]);
    if (m->free_value) m->free_value(m->values[i]);
  }
  m->count = 0;
  if (m->slots) memset(m->slots, 0, m->slot_cap * sizeof *m->slots);
}
static void smap_free(smap *m) {
  smap_clear(m);
  free(m->keys); free(m->values); free(m->slots);
  m->keys = NULL; m->values = NULL; m->slots = NULL; m->cap = m->slot_cap = 0;
}
static smap *smap_new(void (*free_value)(void *)) {
  smap *m = xcalloc(1, sizeof *m);
  m->free_value = free_value;
  return m;
}
static void smap_destroy(void *value) {
  smap *m = value;
  if (!m) return;
  smap_free(m);
  free(m);
}

/* Owned string list with a generation (TS list object identity). */
typedef struct slist { char **items; size_t count, cap; unsigned long generation; } slist;
static void slist_push(slist *list, const char *text) {
  if (list->count == list->cap) { list->cap = list->cap ? list->cap * 2 : 16; list->items = xrealloc(list->items, list->cap * sizeof *list->items); }
  list->items[list->count++] = xdup(text);
}
static void slist_free(void *value) {
  slist *list = value;
  if (!list) return;
  for (size_t i = 0; i < list->count; i++) free(list->items[i]);
  free(list->items);
  free(list);
}
static slist *slist_copy(const slist *source, unsigned long generation) {
  slist *list = xcalloc(1, sizeof *list);
  for (size_t i = 0; i < source->count; i++) slist_push(list, source->items[i]);
  list->generation = generation;
  return list;
}

/* ---------------------------------------------------------------- */
/* ApplicationOptions copy.                                          */

void qa_app_startup_options_copy(qa_app_options *out, const qa_app_options *o) {
  *out = *o;
  out->network_transport_relay = xdup(o->network_transport_relay);
  out->startup_commands = NULL;
  if (o->startup_command_count > 0) {
    out->startup_commands = xcalloc(o->startup_command_count, sizeof *out->startup_commands);
    for (size_t i = 0; i < o->startup_command_count; i++) out->startup_commands[i] = xdup(o->startup_commands[i]);
  }
  out->remote_content.directory = xdup(o->remote_content.directory);
  out->server_profile_path = xdup(o->server_profile_path);
  out->corpus_root = xdup(o->corpus_root);
  out->user_content_root = xdup(o->user_content_root);
  out->addon_content_roots = NULL;
  if (o->addon_content_roots != NULL) {
    out->addon_content_roots = xcalloc(o->addon_content_root_count, sizeof *out->addon_content_roots);
    for (size_t i = 0; i < o->addon_content_root_count; i++) {
      out->addon_content_roots[i].family = o->addon_content_roots[i].family;
      out->addon_content_roots[i].root = xdup(o->addon_content_roots[i].root);
    }
  }
  out->product = xdup(o->product);
  out->map_product = xdup(o->map_product);
  out->map = xdup(o->map);
  out->quakec_program = xdup(o->quakec_program);
  out->q2_game_library = xdup(o->q2_game_library);
  out->weapon_behavior_product = xdup(o->weapon_behavior_product);
  out->weapon_behavior_id = xdup(o->weapon_behavior_id);
  out->mods = NULL;
  if (o->mod_count > 0) {
    out->mods = xcalloc(o->mod_count, sizeof *out->mods);
    for (size_t i = 0; i < o->mod_count; i++) {
      out->mods[i].product = xdup(o->mods[i].product);
      out->mods[i].id = xdup(o->mods[i].id);
    }
  }
  out->movement_product = xdup(o->movement_product);
  out->character_model = xdup(o->character_model);
  out->character_skin = xdup(o->character_skin);
  out->network_host = xdup(o->network_host);
  out->network_remote = xdup(o->network_remote);
}
static void set_text(char **field, const char *value) {
  char *copy = xdup(value);
  free(*field);
  *field = copy;
}
static void set_mods(qa_app_options *o, const qa_mod_selection *mods, size_t count) {
  for (size_t i = 0; i < o->mod_count; i++) qa_mod_selection_free(&o->mods[i]);
  free(o->mods);
  o->mods = NULL; o->mod_count = 0;
  if (count == 0) return;
  o->mods = xcalloc(count, sizeof *o->mods);
  for (size_t i = 0; i < count; i++) { o->mods[i].product = xdup(mods[i].product); o->mods[i].id = xdup(mods[i].id); }
  o->mod_count = count;
}
static bool network_is_server(const char *kind) {
  return eq(kind, "native-server") || eq(kind, "q2-server") || eq(kind, "unified-server");
}

/* ---------------------------------------------------------------- */
/* Model state.                                                      */

enum field {
  F_DOPPLER, F_ENVIRONMENT, F_PRODUCT, F_MAP_PRODUCT, F_MAP, F_MOVEMENT, F_CHARACTER, F_MODEL, F_SKIN,
  F_TOP_COLOR, F_BOTTOM_COLOR, F_WEAPONS, F_ENEMIES, F_GRAPPLE, F_GRAPPLE_STYLE, F_GRENADES, F_MODE,
  F_RULES, F_SKILL, F_SEATS, F_RENDERER, F_COUNT
};
static const char *const field_names[F_COUNT] = {
  "doppler", "environment", "product", "mapProduct", "map", "movement", "character", "model", "skin",
  "topColor", "bottomColor", "weapons", "enemies", "grapple", "grappleStyle", "grenades", "mode",
  "rules", "skill", "seats", "renderer"};
static int field_of(const char *name) {
  for (int i = 0; i < F_COUNT; i++) if (strcmp(field_names[i], name) == 0) return i;
  return -1;
}

typedef struct roster_state { char *source; char *def; smap *by_classname; /* classname -> char* */ } roster_state;
static void roster_free(void *value) {
  roster_state *roster = value;
  if (!roster) return;
  free(roster->source); free(roster->def);
  smap_destroy(roster->by_classname);
  free(roster);
}
typedef struct file_set { const qa_catalog_discovered_product *product; unsigned long loose; smap *paths; } file_set;
/* A Q3 QVM hook style (TS GrappleStyle built in prepareHookStyles). */
typedef struct qvm_style {
  char *module_id;
  char *artifact_path, *digest, *revision;
  qa_qvm_grapple_definition profile;
  qa_grapple_selection selection;
} qvm_style;
static void qvm_style_free(qvm_style *style) {
  free(style->module_id); free(style->artifact_path); free(style->digest); free(style->revision);
  free(style);
}
typedef struct retained { void **items; void (**frees)(void *); size_t count, cap; } retained;
static void retain(retained *r, void *item, void (*release)(void *)) {
  if (item == NULL) return;
  if (r->count == r->cap) {
    r->cap = r->cap ? r->cap * 2 : 8;
    r->items = xrealloc(r->items, r->cap * sizeof *r->items);
    r->frees = xrealloc(r->frees, r->cap * sizeof *r->frees);
  }
  r->items[r->count] = item; r->frees[r->count] = release; r->count++;
}
static void retained_free(retained *r) {
  for (size_t i = r->count; i-- > 0;) r->frees[i](r->items[i]);
  free(r->items); free(r->frees);
  memset(r, 0, sizeof *r);
}
static void catalog_release(void *value) {
  qa_content_catalog *catalog = value;
  qa_content_catalog_destroy(catalog);
  free(catalog);
}

struct qa_app_startup_selection_model {
  qa_content_catalog *current; /* TS currentCatalog */
  retained owned;              /* catalogs, Q3/mod backings: released at destroy */
  qa_app_options initial;
  char *values[F_COUNT];
  /* arenaSelection */
  bool has_arenas; qa_app_arena_selection arenas;
  qa_app_base_arena_catalog arena_catalog; qa_app_base_arena_progression *arena_progression;
  qa_cvar_registry *arena_cvars; qa_identity_owner arena_identity; bool has_arena_identity;
  /* teamArena */
  qa_app_team_arena_campaign *team_campaign;
  char *team_player, *team_opponent;
  arena team_arena;
  /* selectedServerProfile: has = not null; path NULL = {} */
  bool has_server_profile; char *server_profile_path;
  /* mods */
  const qa_app_mod_choices *mod_choices;
  qa_mod_selection_set selected_mods; bool has_selected_mods;
  char *mod_status; bool refreshing_mods;
  qa_ui_mod_menu_service mod_service;
  arena mod_rows;
  qvm_style **qvm_styles; size_t qvm_style_count;
  /* display */
  double display_width, display_height, display_gamma;
  bool display_overrides_consumed;
  smap *playable_maps;        /* product id -> pchoices */
  smap *authored_default_maps;/* product id -> char* */
  smap *loose_models;         /* product id -> slist */
  smap *eligible_maps;        /* key -> pchoices */
  smap *map_classnames;       /* key -> slist */
  smap *monster_classes;      /* key -> smap(classname -> count) */
  smap *rosters;              /* product id -> roster_state */
  smap *selected_models;      /* character id -> char* */
  smap *model_choices;        /* product id -> pchoices */
  file_set *file_sets; size_t file_set_count, file_set_cap;
  unsigned long loose_generation;
  /* result storage */
  arena rows_arena, presets_arena, monster_rows_arena, source_row_arena, summary_arena, preview_arena, key_arena;
};

static const qa_catalog_discovered_product *cat_product(qa_app_startup_selection_model *m, const char *id) {
  return qa_content_catalog_product(m->current, id);
}
static const qa_catalog_discovered_product *cat_require(qa_app_startup_selection_model *m, const char *id) {
  return qa_content_catalog_require(m->current, id);
}
static const qa_catalog_discovered_product *find_product_by_id(qa_content_catalog *catalog, const char *id) {
  for (size_t i = 0; i < catalog->product_count; i++)
    if (eq(catalog->products[i].expectation.id, id)) return &catalog->products[i];
  return NULL;
}
static bool installed(const qa_catalog_discovered_product *product) {
  return product->availability.kind == QA_CATALOG_INSTALLED;
}
static const char *edition_of(const qa_catalog_discovered_product *product) {
  return product->expectation.edition != NULL ? product->expectation.edition : "";
}
static const char *campaign_of(const qa_catalog_discovered_product *product) {
  return product->expectation.campaign != NULL ? product->expectation.campaign : "";
}
/* TS unavailable(product), into `a`. */
static const char *unavailable_of(const qa_catalog_discovered_product *product, arena *a) {
  const qa_catalog_availability *state = &product->availability;
  if (state->kind == QA_CATALOG_INSTALLED) return NULL;
  if (state->kind == QA_CATALOG_UNRESOLVED) return state->reason != NULL ? state->reason : "";
  size_t length = strlen("Missing: ") + 1;
  for (size_t i = 0; i < state->requirement_count; i++) length += strlen(state->requirements[i]) + 2;
  char *text = arena_alloc(a, length);
  strcpy(text, "Missing: ");
  for (size_t i = 0; i < state->requirement_count; i++) {
    if (i > 0) strcat(text, ", ");
    strcat(text, state->requirements[i]);
  }
  return text;
}
static const char *family_group(qa_game_family family) {
  return family == QA_GAME_FAMILY_Q1 ? "Quake" : family == QA_GAME_FAMILY_Q2 ? "Quake II" : "Quake III Arena";
}
/* TS productChoice. */
static choice_t product_choice(const qa_catalog_discovered_product *product, arena *a) {
  choice_t c = choice3(product->expectation.id, product->expectation.title, unavailable_of(product, a));
  c.group = family_group(product->expectation.family);
  const char *edition = edition_of(product);
  if (eq(edition, "classic") || eq(edition, "rerelease")) c.edition = edition;
  return c;
}
static const char *value(qa_app_startup_selection_model *m, int field) { return m->values[field]; }
static void set_value(qa_app_startup_selection_model *m, int field, const char *text) { set_text(&m->values[field], text); }
/* TS product(field) / geometry(). */
static const qa_catalog_discovered_product *product_field(qa_app_startup_selection_model *m, int field) {
  return cat_product(m, m->values[field]);
}
static const qa_catalog_discovered_product *geometry(qa_app_startup_selection_model *m) {
  return cat_require(m, m->values[F_MAP_PRODUCT]);
}
/* TS `${map.source}:${map.memberIndex}` */
static char *map_key(const qa_catalog_content_map *map) {
  if (!map->has_member_index) return xprintf("%s:null", map->source);
  return xprintf("%s:%s", map->source, QA_JS_NUMBER((double)map->member_index));
}
static const qa_catalog_content_map **maps_for(qa_app_startup_selection_model *m, const char *id, size_t *count) {
  const qa_catalog_content_map **maps = qa_content_catalog_maps_for(m->current, id, count);
  if (maps == NULL && *count > 0) qa_range_error("Out of memory");
  return maps;
}

/* ---------------------------------------------------------------- */
/* Mount plan opener for catalog reads (TS MountPreparationScope.open). */

typedef struct reader_state { qa_content_mounted *mounted; uint8_t **buffers; size_t count, cap; } reader_state;
static bool reader_open(void *ctx, const char *path, qa_catalog_opened_resource *out) {
  reader_state *state = ctx;
  qa_content_opened_resource opened = {0};
  if (!qa_content_mounted_open(state->mounted, path, NULL, NULL, &opened)) return false;
  if (state->count == state->cap) { state->cap = state->cap ? state->cap * 2 : 4; state->buffers = xrealloc(state->buffers, state->cap * sizeof *state->buffers); }
  state->buffers[state->count++] = opened.bytes;
  out->reference = *opened.reference;
  out->bytes = opened.bytes;
  out->byte_length = opened.byte_length;
  return true;
}
static void reader_close(void *ctx) {
  reader_state *state = ctx;
  qa_content_mounted_destroy(state->mounted);
  for (size_t i = 0; i < state->count; i++) free(state->buffers[i]);
  free(state->buffers);
  free(state);
}
static bool opener_open_plan(void *ctx, const qa_resolved_mount_plan *plan, qa_catalog_mounted_reader *out) {
  (void)ctx; /* the TS opener closure captures nothing beyond its scope */
  qa_open_mount_options options;
  memset(&options, 0, sizeof options);
  qa_content_mounted *mounted = qa_open_mount_plan(plan, &options);
  if (mounted == NULL) qa_range_error("Out of memory");
  reader_state *state = xcalloc(1, sizeof *state);
  state->mounted = mounted;
  out->ctx = state;
  out->open = reader_open;
  out->close = reader_close;
  return true;
}
static qa_catalog_mount_plan_opener scope_opener(void) {
  qa_catalog_mount_plan_opener opener = {NULL, opener_open_plan};
  return opener;
}

/* ---------------------------------------------------------------- */
/* Construction.                                                     */

static const qa_ui_mod_menu_row *mod_rows(void *ctx, size_t *out_count);
static void mod_set_enabled(void *ctx, const char *id, bool enabled);
static void mod_refresh(void *ctx);
static const char *mod_status(void *ctx);

static qa_app_startup_selection_model *model_alloc(qa_content_catalog *catalog, const qa_app_options *initial) {
  qa_app_startup_selection_model *m = xcalloc(1, sizeof *m);
  m->current = catalog;
  qa_app_startup_options_copy(&m->initial, initial);
  m->team_player = xdup("pagans");
  m->team_opponent = xdup("stroggs");
  m->mod_status = xdup("");
  m->mod_service.ctx = m;
  m->mod_service.rows = mod_rows;
  m->mod_service.set_enabled = mod_set_enabled;
  m->mod_service.refresh = mod_refresh;
  m->mod_service.status = mod_status;
  m->playable_maps = smap_new(pchoices_free);
  m->authored_default_maps = smap_new(free);
  m->loose_models = smap_new(slist_free);
  m->eligible_maps = smap_new(pchoices_free);
  m->map_classnames = smap_new(slist_free);
  m->monster_classes = smap_new(smap_destroy);
  m->rosters = smap_new(roster_free);
  m->selected_models = smap_new(free);
  m->model_choices = smap_new(pchoices_free);
  return m;
}
static char *number_text(double number) { return xdup(QA_JS_NUMBER(number)); }

/* TS constructor(currentCatalog, initial). */
static qa_app_startup_selection_model *model_create(qa_content_catalog *catalog, const qa_app_options *initial) {
  qa_app_startup_selection_model *m = model_alloc(catalog, initial);
  retain(&m->owned, catalog, catalog_release);
  m->display_width = initial->width;
  m->display_height = initial->height;
  m->display_gamma = initial->gamma;
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, initial->product);
  const char *campaign = campaign_of(product);
  const char *movement = NULL, *character = NULL;
  qa_app_application_player_products(catalog, &m->initial, false, false, &movement, &character);
  const char *rules = initial->rules != NULL ? initial->rules
      : product->expectation.family == QA_GAME_FAMILY_Q2 && eq(edition_of(product), "classic")
        && (eq(campaign, "ctf") || eq(campaign, "lmctf")) ? campaign : "standard";
  m->values[F_PRODUCT] = xdup(initial->product);
  m->values[F_MAP_PRODUCT] = xdup(initial->map_product != NULL ? initial->map_product : initial->product);
  m->values[F_MAP] = xdup(initial->map != NULL ? initial->map : "");
  m->values[F_MOVEMENT] = xdup(movement);
  m->values[F_CHARACTER] = xdup(character);
  m->values[F_MODEL] = xdup(initial->character_model != NULL ? initial->character_model : "");
  m->values[F_SKIN] = xdup(initial->character_skin != NULL ? initial->character_skin : "");
  m->values[F_TOP_COLOR] = number_text(initial->has_character_top_color ? initial->character_top_color : 0);
  m->values[F_BOTTOM_COLOR] = number_text(initial->has_character_bottom_color ? initial->character_bottom_color : 0);
  m->values[F_DOPPLER] = xdup("source");
  m->values[F_ENVIRONMENT] = xdup("audio-content");
  m->values[F_WEAPONS] = xdup("native");
  m->values[F_ENEMIES] = xdup("native");
  m->values[F_GRAPPLE] = xdup("native");
  m->values[F_GRAPPLE_STYLE] = xdup("native");
  m->values[F_GRENADES] = xdup("native");
  m->values[F_MODE] = xdup(initial->mode != NULL ? initial->mode : "");
  m->values[F_RULES] = xdup(rules);
  m->values[F_SKILL] = number_text(initial->skill);
  m->values[F_SEATS] = number_text(initial->seats);
  m->values[F_RENDERER] = xdup(initial->renderer != NULL ? initial->renderer : "");
  smap_set(m->selected_models, m->values[F_CHARACTER], xdup(m->values[F_MODEL]));
  return m;
}
qa_app_startup_selection_model *qa_app_startup_selection_model_new(qa_content_catalog *catalog, const qa_app_options *initial) {
  return model_create(catalog, initial);
}
static void arena_state_free(qa_app_startup_selection_model *m) {
  if (m->has_arenas) qa_app_arena_selection_destroy(&m->arenas);
  m->has_arenas = false;
  qa_app_base_arena_catalog_destroy(&m->arena_catalog);
  memset(&m->arena_catalog, 0, sizeof m->arena_catalog);
  if (m->arena_progression) qa_app_base_arena_progression_destroy(m->arena_progression);
  m->arena_progression = NULL;
  if (m->arena_cvars) qa_cvar_registry_destroy(m->arena_cvars);
  m->arena_cvars = NULL;
  if (m->has_arena_identity) qa_identity_owner_destroy(&m->arena_identity);
  m->has_arena_identity = false;
}
static void qvm_styles_free(qa_app_startup_selection_model *m) {
  for (size_t i = 0; i < m->qvm_style_count; i++) qvm_style_free(m->qvm_styles[i]);
  free(m->qvm_styles);
  m->qvm_styles = NULL; m->qvm_style_count = 0;
}
static void file_sets_free(qa_app_startup_selection_model *m) {
  for (size_t i = 0; i < m->file_set_count; i++) smap_destroy(m->file_sets[i].paths);
  free(m->file_sets);
  m->file_sets = NULL; m->file_set_count = m->file_set_cap = 0;
}
void qa_app_startup_selection_model_destroy(qa_app_startup_selection_model *m) {
  if (!m) return;
  arena_state_free(m);
  if (m->team_campaign) qa_app_team_arena_campaign_destroy(m->team_campaign);
  free(m->team_player); free(m->team_opponent);
  arena_free(&m->team_arena);
  free(m->server_profile_path);
  if (m->has_selected_mods) qa_mod_selection_set_destroy(&m->selected_mods);
  free(m->mod_status);
  arena_free(&m->mod_rows);
  qvm_styles_free(m);
  smap_destroy(m->playable_maps); smap_destroy(m->authored_default_maps); smap_destroy(m->loose_models);
  smap_destroy(m->eligible_maps); smap_destroy(m->map_classnames); smap_destroy(m->monster_classes);
  smap_destroy(m->rosters); smap_destroy(m->selected_models); smap_destroy(m->model_choices);
  file_sets_free(m);
  arena_free(&m->rows_arena); arena_free(&m->presets_arena); arena_free(&m->monster_rows_arena);
  arena_free(&m->source_row_arena); arena_free(&m->summary_arena); arena_free(&m->preview_arena);
  arena_free(&m->key_arena);
  for (int i = 0; i < F_COUNT; i++) free(m->values[i]);
  qa_app_options_destroy(&m->initial);
  retained_free(&m->owned);
  free(m);
}
qa_content_catalog *qa_app_startup_selection_catalog(qa_app_startup_selection_model *m) { return m->current; }

/* ---------------------------------------------------------------- */
/* Server profile, hosting.                                          */

void qa_app_startup_selection_select_server_profile(qa_app_startup_selection_model *m, const char *path) {
  free(m->server_profile_path);
  m->server_profile_path = xdup(path);
  m->has_server_profile = true;
}
/* TS applySelectedServerProfile, in place. */
static void apply_selected_server_profile(qa_app_startup_selection_model *m, qa_app_options *options) {
  if (!m->has_server_profile) return;
  options->server_profile = NULL;
  set_text(&options->server_profile_path, m->server_profile_path);
}
qa_app_startup_hosting qa_app_startup_selection_hosting(qa_app_startup_selection_model *m) {
  const char *kind = m->initial.network_kind;
  const qa_catalog_discovered_product *product = product_field(m, F_PRODUCT);
  qa_app_startup_hosting out;
  memset(&out, 0, sizeof out);
  if (product->expectation.family == QA_GAME_FAMILY_Q1 && !eq(edition_of(product), "quakeworld")) {
    out.has_q1_protocol = true;
    out.q1_protocol = m->initial.has_q1_protocol ? m->initial.q1_protocol : qa_default_netquake_profile(15);
  }
  if (network_is_server(kind)) {
    out.kind = eq(kind, "q2-server") ? "native-server" : eq(kind, "native-server") ? "native-server" : "unified-server";
    out.port = m->initial.network_port;
    return out;
  }
  out.kind = "offline";
  out.port = product->expectation.family == QA_GAME_FAMILY_Q3 ? 27960
      : product->expectation.family == QA_GAME_FAMILY_Q2
        ? (eq(qa_app_live_q2_protocol(&m->initial, eq(edition_of(product), "rerelease")).kind, "q2-kex") ? 5069 : 27910)
      : eq(edition_of(product), "quakeworld") ? 27500 : 26000;
  return out;
}
void qa_app_startup_selection_select(qa_app_startup_selection_model *m, const char *field, const char *id);
void qa_app_startup_selection_set_hosting(qa_app_startup_selection_model *m, qa_app_startup_hosting hosting) {
  if (!(isfinite(hosting.port) && trunc(hosting.port) == hosting.port) || hosting.port < 1 || hosting.port > 65535)
    fail("Port must be a whole number from 1 to 65535.");
  if (!eq(hosting.kind, "offline") && eq(value(m, F_MODE), "singleplayer"))
    qa_app_startup_selection_select(m, "mode", product_field(m, F_PRODUCT)->expectation.family == QA_GAME_FAMILY_Q3 ? "deathmatch" : "coop");
  const char *previous = m->initial.network_kind;
  char *host = xdup(network_is_server(previous) && m->initial.network_host != NULL ? m->initial.network_host : "0.0.0.0");
  /* The object literal reads this.hosting() and this.initial before the assignment. */
  bool keep_q1 = eq(hosting.kind, "native-server") && qa_app_startup_selection_hosting(m).has_q1_protocol && hosting.has_q1_protocol;
  bool keep_transport = eq(hosting.kind, "native-server") && m->initial.network_transport_kind != NULL
      && m->initial.network_transport_kind[0] != '\0';
  if (!keep_transport) {
    m->initial.network_transport_kind = "";
    free(m->initial.network_transport_relay);
    m->initial.network_transport_relay = NULL;
  }
  m->initial.has_q1_protocol = keep_q1;
  if (keep_q1) m->initial.q1_protocol = hosting.q1_protocol;
  free(m->initial.network_host); m->initial.network_host = NULL;
  free(m->initial.network_remote); m->initial.network_remote = NULL;
  if (eq(hosting.kind, "offline")) {
    m->initial.network_kind = "offline";
    m->initial.network_port = 0;
    free(host);
  } else {
    m->initial.network_kind = eq(hosting.kind, "native-server") ? "native-server" : "unified-server";
    m->initial.network_host = host;
    m->initial.network_port = hosting.port;
  }
}

/* ---------------------------------------------------------------- */
/* Mods (TS prepareMods and the ModMenuService).                     */

static void enabled_initial_mods(const qa_app_options *initial, qa_mod_selection **out, size_t *count) {
  size_t n = initial->mod_count + (initial->has_weapon_behavior ? 1 : 0);
  qa_mod_selection *list = xcalloc(n, sizeof *list);
  for (size_t i = 0; i < initial->mod_count; i++) list[i] = initial->mods[i];
  if (initial->has_weapon_behavior) {
    list[initial->mod_count].product = initial->weapon_behavior_product;
    list[initial->mod_count].id = initial->weapon_behavior_id;
  }
  *out = list; *count = n;
}
static qa_mod_description *choice_descriptions(const qa_app_mod_choices *choices) {
  qa_mod_description *descriptions = xcalloc(choices->count, sizeof *descriptions);
  for (size_t i = 0; i < choices->count; i++) descriptions[i] = choices->items[i].description;
  return descriptions;
}
/* TS prepareMods(catalog = this.catalog). */
static void prepare_mods(qa_app_startup_selection_model *m, qa_content_catalog *catalog) {
  void *backing = NULL;
  const qa_app_mod_choices *choices = qa_app_application_mod_choices_native(catalog, &backing);
  retain(&m->owned, backing, qa_app_mod_choices_backing_free);
  qa_mod_description *descriptions = choice_descriptions(choices);
  if (!m->has_selected_mods) {
    qa_mod_selection *initial = NULL; size_t initial_count = 0;
    enabled_initial_mods(&m->initial, &initial, &initial_count);
    if (!qa_mod_selection_set_init(&m->selected_mods, descriptions, choices->count, initial, initial_count))
      qa_range_error("Out of memory");
    m->has_selected_mods = true;
    free(initial);
  } else qa_mod_selection_set_refresh(&m->selected_mods, descriptions, choices->count);
  free(descriptions);
  m->mod_choices = choices;
}
static const qa_ui_mod_menu_row *mod_rows(void *ctx, size_t *out_count) {
  qa_app_startup_selection_model *m = ctx;
  arena_reset(&m->mod_rows);
  size_t count = 0;
  const qa_mod_description *entries = m->has_selected_mods ? qa_mod_selection_set_entries(&m->selected_mods, &count) : NULL;
  qa_ui_mod_menu_row *rows = arena_alloc(&m->mod_rows, (count ? count : 1) * sizeof *rows);
  size_t at = 0;
  for (size_t i = 0; i < count; i++) {
    if (!eq(entries[i].purpose, "addition")) continue;
    char *key = qa_mod_selection_key(entries[i].selection);
    if (!key) qa_range_error("Out of memory");
    rows[at].id = arena_keep(&m->mod_rows, key);
    rows[at].title = arena_dup(&m->mod_rows, entries[i].title);
    rows[at].source = arena_dup(&m->mod_rows, entries[i].source_title);
    rows[at].enabled = qa_mod_selection_set_has(&m->selected_mods, entries[i].selection);
    rows[at].unavailable = entries[i].is_available ? NULL : arena_dup(&m->mod_rows, entries[i].unavailable_reason);
    at++;
  }
  *out_count = at;
  return rows;
}
typedef struct set_enabled_call { qa_app_startup_selection_model *m; const char *id; bool enabled; } set_enabled_call;
static void set_enabled_body(void *ctx) {
  set_enabled_call *call = ctx;
  if (!call->m->has_selected_mods) fail("Mod discovery has not finished");
  qa_mod_selection selection = {0};
  if (!qa_read_mod_selection(call->id, &selection)) qa_range_error("Out of memory");
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, (void (*)(void *))qa_mod_selection_free, &selection);
  qa_mod_selection_set_set_enabled(&call->m->selected_mods, selection, call->enabled);
  qa_cleanup_pop(&cleanup);
  qa_mod_selection_free(&selection);
  set_text(&call->m->mod_status, "");
}
static void mod_set_enabled(void *ctx, const char *id, bool enabled) {
  set_enabled_call call = {ctx, id, enabled};
  qa_caught_error caught;
  if (!qa_try(set_enabled_body, &call, &caught)) set_text(&call.m->mod_status, caught.message);
}
static void prepare_hook_styles(qa_app_startup_selection_model *m, qa_content_catalog *catalog);
static qa_content_catalog *discover_catalog(qa_content_catalog *from, bool generation_next);
typedef struct refresh_call { qa_app_startup_selection_model *m; } refresh_call;
static void refresh_body(void *ctx) {
  refresh_call *call = ctx;
  qa_app_startup_selection_model *m = call->m;
  qa_content_catalog *catalog = discover_catalog(m->current, true);
  retain(&m->owned, catalog, catalog_release);
  prepare_mods(m, catalog);
  prepare_hook_styles(m, catalog);
  m->current = catalog;
  set_text(&m->mod_status, "");
}
/* TS mods.refresh(): the discovery promise runs to completion here. */
static void mod_refresh(void *ctx) {
  qa_app_startup_selection_model *m = ctx;
  if (m->refreshing_mods) return;
  m->refreshing_mods = true;
  set_text(&m->mod_status, "Reading installed mods...");
  refresh_call call = {m};
  qa_caught_error caught;
  if (!qa_try(refresh_body, &call, &caught)) set_text(&m->mod_status, caught.message);
  m->refreshing_mods = false;
}
static const char *mod_status(void *ctx) { return ((qa_app_startup_selection_model *)ctx)->mod_status; }
const qa_ui_mod_menu_service *qa_app_startup_selection_mods(qa_app_startup_selection_model *m) { return &m->mod_service; }

/* ---------------------------------------------------------------- */
/* Discovery.                                                        */

/* discoverInstalledContent({corpusRoot, generation + 1, addonContentRoots,
 * userContentRoot?, discoverMods: true}) from an existing catalog. */
static qa_content_catalog *discover_catalog(qa_content_catalog *from, bool generation_next) {
  qa_content_catalog *catalog = xcalloc(1, sizeof *catalog);
  qa_discover_content_options options;
  memset(&options, 0, sizeof options);
  options.corpus_root = from->corpus_root;
  options.user_content_root = from->user_content_root;
  options.addon_content_roots = from->addon_content_roots;
  options.addon_content_root_count = from->addon_content_root_count;
  options.has_generation = generation_next;
  options.generation = (double)from->generation + 1;
  options.discover_mods = true;
  options.inspector = qa_app_default_archive_inspector();
  if (!qa_discover_installed_content(&options, catalog)) { free(catalog); qa_range_error("Out of memory"); }
  return catalog;
}

/* ---------------------------------------------------------------- */
/* Hook styles (TS hookStyles / prepareHookStyles).                  */

static char *hex_text(const char *text) {
  static const char digits[] = "0123456789abcdef";
  size_t length = strlen(text);
  char *out = xmalloc(length * 2 + 1);
  for (size_t i = 0; i < length; i++) {
    out[i * 2] = digits[((unsigned char)text[i] >> 4) & 15];
    out[i * 2 + 1] = digits[(unsigned char)text[i] & 15];
  }
  out[length * 2] = '\0';
  return out;
}
typedef struct grapple_host_state {
  qa_catalog_mount_list list; qa_mount_id *order; char *plan_id; bool open;
  char *artifact_path, *digest, *revision;
} grapple_host_state;
static void grapple_plan_release(grapple_host_state *state) {
  if (!state->open) return;
  qa_content_catalog_mount_list_free(&state->list);
  free(state->order); free(state->plan_id);
  state->order = NULL; state->plan_id = NULL; state->open = false;
}
/* TS openPlan({id: createMountPlanId("qvm-grapple", hex(content)), mounts, defaultOrder}). */
static qa_content_mounted *grapple_open_plan(void *ctx, const qa_content_catalog *catalog, qa_content_id content) {
  grapple_host_state *state = ctx;
  grapple_plan_release(state);
  if (!qa_content_catalog_mounts_for((qa_content_catalog *)catalog, content, &state->list)) qa_range_error("Out of memory");
  state->open = true;
  state->order = xcalloc(state->list.count, sizeof *state->order);
  for (size_t i = 0; i < state->list.count; i++)
    state->order[i] = state->list.mounts[i].kind == QA_CONTENT_MOUNT_ARCHIVE
        ? state->list.mounts[i].as.archive.identity.id : state->list.mounts[i].as.loose.identity.id;
  char *hex = hex_text(content);
  state->plan_id = qa_create_mount_plan_id("qvm-grapple", hex);
  free(hex);
  if (!state->plan_id) qa_range_error("Out of memory");
  qa_resolved_mount_plan plan = {state->plan_id, state->list.mounts, state->list.count, state->order, state->list.count, NULL, 0};
  qa_open_mount_options options;
  memset(&options, 0, sizeof options);
  qa_content_mounted *mounted = qa_open_mount_plan(&plan, &options);
  if (!mounted) qa_range_error("Out of memory");
  return mounted;
}
/* TS q3GrappleProfile(artifact) over the content profile's artifact view.
 * The module identity strings are copied: the selection outlives the
 * mounts that resolved them. */
static bool grapple_profile_for(void *ctx, const qa_resolved_qvm_artifact *artifact, qa_qvm_grapple_definition *out) {
  grapple_host_state *state = ctx;
  qa_q3_equipment_qvm_artifact view;
  memset(&view, 0, sizeof view);
  qa_q3_equipment_known_artifact known = {0};
  view.module = artifact->module;
  view.role = artifact->role;
  view.abi_profile = artifact->has_abi_profile ? artifact->abi_profile : NULL;
  if (artifact->known != NULL) { known.product = artifact->known->product; view.known = &known; }
  view.image.initialized_data = artifact->image.initialized_data;
  view.image.initialized_data_length = artifact->image.initialized_length;
  view.image.allocated_data_length = (double)artifact->image.allocated_data_length;
  view.image.data_length = (double)artifact->image.data_length;
  view.image.literal_length = (double)artifact->image.literal_length;
  view.image.bss_length = (double)artifact->image.bss_length;
  qa_q3_equipment_qvm_instruction *instructions = xcalloc(artifact->image.instruction_count, sizeof *instructions);
  for (size_t i = 0; i < artifact->image.instruction_count; i++) {
    instructions[i].byte_offset = (double)artifact->image.instructions[i].byte_offset;
    instructions[i].opcode = (double)artifact->image.instructions[i].opcode;
    instructions[i].operand_width = (double)artifact->image.instructions[i].operand_width;
    instructions[i].operand = (double)artifact->image.instructions[i].operand;
  }
  view.image.instructions = instructions;
  view.image.instruction_count = artifact->image.instruction_count;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, instructions);
  bool found = qa_q3_grapple_profile(&view, out);
  qa_cleanup_pop(&cleanup);
  free(instructions);
  if (!found) return false;
  free(state->artifact_path); free(state->digest); free(state->revision);
  state->artifact_path = xdup(out->module.artifact_path);
  state->digest = xdup(out->module.digest);
  state->revision = xdup(out->module.revision);
  out->module.artifact_path = state->artifact_path;
  out->module.digest = state->digest;
  out->module.revision = state->revision;
  return true;
}
/* TS prepareHookStyles(catalog = this.catalog). */
static void prepare_hook_styles(qa_app_startup_selection_model *m, qa_content_catalog *catalog) {
  qvm_style **styles = NULL; size_t count = 0;
  for (size_t i = 0; i < catalog->product_count; i++) {
    const qa_catalog_discovered_product *product = &catalog->products[i];
    if (product->expectation.family != QA_GAME_FAMILY_Q3 || !installed(product)) continue;
    grapple_host_state state;
    memset(&state, 0, sizeof state);
    qa_app_qvm_grapple_host host = {&state, grapple_open_plan, grapple_profile_for};
    qvm_style *style = xcalloc(1, sizeof *style);
    bool found = qa_app_application_qvm_grapple_selection(catalog, product->id, &host, &style->selection,
        &style->profile, &style->module_id);
    grapple_plan_release(&state);
    bool duplicate = false;
    for (size_t j = 0; found && j < count; j++) if (eq(styles[j]->profile.id, style->profile.id)) duplicate = true;
    if (!found || duplicate) {
      free(state.artifact_path); free(state.digest); free(state.revision);
      qvm_style_free(style);
      continue;
    }
    style->artifact_path = state.artifact_path;
    style->digest = state.digest;
    style->revision = state.revision;
    style->selection.profile = &style->profile;
    styles = xrealloc(styles, (count + 1) * sizeof *styles);
    styles[count++] = style;
  }
  qvm_styles_free(m);
  m->qvm_styles = styles;
  m->qvm_style_count = count;
}
/* TS hookStyles(): grappleStyles(catalog, product("product")) + the QVM styles. */
typedef struct hook_styles { qa_grapple_style *native; size_t native_count; qa_grapple_style *all; size_t count; } hook_styles;
static hook_styles hook_styles_of(qa_app_startup_selection_model *m) {
  hook_styles out;
  memset(&out, 0, sizeof out);
  out.native = qa_grapple_styles(m->current, product_field(m, F_PRODUCT), &out.native_count);
  if (out.native == NULL && out.native_count > 0) qa_range_error("Out of memory");
  out.count = out.native_count + m->qvm_style_count;
  out.all = xcalloc(out.count, sizeof *out.all);
  for (size_t i = 0; i < out.native_count; i++) out.all[i] = out.native[i];
  for (size_t i = 0; i < m->qvm_style_count; i++) {
    qa_grapple_style *style = &out.all[out.native_count + i];
    style->id = m->qvm_styles[i]->profile.id;
    style->title = m->qvm_styles[i]->profile.title;
    style->selection = m->qvm_styles[i]->selection;
    style->unavailable = NULL;
  }
  return out;
}
static void hook_styles_free(hook_styles *styles) {
  qa_grapple_styles_free(styles->native, styles->native_count);
  free(styles->all);
  memset(styles, 0, sizeof *styles);
}
static void hook_styles_cleanup(void *arg) { hook_styles_free(arg); }

/* ---------------------------------------------------------------- */
/* Q3 product and Team Arena preparation.                            */

/* TS prepareQ3Catalog(). */
static void prepare_q3_catalog(qa_app_startup_selection_model *m) {
  qa_content_catalog *catalog = m->current;
  const qa_catalog_discovered_product *product = NULL;
  for (size_t i = 0; i < catalog->product_count && product == NULL; i++)
    if (eq(catalog->products[i].expectation.id, m->initial.product) && catalog->products[i].expectation.family == QA_GAME_FAMILY_Q3)
      product = &catalog->products[i];
  for (size_t i = 0; i < catalog->product_count && product == NULL; i++)
    if (catalog->products[i].expectation.family == QA_GAME_FAMILY_Q3 && installed(&catalog->products[i]))
      product = &catalog->products[i];
  if (product == NULL) return;
  qa_app_prepared_q3_product prepared;
  void *backing = NULL;
  qa_app_prepare_q3_application_product_native(catalog, product->id, &m->initial, &prepared, &backing);
  retain(&m->owned, backing, qa_app_q3_product_backing_free);
  m->current = (qa_content_catalog *)prepared.catalog;
  if (prepared.has_q3_product) { m->initial.has_q3_product = true; m->initial.q3_product = prepared.q3_product; }
}
typedef struct team_arena_reader { qa_content_mounted *mounted; } team_arena_reader;
/* TS mounts.read(path) as latin1 text (a missing file throws "Resource not found"). */
static char *team_arena_read(void *ctx, const char *path) {
  team_arena_reader *reader = ctx;
  size_t length = 0;
  uint8_t *bytes = qa_content_mounted_read_path(reader->mounted, path, &length);
  char *text = xmalloc(length + 1);
  if (length > 0) memcpy(text, bytes, length);
  text[length] = '\0';
  free(bytes);
  return text;
}
static char **team_arena_list(void *ctx, size_t *out_count) {
  team_arena_reader *reader = ctx;
  *out_count = 0;
  char **files = qa_content_mounted_list_files(reader->mounted, "scripts", ".team", out_count);
  if (!files && *out_count > 0) qa_range_error("Out of memory");
  return files;
}
static void product_mounts_cleanup(void *arg) { qa_app_product_mounts_destroy(arg); }
/* TS prepareTeamArena() with loadTeamArenaCampaign(catalog, initial). */
static void prepare_team_arena(qa_app_startup_selection_model *m) {
  if (m->team_campaign != NULL) return;
  const qa_catalog_discovered_product *product = find_product_by_id(m->current, "q3-missionpack");
  if (product == NULL || !installed(product)) return;
  qa_app_prepared_q3_product prepared;
  void *backing = NULL;
  qa_app_prepare_q3_application_product_native(m->current, "q3-missionpack", &m->initial, &prepared, &backing);
  retain(&m->owned, backing, qa_app_q3_product_backing_free);
  if (!prepared.has_q3_product) fail("Team Arena requires a Q3 product policy");
  qa_content_catalog *catalog = (qa_content_catalog *)prepared.catalog;
  const qa_catalog_discovered_product *pack = qa_content_catalog_require(catalog, "q3-missionpack");
  qa_app_product_mounts *mounts = qa_app_open_product_mounts(catalog, pack->id, "team-arena-catalog",
      prepared.q3_product.restriction.kind == QA_Q3_MOUNT_DEMO);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, product_mounts_cleanup, mounts);
  team_arena_reader reader = {qa_app_product_mounts_mounted(mounts)};
  qa_q3_team_arena_catalog_policy files = qa_q3_team_arena_catalog_policy_make(prepared.q3_product.policy);
  qa_app_team_arena_campaign_host host = {&reader, team_arena_read, team_arena_list, files.additional_teams,
      files.game_info, files.team_info};
  qa_app_team_arena_campaign *campaign = qa_app_load_team_arena_campaign(host);
  qa_cleanup_pop(&cleanup);
  qa_app_product_mounts_destroy(mounts);
  m->team_campaign = campaign;
  m->current = catalog;
  m->initial.has_q3_product = true;
  m->initial.q3_product = prepared.q3_product;
}
const qa_ui_choice *qa_app_startup_selection_team_choices(qa_app_startup_selection_model *m, size_t *out_count) {
  arena_reset(&m->team_arena);
  size_t count = 0;
  char **keys = m->team_campaign != NULL ? qa_app_team_arena_team_choices(m->team_campaign, &count) : NULL;
  qa_ui_choice *out = arena_alloc(&m->team_arena, (count ? count : 1) * sizeof *out);
  for (size_t i = 0; i < count; i++) {
    out[i].id = arena_dup(&m->team_arena, keys[i]);
    char *label = arena_dup(&m->team_arena, keys[i]);
    if (label[0] >= 'a' && label[0] <= 'z') label[0] = (char)(label[0] - 'a' + 'A');
    out[i].label = label;
  }
  free(keys);
  *out_count = count;
  return out;
}
void qa_app_startup_selection_team_read(qa_app_startup_selection_model *m, const char **out_player, const char **out_opponent) {
  *out_player = m->team_player;
  *out_opponent = m->team_opponent;
}
void qa_app_startup_selection_team_write(qa_app_startup_selection_model *m, const char *side, const char *team) {
  char *name = lower_dup(team);
  bool known = false;
  size_t count = 0;
  char **keys = m->team_campaign != NULL ? qa_app_team_arena_team_choices(m->team_campaign, &count) : NULL;
  for (size_t i = 0; i < count; i++) if (eq(keys[i], name)) known = true;
  free(keys);
  if (!known) { free(name); fail("Unknown authored Team Arena team"); }
  if (eq(side, "player")) { free(m->team_player); m->team_player = name; }
  else { free(m->team_opponent); m->team_opponent = name; }
}

/* ---------------------------------------------------------------- */
/* Files, models, skins.                                             */

static const smap *files_of(qa_app_startup_selection_model *m, const qa_catalog_discovered_product *product);
/* TS collectFiles(product). */
static smap *collect_files(qa_app_startup_selection_model *m, const qa_catalog_discovered_product *product) {
  smap *paths = smap_new(NULL);
  for (size_t i = 0; i < product->archive_count; i++)
    for (size_t j = 0; j < product->archives[i].entry_count; j++) {
      char *lower = lower_dup(product->archives[i].entries[j].path);
      if (!smap_has(paths, lower)) smap_set(paths, lower, NULL);
      free(lower);
    }
  const slist *loose = smap_get(m->loose_models, product->expectation.id);
  if (loose != NULL)
    for (size_t i = 0; i < loose->count; i++) if (!smap_has(paths, loose->items[i])) smap_set(paths, loose->items[i], NULL);
  if (product->expectation.base_product != NULL) {
    const smap *base = files_of(m, cat_product(m, product->expectation.base_product));
    for (size_t i = 0; i < base->count; i++) if (!smap_has(paths, base->keys[i])) smap_set(paths, base->keys[i], NULL);
  }
  return paths;
}
/* TS files(product): rebuilt only when the product or its loose list changes. */
static const smap *files_of(qa_app_startup_selection_model *m, const qa_catalog_discovered_product *product) {
  const slist *loose = smap_get(m->loose_models, product->expectation.id);
  unsigned long generation = loose != NULL ? loose->generation : 0;
  for (size_t i = 0; i < m->file_set_count; i++)
    if (m->file_sets[i].product == product) {
      if (m->file_sets[i].loose == generation) return m->file_sets[i].paths;
      smap *paths = collect_files(m, product);
      smap_destroy(m->file_sets[i].paths);
      m->file_sets[i].paths = paths;
      m->file_sets[i].loose = generation;
      return paths;
    }
  smap *paths = collect_files(m, product);
  if (m->file_set_count == m->file_set_cap) {
    m->file_set_cap = m->file_set_cap ? m->file_set_cap * 2 : 16;
    m->file_sets = xrealloc(m->file_sets, m->file_set_cap * sizeof *m->file_sets);
  }
  m->file_sets[m->file_set_count].product = product;
  m->file_sets[m->file_set_count].loose = generation;
  m->file_sets[m->file_set_count].paths = paths;
  m->file_set_count++;
  return paths;
}
/* Path segments split on '/'. Fills up to `capacity`; returns the count. */
static size_t split_path(const char *path, char *buffer, size_t buffer_size, const char **segments, size_t capacity) {
  size_t length = strlen(path);
  if (length + 1 > buffer_size) return 0;
  memcpy(buffer, path, length + 1);
  size_t count = 0;
  char *start = buffer;
  for (char *p = buffer;; p++) {
    if (*p == '/' || *p == '\0') {
      bool end = *p == '\0';
      *p = '\0';
      if (count < capacity) segments[count] = start;
      count++;
      if (end) break;
      start = p + 1;
    }
  }
  return count;
}
/* /^models\/players\/(?:characters\/)?([^/]+)\/<leaf>$/ with leaf test. Returns
 * the captured name in `name` (buffer) or false. */
static bool q3_player_path(const char *path, bool (*leaf)(const char *segment, void *ctx), void *ctx,
                           char *name, size_t name_size) {
  char buffer[1024];
  const char *s[6];
  size_t count = split_path(path, buffer, sizeof buffer, s, 6);
  if (count < 4 || count > 5 || strcmp(s[0], "models") != 0 || strcmp(s[1], "players") != 0) return false;
  if (count == 5 && strcmp(s[2], "characters") != 0) return false;
  const char *captured = s[count - 2];
  if (captured[0] == '\0' || !leaf(s[count - 1], ctx)) return false;
  snprintf(name, name_size, "%s", captured);
  return true;
}
static bool leaf_lower_md3(const char *segment, void *ctx) { (void)ctx; return strcmp(segment, "lower.md3") == 0; }
/* lower_([^/]+)\.skin */
static bool leaf_lower_skin(const char *segment, void *ctx) {
  size_t length = strlen(segment);
  if (length <= strlen("lower_") + strlen(".skin") || !starts_with(segment, "lower_") || !ends_with(segment, ".skin")) return false;
  char *skin = ctx;
  size_t size = length - strlen("lower_") - strlen(".skin");
  memcpy(skin, segment + strlen("lower_"), size);
  skin[size] = '\0';
  return true;
}
static int compare_text(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
/* TS modelsFor(product). */
static void models_for(qa_app_startup_selection_model *m, const qa_catalog_discovered_product *product, cvec *out) {
  const smap *paths = files_of(m, product);
  if (product->expectation.family == QA_GAME_FAMILY_Q1) {
    cvec_push(out, choice3("player", "Quake player", smap_has(paths, "progs/player.mdl") ? NULL : "Player model not installed"));
    return;
  }
  smap *models = smap_new(NULL);
  bool q3 = product->expectation.family == QA_GAME_FAMILY_Q3;
  for (size_t i = 0; i < paths->count; i++) {
    const char *path = paths->keys[i];
    char name[1024];
    bool found = false;
    if (q3) found = q3_player_path(path, leaf_lower_md3, NULL, name, sizeof name);
    else {
      char buffer[1024];
      const char *s[4];
      size_t count = split_path(path, buffer, sizeof buffer, s, 4);
      if (count == 3 && strcmp(s[0], "players") == 0 && s[1][0] != '\0' && strcmp(s[2], "tris.md2") == 0) {
        snprintf(name, sizeof name, "%s", s[1]);
        found = true;
      }
    }
    if (!found) continue;
    if (q3) {
      char *upper = xprintf("models/players/%s/upper.md3", name), *upper2 = xprintf("models/players/characters/%s/upper.md3", name);
      char *head = xprintf("models/players/%s/head.md3", name), *head2 = xprintf("models/players/heads/%s/%s.md3", name, name);
      bool complete = (smap_has(paths, upper) || smap_has(paths, upper2)) && (smap_has(paths, head) || smap_has(paths, head2));
      free(upper); free(upper2); free(head); free(head2);
      if (!complete) continue;
    }
    if (!smap_has(models, name)) smap_set(models, name, NULL);
  }
  char **names = xcalloc(models->count, sizeof *names);
  for (size_t i = 0; i < models->count; i++) names[i] = models->keys[i];
  qa_js_sort(names, models->count, sizeof *names, compare_text);
  /* The choice strings must outlive the set: keep them in a list owned by
   * the caller's cvec through duplication at finish time. */
  for (size_t i = 0; i < models->count; i++) cvec_push(out, choice1(xdup(names[i])));
  free(names);
  smap_destroy(models);
}
/* Frees the strings models_for duplicated (non-q1 results). */
static void models_free_strings(const qa_catalog_discovered_product *product, cvec *list) {
  if (product->expectation.family == QA_GAME_FAMILY_Q1) return;
  for (size_t i = 0; i < list->count; i++) free((char *)list->items[i].id);
}
/* TS models(): cached per character product. Borrowed persistent list. */
static const pchoices *models_of(qa_app_startup_selection_model *m) {
  const qa_catalog_discovered_product *product = product_field(m, F_CHARACTER);
  const pchoices *existing = smap_get(m->model_choices, product->expectation.id);
  if (existing != NULL) return existing;
  cvec list = {0};
  models_for(m, product, &list);
  pchoices *result = pchoices_from(list.items, list.count);
  models_free_strings(product, &list);
  free(list.items);
  smap_set(m->model_choices, product->expectation.id, result);
  return result;
}
/* First model choice of modelsFor(product) whose id equals `id` (NULL when
 * absent); *first receives modelsFor(product)[0]?.id. Strings copied into `a`. */
static const char *model_lookup(qa_app_startup_selection_model *m, const qa_catalog_discovered_product *product,
                                const char *id, const char **unavailable, const char **first, arena *a) {
  cvec list = {0};
  models_for(m, product, &list);
  const char *found = NULL;
  if (unavailable) *unavailable = NULL;
  if (first) *first = list.count > 0 ? arena_dup(a, list.items[0].id) : NULL;
  for (size_t i = 0; i < list.count; i++)
    if (eq(list.items[i].id, id)) {
      found = arena_dup(a, list.items[i].id);
      if (unavailable) *unavailable = arena_dup(a, list.items[i].unavailable);
      break;
    }
  models_free_strings(product, &list);
  free(list.items);
  return found;
}
/* TS skinsFor(product, modelValue); choice strings in `a`. */
static void skins_for(qa_app_startup_selection_model *m, const qa_catalog_discovered_product *product,
                      const char *model_value, arena *a, cvec *out) {
  qa_game_family family = product->expectation.family;
  char *model = lower_dup(model_value);
  cvec_push(out, choice3("", "Default", NULL));
  if (family == QA_GAME_FAMILY_Q1 || model[0] == '\0') { free(model); return; }
  smap *skins = smap_new(NULL);
  const smap *paths = files_of(m, product);
  for (size_t i = 0; i < paths->count; i++) {
    const char *path = paths->keys[i];
    if (family == QA_GAME_FAMILY_Q2) {
      char buffer[1024];
      const char *s[4];
      size_t count = split_path(path, buffer, sizeof buffer, s, 4);
      if (count != 3 || strcmp(s[0], "players") != 0 || s[1][0] == '\0') continue;
      size_t length = strlen(s[2]);
      if (length <= 4 || !ends_with(s[2], ".pcx")) continue;
      char skin[1024];
      snprintf(skin, sizeof skin, "%.*s", (int)(length - 4), s[2]);
      if (strcmp(s[1], model) == 0 && !ends_with(skin, "_i") && strcmp(skin, "weapon") != 0 && !smap_has(skins, skin))
        smap_set(skins, skin, NULL);
    } else {
      char name[1024], skin[1024];
      if (!q3_player_path(path, leaf_lower_skin, skin, name, sizeof name)) continue;
      if (strcmp(name, model) == 0 && !smap_has(skins, skin)) smap_set(skins, skin, NULL);
    }
  }
  smap_delete(skins, "default");
  char **names = xcalloc(skins->count, sizeof *names);
  for (size_t i = 0; i < skins->count; i++) names[i] = skins->keys[i];
  qa_js_sort(names, skins->count, sizeof *names, compare_text);
  for (size_t i = 0; i < skins->count; i++) cvec_push(out, choice1(arena_dup(a, names[i])));
  free(names);
  smap_destroy(skins);
  free(model);
}
static void skins_of(qa_app_startup_selection_model *m, arena *a, cvec *out) {
  skins_for(m, product_field(m, F_CHARACTER), value(m, F_MODEL), a, out);
}
static bool cvec_has(const cvec *list, const char *id) {
  for (size_t i = 0; i < list->count; i++) if (eq(list->items[i].id, id)) return true;
  return false;
}
static const char *const q1_color_names[14] = {"White", "Brown", "Slate", "Olive", "Red", "Ochre", "Rust", "Peach",
  "Mauve", "Rose", "Khaki", "Teal", "Yellow", "Blue"};
static const char *const color_ids[14] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13"};
static void color_choices(cvec *out) {
  for (int i = 0; i < 14; i++) cvec_push(out, choice3(color_ids[i], q1_color_names[i], NULL));
}
static const char *default_model(qa_game_family family) {
  return family == QA_GAME_FAMILY_Q1 ? "player" : family == QA_GAME_FAMILY_Q2 ? "male" : "sarge";
}
static double preview_color(const char *override, const char *current) {
  double number = qa_js_string_to_number(override != NULL ? override : current);
  double truncated = trunc(number);
  if (isnan(truncated) || truncated == 0) truncated = 0; /* `|| 0` */
  return qa_js_min(13, qa_js_max(0, truncated));
}
qa_app_startup_character_preview qa_app_startup_selection_character_preview(qa_app_startup_selection_model *m,
    const qa_app_startup_preview_overrides *overrides) {
  qa_app_startup_preview_overrides none = {0};
  const qa_app_startup_preview_overrides *o = overrides != NULL ? overrides : &none;
  arena *a = &m->preview_arena;
  arena_reset(a);
  const qa_catalog_discovered_product *product = o->character == NULL ? product_field(m, F_CHARACTER) : cat_product(m, o->character);
  qa_game_family family = product->expectation.family;
  const char *model = o->model;
  if (model == NULL) {
    if (o->character == NULL) model = arena_dup(a, value(m, F_MODEL));
    else {
      const char *selected = smap_get(m->selected_models, o->character);
      const char *preferred = selected != NULL ? selected : default_model(family);
      const char *first = NULL;
      model = model_lookup(m, product, preferred, NULL, &first, a);
      if (model == NULL) model = first != NULL ? first : "";
    }
  } else model = arena_dup(a, model);
  const char *skin = arena_dup(a, o->skin != NULL ? o->skin : value(m, F_SKIN));
  if (family == QA_GAME_FAMILY_Q1) skin = "";
  else {
    cvec skins = {0};
    skins_for(m, product, model, a, &skins);
    if (!cvec_has(&skins, skin)) skin = "";
    free(skins.items);
  }
  qa_app_startup_character_preview out = {product->id, family, model, skin,
      preview_color(o->top_color, value(m, F_TOP_COLOR)), preview_color(o->bottom_color, value(m, F_BOTTOM_COLOR))};
  return out;
}

/* ---------------------------------------------------------------- */
/* Monster rosters.                                                  */

static roster_state *roster_of(qa_app_startup_selection_model *m) {
  roster_state *roster = smap_get(m->rosters, value(m, F_PRODUCT));
  if (roster == NULL) {
    roster = xcalloc(1, sizeof *roster);
    roster->source = xdup("native");
    roster->def = xdup("native");
    roster->by_classname = smap_new(free);
    smap_set(m->rosters, value(m, F_PRODUCT), roster);
  }
  return roster;
}
/* TS cacheMonsterClasses(key, classnames). */
static void cache_monster_classes(qa_app_startup_selection_model *m, const char *key, const char *const *classnames, size_t count) {
  slist *list = xcalloc(1, sizeof *list);
  for (size_t i = 0; i < count; i++) slist_push(list, classnames[i]);
  smap_set(m->map_classnames, key, list);
  smap_clear(m->eligible_maps);
  smap *counts = smap_new(NULL);
  for (size_t i = 0; i < count; i++)
    if (starts_with(classnames[i], "monster_"))
      smap_set(counts, classnames[i], (void *)((uintptr_t)smap_get(counts, classnames[i]) + 1));
  smap_set(m->monster_classes, key, counts);
}
static const char *const monster_name_keys[] = {"monster_army", "monster_demon1", "monster_wizard", "monster_shalrath", "monster_tarbaby"};
static const char *const monster_name_values[] = {"Grunt", "Fiend", "Scrag", "Vore", "Spawn"};
/* TS monsterLabel(classname), into `a`. */
static const char *monster_label(const char *classname, arena *a) {
  for (size_t i = 0; i < 5; i++) if (strcmp(monster_name_keys[i], classname) == 0) return monster_name_values[i];
  /* classname.replace("monster_", "") replaces the first occurrence only. */
  const char *found = strstr(classname, "monster_");
  size_t length = strlen(classname);
  char *text = arena_alloc(a, length + 1);
  if (found != NULL) {
    size_t before = (size_t)(found - classname);
    memcpy(text, classname, before);
    strcpy(text + before, found + strlen("monster_"));
  } else strcpy(text, classname);
  /* split("_").map(capitalize).join(" ") */
  bool word_start = true;
  for (char *p = text; *p; p++) {
    if (*p == '_') { *p = ' '; word_start = true; continue; }
    if (word_start && *p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
    word_start = false;
  }
  return text;
}
static const char *monster_program_suffix(const char *program, arena *a) {
  return eq(program, "id1") || eq(program, "baseq2") ? "" : arena_printf(a, " %s", program);
}
static const char *monster_source_title(const char *family, const char *program, const char *title) {
  if (eq(program, "id1") || eq(program, "baseq2")) return eq(family, "q1") ? "Quake" : "Quake II";
  return title != NULL ? title : program;
}
static const char *upper_family(const char *family, arena *a) {
  char *text = arena_dup(a, family);
  for (char *p = text; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 'a' + 'A');
  return text;
}
/* catalog.products.find(family/edition/campaign of the monster source). */
static const qa_catalog_discovered_product *monster_source_product(qa_app_startup_selection_model *m,
    const qa_monster_source_definition *source) {
  for (size_t i = 0; i < m->current->product_count; i++) {
    const qa_catalog_discovered_product *product = &m->current->products[i];
    if (eq(family_text(product->expectation.family), source->family) && eq(edition_of(product), source->edition)
        && eq(campaign_of(product), source->program)) return product;
  }
  return NULL;
}
/* TS monsterChoices(). */
static void monster_choices(qa_app_startup_selection_model *m, arena *a, cvec *out) {
  cvec_push(out, choice3("native", "Keep native", NULL));
  for (size_t s = 0; s < qa_monster_source_count; s++) {
    const qa_monster_source_definition *source = &qa_monster_sources[s];
    const qa_catalog_discovered_product *product = monster_source_product(m, source);
    const smap *paths = product != NULL ? files_of(m, product) : NULL;
    for (size_t c = 0; c < source->creature_count; c++) {
      const qa_monster_creature_entry *creature = &source->creatures[c];
      const char *unavailable;
      if (product == NULL) unavailable = "Source content unavailable";
      else {
        unavailable = unavailable_of(product, a);
        if (unavailable == NULL) {
          bool all = true;
          for (size_t r = 0; r < creature->resource_count && all; r++) {
            char *lower = lower_dup(creature->resources[r]);
            all = smap_has(paths, lower);
            free(lower);
          }
          unavailable = all ? NULL : "Creature resources are not installed";
        }
      }
      cvec_push(out, choice3(arena_printf(a, "%s/%s", source->provider, creature->classname),
          arena_printf(a, "%s (%s%s, %s)", monster_label(creature->classname, a), upper_family(source->family, a),
              monster_program_suffix(source->program, a), source->edition),
          unavailable));
    }
  }
}
/* TS monsterSourceRow(). */
static qa_app_startup_selection_row monster_source_row(qa_app_startup_selection_model *m, arena *a, cvec *choices) {
  cvec_push(choices, choice3("native", "Authored campaign monsters", NULL));
  for (size_t s = 0; s < qa_monster_source_count; s++) {
    const qa_monster_source_definition *source = &qa_monster_sources[s];
    const qa_catalog_discovered_product *product = monster_source_product(m, source);
    choice_t c = choice3(source->provider,
        monster_source_title(source->family, source->program, product != NULL ? product->expectation.title : NULL),
        product == NULL ? "Source content unavailable" : unavailable_of(product, a));
    c.group = eq(source->family, "q1") ? "Quake" : eq(source->family, "q2") ? "Quake II" : "Quake III Arena";
    c.edition = source->edition;
    cvec_push(choices, c);
  }
  qa_app_startup_selection_row row = {"enemies", "Monster source", roster_of(m)->source, NULL, 0};
  return row;
}
qa_app_startup_selection_row qa_app_startup_selection_monster_source_row(qa_app_startup_selection_model *m) {
  arena *a = &m->source_row_arena;
  arena_reset(a);
  cvec choices = {0};
  qa_app_startup_selection_row row = monster_source_row(m, a, &choices);
  row.id = NULL;
  row.label = "Monster source";
  row.value = arena_dup(a, row.value);
  row.choices = cvec_finish(&choices, a, &row.choice_count);
  return row;
}
void qa_app_startup_selection_select_monster_source(qa_app_startup_selection_model *m, const char *id) {
  if (geometry(m)->expectation.family == QA_GAME_FAMILY_Q3) fail("This map has no supported authored monster roster");
  arena a = {0};
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, arena_cleanup, &a);
  cvec choices = {0};
  monster_source_row(m, &a, &choices);
  arena_keep(&a, choices.items);
  const choice_t *selected = choices_find(choices.items, choices.count, id);
  if (selected == NULL) fail("Unknown monster source");
  if (selected->unavailable != NULL) fail("%s", selected->unavailable);
  qa_cleanup_pop(&cleanup);
  arena_free(&a);
  roster_state *roster = roster_of(m);
  set_text(&roster->source, id);
  set_value(m, F_ENEMIES, "custom");
}
/* TS monsterTarget(id); classname text kept in `a`. */
static qa_monster_selection_target monster_target(qa_app_startup_selection_model *m, const char *id, arena *a) {
  qa_monster_selection_target target;
  memset(&target, 0, sizeof target);
  if (eq(id, "native")) { target.is_map_defined = true; return target; }
  const qa_monster_source_definition *source = NULL;
  for (size_t s = 0; s < qa_monster_source_count && source == NULL; s++) {
    size_t length = strlen(qa_monster_sources[s].provider);
    if (strncmp(id, qa_monster_sources[s].provider, length) == 0 && id[length] == '/') source = &qa_monster_sources[s];
  }
  if (source == NULL) fail("Unknown monster source");
  const qa_catalog_discovered_product *product = monster_source_product(m, source);
  if (product == NULL) fail("Monster source content unavailable");
  target.reference.source.provider = source->provider;
  target.reference.source.content = product->id;
  target.reference.classname = arena_dup(a, id + strlen(source->provider) + 1);
  return target;
}
/* TS effectiveMonsterRoster(includeOverrides). by_classname lives in `a`. */
static qa_enemy_selection effective_monster_roster(qa_app_startup_selection_model *m, bool include_overrides, arena *a) {
  roster_state *roster = roster_of(m);
  qa_game_family family = geometry(m)->expectation.family;
  size_t count = include_overrides ? roster->by_classname->count : 0;
  qa_monster_by_classname *by_classname = arena_alloc(a, (count ? count : 1) * sizeof *by_classname);
  for (size_t i = 0; i < count; i++) {
    by_classname[i].classname = arena_dup(a, roster->by_classname->keys[i]);
    by_classname[i].target = monster_target(m, roster->by_classname->values[i], a);
  }
  qa_monster_selection_target fallback = monster_target(m, roster->def, a);
  qa_enemy_selection out;
  memset(&out, 0, sizeof out);
  out.kind = QA_ENEMY_SELECTION_REPLACE;
  if (eq(roster->source, "native") || family == QA_GAME_FAMILY_Q3) {
    out.default_target = fallback;
    out.by_classname = by_classname;
    out.by_classname_count = count;
    return out;
  }
  const qa_monster_source_definition *source = NULL;
  for (size_t s = 0; s < qa_monster_source_count && source == NULL; s++)
    if (eq(qa_monster_sources[s].provider, roster->source)) source = &qa_monster_sources[s];
  if (source == NULL) fail("Unknown monster source");
  const qa_catalog_discovered_product *product = monster_source_product(m, source);
  if (product == NULL) fail("Monster source content unavailable");
  qa_provider_reference target = {source->provider, product->id};
  if (!qa_default_monster_roster(family_text(family), target, by_classname, count, &out)) qa_range_error("Out of memory");
  arena_keep(a, (void *)out.by_classname);
  out.default_target = fallback;
  return out;
}
static const qa_monster_selection_target *roster_target(const qa_enemy_selection *roster, const char *classname) {
  for (size_t i = 0; i < roster->by_classname_count; i++)
    if (eq(roster->by_classname[i].classname, classname)) return &roster->by_classname[i].target;
  return &roster->default_target;
}
/* TS targetLabel(target). */
static const char *target_label(const qa_monster_selection_target *target, arena *a) {
  if (target->is_map_defined) return "Keep native";
  const qa_monster_source_definition *source = NULL;
  for (size_t s = 0; s < qa_monster_source_count && source == NULL; s++)
    if (eq(qa_monster_sources[s].provider, target->reference.source.provider)) source = &qa_monster_sources[s];
  return arena_printf(a, "%s (%s%s %s)", monster_label(target->reference.classname, a),
      source != NULL ? upper_family(source->family, a) : target->reference.source.provider,
      source == NULL ? "" : monster_program_suffix(source->program, a), source != NULL ? source->edition : "");
}
typedef struct slot_sort { const smap *counts; } slot_sort;
static double count_of(const smap *counts, const char *classname) {
  return counts == NULL ? 0 : (double)(uintptr_t)smap_get(counts, classname);
}
static int compare_slots(const void *left_p, const void *right_p, void *ctx) {
  const char *left = *(const char *const *)left_p, *right = *(const char *const *)right_p;
  const slot_sort *sort = ctx;
  int order = (count_of(sort->counts, right) > 0) - (count_of(sort->counts, left) > 0);
  return order != 0 ? order : qa_js_locale_compare(left, right);
}
static const qa_catalog_content_map *find_map_exact(const qa_catalog_content_map **maps, size_t count, const char *path) {
  for (size_t i = 0; i < count; i++) if (eq(maps[i]->path, path)) return maps[i];
  return NULL;
}
static const qa_catalog_content_map *find_map_ci(const qa_catalog_content_map **maps, size_t count, const char *path) {
  for (size_t i = 0; i < count; i++) if (equals_ci(maps[i]->path, path)) return maps[i];
  return NULL;
}
/* TS monsterRosterRows(); rows and strings in `a`. */
static qa_app_monster_roster_row *monster_roster_rows(qa_app_startup_selection_model *m, arena *a, size_t *out_count) {
  size_t map_count = 0;
  const qa_catalog_content_map **maps = maps_for(m, geometry(m)->id, &map_count);
  arena_keep(a, (void *)maps);
  const qa_catalog_content_map *map = find_map_exact(maps, map_count, value(m, F_MAP));
  const smap *counts = NULL;
  if (map != NULL) { char *key = map_key(map); counts = smap_get(m->monster_classes, key); free(key); }
  roster_state *roster = roster_of(m);
  cvec choices = {0};
  monster_choices(m, a, &choices);
  size_t choice_count = 0;
  const choice_t *shared = cvec_finish(&choices, a, &choice_count);
  qa_enemy_selection defaults = effective_monster_roster(m, false, a);
  qa_enemy_selection effective = effective_monster_roster(m, true, a);
  smap *slots = smap_new(NULL);
  size_t slot_count = 0;
  const qa_monster_roster_slot *campaign = qa_campaign_monster_slots(geometry(m)->expectation.family, &slot_count);
  for (size_t i = 0; i < slot_count; i++) if (!smap_has(slots, campaign[i].classname)) smap_set(slots, campaign[i].classname, NULL);
  if (counts != NULL) for (size_t i = 0; i < counts->count; i++) if (!smap_has(slots, counts->keys[i])) smap_set(slots, counts->keys[i], NULL);
  for (size_t i = 0; i < roster->by_classname->count; i++)
    if (!smap_has(slots, roster->by_classname->keys[i])) smap_set(slots, roster->by_classname->keys[i], NULL);
  char **sorted = arena_alloc(a, (slots->count ? slots->count : 1) * sizeof *sorted);
  for (size_t i = 0; i < slots->count; i++) sorted[i] = arena_dup(a, slots->keys[i]);
  size_t sorted_count = slots->count;
  smap_destroy(slots);
  slot_sort sort = {counts};
  qa_js_stable_sort(sorted, sorted_count, sizeof *sorted, compare_slots, &sort);
  qa_app_monster_roster_row *rows = arena_alloc(a, (sorted_count + 1) * sizeof *rows);
  rows[0].classname = NULL;
  rows[0].label = "Unmatched classes";
  rows[0].value = arena_dup(a, roster->def);
  rows[0].effective_label = target_label(&defaults.default_target, a);
  rows[0].choices = shared;
  rows[0].choice_count = choice_count;
  for (size_t i = 0; i < sorted_count; i++) {
    const char *classname = sorted[i];
    const char *selected = smap_get(roster->by_classname, classname);
    const char *row_value = selected != NULL ? arena_dup(a, selected) : "default";
    const char *label = target_label(roster_target(&effective, classname), a);
    qa_app_monster_roster_row *row = &rows[i + 1];
    row->classname = classname;
    row->label = arena_printf(a, "%s (%s)", monster_label(classname, a), QA_JS_NUMBER(count_of(counts, classname)));
    row->value = row_value;
    row->effective_label = arena_printf(a, "%s%s", label, eq(row_value, "default") ? "" : " *");
    choice_t *list = arena_alloc(a, (choice_count + 1) * sizeof *list);
    list[0] = choice3("default", arena_printf(a, "Use default: %s", target_label(roster_target(&defaults, classname), a)), NULL);
    for (size_t c = 0; c < choice_count; c++) list[c + 1] = shared[c];
    row->choices = list;
    row->choice_count = choice_count + 1;
  }
  *out_count = sorted_count + 1;
  return rows;
}
const qa_app_monster_roster_row *qa_app_startup_selection_monster_roster_rows(qa_app_startup_selection_model *m, size_t *out_count) {
  arena_reset(&m->monster_rows_arena);
  return monster_roster_rows(m, &m->monster_rows_arena, out_count);
}
void qa_app_startup_selection_select_monster(qa_app_startup_selection_model *m, const char *classname, const char *id) {
  arena a = {0};
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, arena_cleanup, &a);
  size_t count = 0;
  qa_app_monster_roster_row *rows = monster_roster_rows(m, &a, &count);
  const choice_t *selected = NULL;
  for (size_t i = 0; i < count && selected == NULL; i++)
    if ((classname == NULL && rows[i].classname == NULL) || (classname != NULL && eq(rows[i].classname, classname)))
      selected = choices_find(rows[i].choices, rows[i].choice_count, id);
  if (selected == NULL) fail("Unknown monster roster choice");
  if (selected->unavailable != NULL) fail("%s", selected->unavailable);
  qa_cleanup_pop(&cleanup);
  arena_free(&a);
  roster_state *roster = roster_of(m);
  if (classname == NULL) set_text(&roster->def, id);
  else if (eq(id, "default")) smap_delete(roster->by_classname, classname);
  else smap_set(roster->by_classname, classname, xdup(id));
}

/* ---------------------------------------------------------------- */
/* Options (TS get options) validation.                              */

typedef struct mode_rules { const char *mode; const char *rules; double skill; const char *renderer; } mode_rules;
/* The validating head of TS `get options()`. */
static mode_rules options_mode_rules(qa_app_startup_selection_model *m) {
  const char *rules = value(m, F_RULES), *renderer = value(m, F_RENDERER);
  double skill = qa_js_string_to_number(value(m, F_SKILL));
  const char *mode = eq(rules, "ctf") || eq(rules, "lmctf") || eq(rules, "tag") || eq(rules, "deathball") ? "deathmatch" : value(m, F_MODE);
  if ((!eq(mode, "singleplayer") && !eq(mode, "coop") && !eq(mode, "deathmatch")) || (!eq(renderer, "gl") && !eq(renderer, "cpu"))
      || (!eq(rules, "standard") && !eq(rules, "ctf") && !eq(rules, "lmctf") && !eq(rules, "tag") && !eq(rules, "deathball") && !eq(rules, "horde"))
      || (skill != 0 && skill != 1 && skill != 2 && skill != 3))
    fail("Invalid startup settings");
  mode_rules out = {eq(mode, "deathmatch") ? "deathmatch" : eq(mode, "coop") ? "coop" : "singleplayer",
      eq(rules, "standard") ? "standard" : eq(rules, "ctf") ? "ctf" : eq(rules, "lmctf") ? "lmctf" : eq(rules, "tag") ? "tag"
        : eq(rules, "deathball") ? "deathball" : "horde",
      skill, eq(renderer, "gl") ? "gl" : "cpu"};
  return out;
}
static qa_app_match_mode_selection match_selection(const qa_catalog_discovered_product *product, const char *mode, const char *rules) {
  qa_app_match_mode_selection selection = {product->expectation.family, edition_of(product), campaign_of(product), mode, rules};
  return selection;
}
/* TS matchMapUnavailable over a cached classname list, into `a`. */
static const char *map_unavailable(const qa_catalog_discovered_product *source, const char *mode, const char *rules,
                                   const slist *classnames, arena *a) {
  char scratch[2048];
  const char *result = qa_app_match_map_unavailable(match_selection(source, mode, rules),
      (const char *const *)classnames->items, classnames->count, scratch, sizeof scratch);
  return arena_dup(a, result);
}
static const slist *classnames_for(qa_app_startup_selection_model *m, const qa_catalog_content_map *map) {
  if (map == NULL) return NULL;
  char *key = map_key(map);
  const slist *list = smap_get(m->map_classnames, key);
  free(key);
  return list;
}
/* Lower-case path index over mapsFor(product) (TS new Map(...): the last
 * duplicate wins). */
static const qa_catalog_content_map *metadata_lookup(const qa_catalog_content_map **maps, size_t count, const char *id) {
  const qa_catalog_content_map *found = NULL;
  for (size_t i = 0; i < count; i++) if (equals_ci(maps[i]->path, id)) found = maps[i];
  return found;
}
static const pchoices empty_choices = {NULL, 0};
/* TS maps(): a borrowed persistent list. */
static const pchoices *maps_of(qa_app_startup_selection_model *m) {
  const qa_catalog_discovered_product *product = geometry(m);
  const pchoices *choices = smap_get(m->playable_maps, product->expectation.id);
  if (choices == NULL) choices = &empty_choices;
  const qa_catalog_discovered_product *source = qa_source_program_product(m->current, product_field(m, F_PRODUCT)->id);
  mode_rules selected = options_mode_rules(m);
  if (!eq(selected.mode, "deathmatch") && eq(selected.rules, "standard")) return choices;
  char *key = xprintf("%s:%s:%s:%s", source->id, product->id, selected.mode, selected.rules);
  const pchoices *cached = smap_get(m->eligible_maps, key);
  if (cached != NULL) { free(key); return cached; }
  size_t map_count = 0;
  const qa_catalog_content_map **maps = maps_for(m, product->id, &map_count);
  arena a = {0};
  choice_t *items = xcalloc(choices->count, sizeof *items);
  for (size_t i = 0; i < choices->count; i++) {
    items[i] = choices->items[i];
    if (items[i].unavailable != NULL) continue;
    const slist *classnames = classnames_for(m, metadata_lookup(maps, map_count, items[i].id));
    if (classnames != NULL) items[i].unavailable = map_unavailable(source, selected.mode, selected.rules, classnames, &a);
  }
  pchoices *result = pchoices_from(items, choices->count);
  free(items);
  arena_free(&a);
  free((void *)maps);
  smap_set(m->eligible_maps, key, result);
  free(key);
  return result;
}
/* TS defaultMap(), into `a`. */
static const char *default_map(qa_app_startup_selection_model *m, arena *a) {
  const qa_catalog_discovered_product *product = geometry(m);
  const pchoices *maps = maps_of(m);
  const char *preferred = smap_get(m->authored_default_maps, product->expectation.id);
  if (preferred == NULL) preferred = product->expectation.map_witness;
  if (preferred == NULL)
    preferred = product->expectation.family == QA_GAME_FAMILY_Q1 ? "maps/start.bsp"
        : product->expectation.family == QA_GAME_FAMILY_Q2 ? "maps/base1.bsp" : "maps/q3dm0.bsp";
  for (size_t i = 0; i < maps->count; i++) if (eq(maps->items[i].id, preferred)) return arena_dup(a, maps->items[i].id);
  return "";
}
static void reset_default_map(qa_app_startup_selection_model *m) {
  arena a = {0};
  const char *map = default_map(m, &a);
  set_value(m, F_MAP, map);
  arena_free(&a);
}

/* ---------------------------------------------------------------- */
/* Map files.                                                        */

static uint32_t u32le(const uint8_t *bytes) {
  return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}
/* TS `new TextDecoder().decode(bytes)` as C text (bytes up to the first NUL). */
static char *bytes_text(const uint8_t *bytes, size_t length) {
  char *text = xmalloc(length + 1);
  if (length > 0) memcpy(text, bytes, length);
  text[length] = '\0';
  return text;
}
typedef struct map_read {
  qa_archive_source file; bool file_open;
  qa_archive_handle *archive; bool archive_owned;
  uint8_t *decoded; size_t decoded_length; bool has_decoded;
  char *key; uint8_t *header; uint8_t *bytes; char *text;
  qa_q1_entity *q1; size_t q1_count;
  qa_q2_spawn_fields *q2; size_t q2_count;
  qa_q3_entity *q3; size_t q3_count;
  const char **classnames;
  const qa_catalog_content_map **maps;
} map_read;
static void map_read_release(void *arg) {
  map_read *read = arg;
  free(read->key); free(read->decoded); free(read->header); free(read->bytes); free(read->text); free(read->classnames);
  free((void *)read->maps);
  if (read->q1) qa_q1_entities_destroy(read->q1, read->q1_count);
  if (read->q2) qa_q2_entities_free(read->q2, read->q2_count);
  if (read->q3) qa_q3_entities_destroy(read->q3, read->q3_count);
  if (read->archive_owned && read->archive) qa_archive_destroy(read->archive);
  if (read->file_open) qa_archive_source_destroy(&read->file);
  memset(read, 0, sizeof *read);
}
/* Shared file/archive caches of prepareMaps. */
typedef struct map_caches { smap *archives; smap *files; smap *playable; } map_caches;
static void archive_release(void *value) { qa_archive_destroy(value); }
static void file_release(void *value) { qa_archive_source *file = value; qa_archive_source_destroy(file); free(file); }
static void map_caches_release(void *arg) {
  map_caches *caches = arg;
  smap_destroy(caches->archives); smap_destroy(caches->files); smap_destroy(caches->playable);
}
typedef struct prepare_map_call {
  qa_app_startup_selection_model *m;
  map_caches *caches;
  const qa_catalog_content_map *map;
  qa_game_family family;
  bool accepted, has_accepted;
  map_read read;
} prepare_map_call;
/* One map of TS prepareMaps (the per-map try body). */
static void prepare_map_body(void *ctx) {
  prepare_map_call *call = ctx;
  map_read *read = &call->read;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, map_read_release, read);
  const qa_catalog_content_map *map = call->map;
  read->key = map_key(map);
  uintptr_t cached = (uintptr_t)smap_get(call->caches->playable, read->key);
  call->has_accepted = cached != 0;
  call->accepted = cached == 2;
  if (!call->has_accepted && call->family == QA_GAME_FAMILY_Q1) {
    qa_archive_source *file = smap_get(call->caches->files, map->source);
    if (file == NULL) {
      file = xcalloc(1, sizeof *file);
      if (!qa_file_source_init(file, map->source)) { free(file); qa_range_error("Out of memory"); }
      smap_set(call->caches->files, map->source, file);
    }
    int64_t offset = 0, length = file->byte_length;
    if (map->has_member_index) {
      qa_archive_handle *archive = smap_get(call->caches->archives, map->source);
      if (archive == NULL) {
        archive = qa_open_archive(map->source, false, QA_ARCHIVE_FORMAT_PAK);
        if (archive == NULL) qa_range_error("Out of memory");
        smap_set(call->caches->archives, map->source, archive);
      }
      if (map->member_index < 0 || (size_t)map->member_index >= archive->entry_count) fail("Missing map archive entry: %s", map->path);
      const qa_archive_entry *entry = &archive->entries[map->member_index];
      length = entry->byte_length;
      if (entry->format == QA_ARCHIVE_FORMAT_PAK) offset = entry->data_offset;
      else {
        read->decoded = qa_archive_read_entry_entry(archive, entry, &read->decoded_length);
        if (read->decoded == NULL && read->decoded_length > 0) qa_range_error("Out of memory");
        read->has_decoded = true;
      }
    }
    const uint8_t *header;
    size_t header_length;
    if (read->has_decoded) {
      header = read->decoded;
      header_length = read->decoded_length < 12 ? read->decoded_length : 12;
    } else {
      read->header = qa_archive_source_read(file, offset, length < 12 ? length : 12, &header_length);
      header = read->header;
    }
    if (header_length < 12) fail("Truncated map header: %s", map->path);
    if (eq(qa_classify_bsp(header, header_length, map->path), "q1")) {
      uint32_t start = u32le(header + 4), size = u32le(header + 8);
      if ((int64_t)start > length || (int64_t)size > length - (int64_t)start) fail("Invalid map entity lump: %s", map->path);
      const uint8_t *bytes;
      size_t byte_length;
      if (read->has_decoded) {
        /* decoded.subarray(start, start + size) clamps to the decoded length. */
        size_t begin = start < read->decoded_length ? start : read->decoded_length;
        size_t end = (size_t)start + size < read->decoded_length ? (size_t)start + size : read->decoded_length;
        bytes = read->decoded + begin;
        byte_length = end - begin;
      } else {
        read->bytes = qa_archive_source_read(file, offset + start, size, &byte_length);
        bytes = read->bytes;
      }
      read->text = bytes_text(bytes, byte_length);
      read->q1 = qa_q1_parse_entities(read->text, &read->q1_count);
      read->classnames = xcalloc(read->q1_count, sizeof *read->classnames);
      bool accepted = false;
      for (size_t i = 0; i < read->q1_count; i++) {
        const char *classname = qa_q1_entity_value(&read->q1[i], "classname");
        read->classnames[i] = classname != NULL ? classname : "";
        if (eq(classname, "info_player_start") || eq(classname, "info_player_deathmatch") || eq(classname, "info_player_coop")
            || eq(classname, "info_player_start2")) accepted = true;
      }
      cache_monster_classes(call->m, read->key, read->classnames, read->q1_count);
      call->accepted = accepted;
    } else call->accepted = true;
    call->has_accepted = true;
    smap_set(call->caches->playable, read->key, (void *)(uintptr_t)(call->accepted ? 2 : 1));
  }
  qa_cleanup_pop(&cleanup);
  map_read_release(read);
}
static int compare_choice_ids(const void *a, const void *b, void *ctx) {
  (void)ctx;
  return qa_js_locale_compare(((const choice_t *)a)->id, ((const choice_t *)b)->id);
}
/* TS q2 loose player model listing for prepareMaps. */
static void list_loose_models(const char *loose_root, slist *models) {
  char *directory = xprintf("%s/players", loose_root);
  struct stat info;
  if (stat(directory, &info) == 0) {
    DIR *dir = opendir(directory);
    if (dir == NULL) { free(directory); fail("Cannot read directory: %s", loose_root); }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
      char *path = xprintf("%s/%s", directory, entry->d_name);
      char *tris = xprintf("%s/tris.md2", path);
      struct stat entry_info, tris_info;
      bool is_directory = lstat(path, &entry_info) == 0 && S_ISDIR(entry_info.st_mode);
      if (is_directory && stat(tris, &tris_info) == 0) {
        char *model = xprintf("players/%s/tris.md2", entry->d_name);
        slist_push(models, model);
        free(model);
        DIR *inner = opendir(path);
        if (inner != NULL) {
          struct dirent *file;
          while ((file = readdir(inner)) != NULL) {
            if (strcmp(file->d_name, ".") == 0 || strcmp(file->d_name, "..") == 0) continue;
            char *lower = lower_dup(file->d_name);
            if (ends_with(lower, ".pcx")) {
              char *skin = xprintf("players/%s/%s", entry->d_name, lower);
              slist_push(models, skin);
              free(skin);
            }
            free(lower);
          }
          closedir(inner);
        }
      }
      free(path); free(tris);
    }
    closedir(dir);
  }
  free(directory);
}
/* TS prepareMaps(). */
static void prepare_maps(qa_app_startup_selection_model *m) {
  smap_clear(m->eligible_maps);
  prepare_q3_catalog(m);
  prepare_team_arena(m);
  prepare_mods(m, m->current);
  prepare_hook_styles(m, m->current);
  map_caches caches = {smap_new(archive_release), smap_new(file_release), smap_new(NULL)};
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, map_caches_release, &caches);
  qa_content_catalog *catalog = m->current;
  for (size_t p = 0; p < catalog->product_count; p++) {
    const qa_catalog_discovered_product *product = &catalog->products[p];
    if (!installed(product)) continue;
    if (product->expectation.family == QA_GAME_FAMILY_Q2 && product->loose_root != NULL) {
      slist *models = xcalloc(1, sizeof *models);
      models->generation = ++m->loose_generation;
      list_loose_models(product->loose_root, models);
      smap_set(m->loose_models, product->expectation.id, models);
    }
    arena a = {0};
    qa_cleanup arena_node;
    qa_cleanup_push(&arena_node, arena_cleanup, &a);
    cvec choices = {0};
    size_t map_count = 0;
    const qa_catalog_content_map **maps = maps_for(m, product->id, &map_count);
    arena_keep(&a, (void *)maps);
    for (size_t i = 0; i < map_count; i++) {
      prepare_map_call call;
      memset(&call, 0, sizeof call);
      call.m = m; call.caches = &caches; call.map = maps[i]; call.family = product->expectation.family;
      qa_caught_error caught;
      if (qa_try(prepare_map_body, &call, &caught)) {
        if (!call.has_accepted || call.accepted) cvec_push(&choices, choice1(maps[i]->path));
      } else cvec_push(&choices, choice3(maps[i]->path, maps[i]->path, arena_dup(&a, caught.message)));
    }
    arena_keep(&a, choices.items);
    qa_authored_start_catalog authored;
    bool found = false;
    if (!qa_content_catalog_authored_starts_for(catalog, scope_opener(), product->id, &authored, &found)) qa_range_error("Out of memory");
    cvec starts = {0};
    if (found) {
      for (size_t i = 0; i < authored.start_count; i++) {
        const char *path = authored.starts[i].path;
        bool seen = false;
        for (size_t j = 0; j < starts.count; j++) if (equals_ci(starts.items[j].id, path)) seen = true;
        if (seen) continue;
        const choice_t *installed_choice = NULL;
        for (size_t j = 0; j < choices.count && installed_choice == NULL; j++) if (equals_ci(choices.items[j].id, path)) installed_choice = &choices.items[j];
        const char *title = authored.starts[i].title != NULL && authored.starts[i].title[0] != '\0' ? authored.starts[i].title : path;
        cvec_push(&starts, choice3(arena_dup(&a, installed_choice != NULL ? installed_choice->id : path), arena_dup(&a, title),
            installed_choice == NULL ? arena_printf(&a, "Missing authored start map: %s", path) : installed_choice->unavailable));
      }
      qa_authored_start_catalog_free(&authored);
    }
    arena_keep(&a, starts.items);
    if (starts.count > 0) smap_set(m->authored_default_maps, product->expectation.id, xdup(starts.items[0].id));
    cvec others = {0};
    for (size_t i = 0; i < choices.count; i++) {
      bool authored_path = false;
      for (size_t j = 0; j < starts.count; j++) if (equals_ci(starts.items[j].id, choices.items[i].id)) authored_path = true;
      if (!authored_path) cvec_push(&others, choices.items[i]);
    }
    arena_keep(&a, others.items);
    qa_js_stable_sort(others.items, others.count, sizeof *others.items, compare_choice_ids, NULL);
    choice_t *all = arena_alloc(&a, (starts.count + others.count + 1) * sizeof *all);
    for (size_t i = 0; i < starts.count; i++) all[i] = starts.items[i];
    for (size_t i = 0; i < others.count; i++) all[starts.count + i] = others.items[i];
    smap_set(m->playable_maps, product->expectation.id, pchoices_from(all, starts.count + others.count));
    qa_cleanup_pop(&arena_node);
    arena_free(&a);
  }
  qa_cleanup_pop(&cleanup);
  map_caches_release(&caches);
}
void qa_app_startup_selection_prepare_maps(qa_app_startup_selection_model *m) { prepare_maps(m); }

/* TS prepareMapClassnames(path = this.values.map). */
typedef struct classnames_call { qa_app_startup_selection_model *m; const char *path; map_read read; } classnames_call;
static void prepare_map_classnames_body(void *ctx) {
  classnames_call *call = ctx;
  qa_app_startup_selection_model *m = call->m;
  map_read *read = &call->read;
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, map_read_release, read);
  const qa_catalog_discovered_product *product = geometry(m);
  size_t map_count = 0;
  const qa_catalog_content_map **maps = maps_for(m, product->id, &map_count);
  read->maps = maps; /* released with the read */
  const qa_catalog_content_map *map = find_map_ci(maps, map_count, call->path);
  if (map == NULL) fail("Selected map is unavailable");
  read->key = map_key(map);
  if (smap_has(m->map_classnames, read->key)) {
    qa_cleanup_pop(&cleanup);
    map_read_release(read);
    return;
  }
  if (!qa_file_source_init(&read->file, map->source)) qa_range_error("Out of memory");
  read->file_open = true;
  int64_t offset = 0, length = read->file.byte_length;
  if (map->has_member_index) {
    read->archive = qa_open_archive(map->source, false, QA_ARCHIVE_FORMAT_PAK);
    if (read->archive == NULL) qa_range_error("Out of memory");
    read->archive_owned = true;
    if (map->member_index < 0 || (size_t)map->member_index >= read->archive->entry_count) fail("Selected map archive entry is unavailable");
    const qa_archive_entry *entry = &read->archive->entries[map->member_index];
    length = entry->byte_length;
    if (entry->format == QA_ARCHIVE_FORMAT_PAK) offset = entry->data_offset;
    else {
      read->decoded = qa_archive_read_entry_entry(read->archive, entry, &read->decoded_length);
      if (read->decoded == NULL && read->decoded_length > 0) qa_range_error("Out of memory");
      read->has_decoded = true;
    }
  }
  const uint8_t *header;
  size_t header_length;
  if (read->has_decoded) {
    header = read->decoded;
    header_length = read->decoded_length < 16 ? read->decoded_length : 16;
  } else {
    read->header = qa_archive_source_read(&read->file, offset, length < 16 ? length : 16, &header_length);
    header = read->header;
  }
  if (header_length < 16) fail("Truncated map header");
  const char *codec = qa_classify_bsp(header, header_length, map->path);
  bool q1 = eq(codec, "q1");
  uint32_t start = u32le(header + (q1 ? 4 : 8)), declared = u32le(header + (q1 ? 8 : 12));
  int64_t size = eq(codec, "q2") && (int64_t)start < length
      ? ((int64_t)declared < length - (int64_t)start ? (int64_t)declared : length - (int64_t)start) : (int64_t)declared;
  if ((int64_t)start > length || size > length - (int64_t)start) fail("Invalid map entity lump");
  const uint8_t *bytes;
  size_t byte_length;
  if (read->has_decoded) {
    size_t begin = start < read->decoded_length ? start : read->decoded_length;
    size_t end = (size_t)start + (size_t)size < read->decoded_length ? (size_t)start + (size_t)size : read->decoded_length;
    bytes = read->decoded + begin;
    byte_length = end - begin;
  } else {
    read->bytes = qa_archive_source_read(&read->file, offset + start, size, &byte_length);
    bytes = read->bytes;
  }
  /* .replace(/\0+$/, ""): the C text already ends at the first NUL. */
  read->text = bytes_text(bytes, byte_length);
  const char **classnames = NULL;
  size_t count = 0;
  if (eq(codec, "q2")) {
    char message[1024];
    qa_q2_parse_status status = qa_q2_parse_entities_checked(read->text,
        eq(edition_of(product), "rerelease") ? QA_Q2_EDITION_RERELEASE : QA_Q2_EDITION_CLASSIC,
        &read->q2, &read->q2_count, message, sizeof message);
    if (status == QA_Q2_PARSE_OOM) qa_range_error("Out of memory");
    if (status == QA_Q2_PARSE_MALFORMED) fail("%s", message);
    count = read->q2_count;
    classnames = xcalloc(count, sizeof *classnames);
    for (size_t i = 0; i < count; i++) classnames[i] = read->q2[i].classname != NULL ? read->q2[i].classname : "";
  } else if (eq(codec, "q3")) {
    read->q3 = qa_q3_parse_entities(read->text, map->path, &read->q3_count);
    count = read->q3_count;
    classnames = xcalloc(count, sizeof *classnames);
    for (size_t i = 0; i < count; i++) {
      const char *classname = qa_q3_entity_get(&read->q3[i], "classname");
      classnames[i] = classname != NULL ? classname : "";
    }
  } else {
    read->q1 = qa_q1_parse_entities(read->text, &read->q1_count);
    count = read->q1_count;
    classnames = xcalloc(count, sizeof *classnames);
    for (size_t i = 0; i < count; i++) {
      const char *classname = qa_q1_entity_value(&read->q1[i], "classname");
      classnames[i] = classname != NULL ? classname : "";
    }
  }
  read->classnames = classnames;
  cache_monster_classes(m, read->key, classnames, count);
  qa_cleanup_pop(&cleanup);
  map_read_release(read);
}
static void prepare_map_classnames(qa_app_startup_selection_model *m, const char *path) {
  classnames_call call;
  memset(&call, 0, sizeof call);
  call.m = m;
  call.path = path;
  prepare_map_classnames_body(&call);
}
void qa_app_startup_selection_prepare_monster_roster(qa_app_startup_selection_model *m) {
  if (geometry(m)->expectation.family == QA_GAME_FAMILY_Q3) fail("This map has no supported authored monster roster");
  char *path = xdup(value(m, F_MAP));
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, path);
  prepare_map_classnames(m, path);
  qa_cleanup_pop(&cleanup);
  free(path);
}
/* TS Array.prototype.slice index normalization. */
static size_t slice_index(double index, size_t length) {
  double relative = isnan(index) ? 0 : trunc(index);
  if (relative < 0) relative = qa_js_max(0, (double)length + relative);
  return relative > (double)length ? length : (size_t)relative;
}
bool qa_app_startup_selection_prepare_map_choices(qa_app_startup_selection_model *m, double offset, double count) {
  const qa_catalog_discovered_product *product = geometry(m);
  if (!eq(value(m, F_MODE), "deathmatch") && eq(value(m, F_RULES), "standard")) return false;
  size_t map_count = 0;
  const qa_catalog_content_map **maps = maps_for(m, product->id, &map_count);
  qa_cleanup maps_node;
  qa_cleanup_push(&maps_node, free, (void *)maps);
  const pchoices *all = maps_of(m);
  size_t begin = slice_index(offset, all->count), end = slice_index(offset + count, all->count);
  slist page = {0};
  for (size_t i = begin; i < end; i++) {
    const choice_t *c = &all->items[i];
    const qa_catalog_content_map *map = find_map_ci(maps, map_count, c->id);
    if (c->unavailable != NULL) continue;
    if (map != NULL) {
      char *key = map_key(map);
      bool known = smap_has(m->map_classnames, key);
      free(key);
      if (known) continue;
    }
    slist_push(&page, c->id);
  }
  qa_cleanup_pop(&maps_node);
  free((void *)maps);
  if (page.count == 0) { free(page.items); return false; }
  /* TS prepareMapPage(product, choices). */
  char *product_id = xdup(product->expectation.id);
  for (size_t i = 0; i < page.count; i++) {
    classnames_call call;
    memset(&call, 0, sizeof call);
    call.m = m;
    call.path = page.items[i];
    qa_caught_error caught;
    if (qa_try(prepare_map_classnames_body, &call, &caught)) continue;
    const pchoices *current = smap_get(m->playable_maps, product_id);
    pchoices *next = pchoices_from(current != NULL ? current->items : NULL, current != NULL ? current->count : 0);
    for (size_t j = 0; j < next->count; j++)
      if (eq(next->items[j].id, page.items[i])) { free((char *)next->items[j].unavailable); next->items[j].unavailable = xdup(caught.message); }
    smap_set(m->playable_maps, product_id, next);
    smap_clear(m->eligible_maps);
  }
  free(product_id);
  for (size_t i = 0; i < page.count; i++) free(page.items[i]);
  free(page.items);
  return true;
}

/* ---------------------------------------------------------------- */
/* Presets.                                                          */

/* TS q3TrainingMap(product); malloc'd or NULL. */
static char *q3_training_map(qa_app_startup_selection_model *m, const qa_catalog_discovered_product *product) {
  const smap *files = files_of(m, product);
  slist paths = {0};
  for (size_t i = 0; i < files->count; i++) {
    const char *path = files->keys[i];
    /* /^scripts\/[^/]+\.arena$/ */
    size_t length = strlen(path);
    bool arena_file = length > strlen("scripts/") + strlen(".arena") && starts_with(path, "scripts/") && ends_with(path, ".arena");
    for (size_t c = strlen("scripts/"); arena_file && c < length; c++) if (path[c] == '/') arena_file = false;
    if (strcmp(path, "scripts/arenas.txt") == 0 || arena_file) slist_push(&paths, path);
  }
  char *result = NULL;
  for (size_t p = 0; p < paths.count && result == NULL; p++) {
    size_t length = 0;
    uint8_t *bytes = qa_content_catalog_read(m->current, scope_opener(), product->id, paths.items[p], &length);
    if (bytes == NULL && length > 0) qa_range_error("Out of memory");
    qa_common_parse_state parser;
    qa_common_parse_state_init(&parser);
    qa_common_parse_cursor cursor;
    if (!qa_common_parse_cursor_init(&cursor, (const char *)bytes, length, false)) qa_range_error("Out of memory");
    free(bytes);
    while (result == NULL && strcmp(qa_common_parse_state_parse(&parser, &cursor, true), "{") == 0) {
      smap *fields = smap_new(free);
      for (;;) {
        char *key = xdup(qa_common_parse_state_parse(&parser, &cursor, true));
        if (strcmp(key, "}") == 0 || key[0] == '\0') { free(key); break; }
        smap_set(fields, key, xdup(qa_common_parse_state_parse(&parser, &cursor, false)));
        free(key);
      }
      const char *map = smap_get(fields, "map"), *special = smap_get(fields, "special");
      if (special != NULL && map != NULL) {
        char *lower = lower_dup(special);
        if (strcmp(lower, "training") == 0) result = xprintf("maps/%s.bsp", map);
        free(lower);
      }
      smap_destroy(fields);
    }
    qa_common_parse_cursor_destroy(&cursor);
  }
  for (size_t i = 0; i < paths.count; i++) free(paths.items[i]);
  free(paths.items);
  return result;
}
static bool is_expected(const char *id) {
  for (size_t i = 0; i < qa_expected_product_count; i++) if (eq(qa_expected_products[i].id, id)) return true;
  return false;
}
static const char *const q3_difficulty_ids[5] = {"1", "2", "3", "4", "5"};
static const char *const q3_difficulty_labels[5] = {"I Can Win", "Bring It On", "Hurt Me Plenty", "Hardcore", "Nightmare"};
static const char *const difficulty_ids[4] = {"0", "1", "2", "3"};
static const char *const difficulty_labels[4] = {"Easy", "Normal", "Hard", "Nightmare"};
typedef qa_app_startup_native_preset preset_t;
/* TS presets(); strings in `a`. */
static preset_t *presets_build(qa_app_startup_selection_model *m, arena *a, size_t *out_count) {
  qa_content_catalog *catalog = m->current;
  size_t capacity = catalog->product_count * 6 + 1, count = 0, campaign_count = 0;
  preset_t *presets = arena_alloc(a, capacity * sizeof *presets);
  for (size_t i = 0; i < catalog->product_count; i++) {
    const qa_catalog_discovered_product *product = &catalog->products[i];
    const char *edition = edition_of(product), *campaign = campaign_of(product);
    if (!installed(product) || !is_expected(product->expectation.id) || !(eq(edition, "classic") || eq(edition, "rerelease"))
        || eq(campaign, "ctf") || eq(campaign, "lmctf")) continue;
    preset_t *preset = &presets[count++];
    qa_game_family family = product->expectation.family;
    preset->choice = product_choice(product, a);
    preset->choice.unavailable = NULL;
    preset->choice.edition = NULL;
    preset->family = family;
    preset->edition = edition;
    bool q3 = family == QA_GAME_FAMILY_Q3;
    size_t difficulties = q3 ? 5 : 4;
    choice_t *list = arena_alloc(a, difficulties * sizeof *list);
    for (size_t d = 0; d < difficulties; d++)
      list[d] = q3 ? choice3(q3_difficulty_ids[d], q3_difficulty_labels[d], NULL) : choice3(difficulty_ids[d], difficulty_labels[d], NULL);
    preset->difficulties = list;
    preset->difficulty_count = difficulties;
    preset->default_skill = q3 ? "2" : "1";
  }
  campaign_count = count;
  for (size_t i = 0; i < campaign_count; i++) {
    const preset_t *base = &presets[i];
    const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, base->choice.id);
    qa_game_family family = product->expectation.family;
    const char *campaign = campaign_of(product);
#define MODE_PRESET(SUFFIX, TITLE, RULES, MODE) do { \
      preset_t *mode = &presets[count++]; *mode = *base; \
      mode->choice.id = arena_printf(a, "%s:%s", base->choice.id, SUFFIX); \
      mode->choice.label = arena_printf(a, "%s \xe2\x80\x94 %s", base->choice.label, TITLE); \
      mode->product_id = base->choice.id; mode->rules = RULES; mode->mode = MODE; } while (0)
    if (family == QA_GAME_FAMILY_Q1 && (eq(campaign, "mg1") || eq(campaign, "dopa"))) MODE_PRESET("horde", "Horde", "horde", "singleplayer");
    if (family == QA_GAME_FAMILY_Q3) {
      MODE_PRESET("horde", "Horde", "horde", "singleplayer");
      MODE_PRESET("ctf", "Capture the Flag", "ctf", "deathmatch");
    }
    MODE_PRESET("tag", "Tag", "tag", "deathmatch");
    if (family == QA_GAME_FAMILY_Q2) MODE_PRESET("deathball", "DeathBall", "deathball", "deathmatch");
#undef MODE_PRESET
  }
  *out_count = count;
  return presets;
}
const qa_app_startup_native_preset *qa_app_startup_selection_presets(qa_app_startup_selection_model *m, size_t *out_count) {
  arena_reset(&m->presets_arena);
  return presets_build(m, &m->presets_arena, out_count);
}

/* ---------------------------------------------------------------- */
/* Launch results.                                                   */

typedef struct launch_backing {
  qa_app_application_preset_result preset; bool has_preset;
  qa_launch_preset effective;
  qa_provider_timing *timing;
  qa_provider_timing movement_timing, character_timing; bool has_movement_timing, has_character_timing;
  qa_map_selection map; qa_provider_reference movement; qa_character_selection character;
  qa_campaign_selection campaign;
  qa_provider_reference weapon; char *weapon_provider; qa_provider_reference_list weapons;
  qa_enemy_selection enemies;
  qa_equipment_selection equipment;
  qa_resolve_launch_result resolved; bool has_resolved;
  qa_app_applied_mods applied; bool has_applied;
  qa_app_team_arena_skirmish skirmish; bool has_skirmish;
  arena strings;
} launch_backing;
static void launch_backing_free(launch_backing *backing) {
  if (!backing) return;
  if (backing->has_applied) qa_app_applied_mods_destroy(&backing->applied);
  if (backing->has_resolved) qa_resolve_launch_result_free(&backing->resolved);
  if (backing->has_preset) qa_app_application_preset_result_free(&backing->preset);
  if (backing->has_movement_timing) qa_native_provider_timing_free(&backing->movement_timing);
  if (backing->has_character_timing) qa_native_provider_timing_free(&backing->character_timing);
  if (backing->has_skirmish) qa_app_team_arena_skirmish_free(&backing->skirmish);
  free(backing->timing);
  free(backing->weapon_provider);
  arena_free(&backing->strings);
  free(backing);
}
void qa_app_startup_launch_free(qa_app_startup_launch *launch) {
  if (!launch) return;
  launch_backing_free(launch->backing);
  qa_app_options_destroy(&launch->options);
  memset(launch, 0, sizeof *launch);
}
typedef struct launch_scope { qa_app_startup_launch *launch; } launch_scope;
static void launch_scope_cleanup(void *arg) { qa_app_startup_launch_free(((launch_scope *)arg)->launch); }
static void launch_resolve(qa_app_startup_selection_model *m, launch_backing *backing, const qa_launch_preset *preset,
                           qa_launch_choice choice) {
  qa_resolve_launch_options options;
  memset(&options, 0, sizeof options);
  qa_open_mount_options mount_options;
  memset(&mount_options, 0, sizeof mount_options);
  options.choice = choice;
  options.preset = preset;
  options.catalog = m->current;
  options.mounts_options = &mount_options;
  options.services = qa_app_native_launch_services();
  if (!qa_resolve_launch(&options, &backing->resolved)) qa_range_error("Out of memory");
  backing->has_resolved = true;
}
/* ...the display, then displayOverrides: {} once setDisplay ran. */
static void apply_display(qa_app_startup_selection_model *m, qa_app_options *options) {
  options->width = m->display_width;
  options->height = m->display_height;
  options->gamma = m->display_gamma;
  if (m->display_overrides_consumed) {
    options->has_display_width = false;
    options->has_display_height = false;
    options->has_display_gamma = false;
  }
}
static bool level_is(double level, int lo, int hi) {
  for (int i = lo; i <= hi; i++) if (level == i) return true;
  return false;
}
typedef struct regex_rule { const char *rules; } regex_rule;
static bool contains_ci(const char *text, const char *needle) {
  char *lower = lower_dup(text);
  bool found = strstr(lower, needle) != NULL;
  free(lower);
  return found;
}
/* /(^|\/)(q2dm|rdm|dm)[^/]*\.bsp$/i */
static bool tag_map(const char *id) {
  char *lower = lower_dup(id);
  const char *leaf = strrchr(lower, '/');
  leaf = leaf != NULL ? leaf + 1 : lower;
  bool match = ends_with(leaf, ".bsp") && (starts_with(leaf, "q2dm") || starts_with(leaf, "rdm") || starts_with(leaf, "dm"));
  free(lower);
  return match;
}
/* /ctf[^/]*\.bsp$/i */
static bool ctf_map(const char *id) {
  char *lower = lower_dup(id);
  const char *leaf = strrchr(lower, '/');
  leaf = leaf != NULL ? leaf + 1 : lower;
  bool match = ends_with(leaf, ".bsp") && strstr(leaf, "ctf") != NULL && (size_t)(strstr(leaf, "ctf") - leaf) + 3 <= strlen(leaf) - 4;
  free(lower);
  return match;
}
/* resolvePreset's authoredFor(rules). */
static const choice_t *authored_for(qa_app_startup_selection_model *m, const qa_catalog_discovered_product *product,
    const pchoices *playable, const char *mode, const char *rules, arena *a) {
  size_t map_count = 0;
  const qa_catalog_content_map **maps = maps_for(m, product->id, &map_count);
  arena_keep(a, (void *)maps);
  for (size_t i = 0; i < playable->count; i++) {
    const slist *classnames = classnames_for(m, metadata_lookup(maps, map_count, playable->items[i].id));
    if (classnames != NULL && map_unavailable(product, mode, rules, classnames, a) == NULL) return &playable->items[i];
  }
  return NULL;
}
/* TS resolvePreset(id, difficulty?, arenaMap?). */
static void resolve_preset(qa_app_startup_selection_model *m, const char *id, bool has_difficulty, double difficulty,
                           const char *arena_map, qa_app_startup_launch *out) {
  memset(out, 0, sizeof *out);
  arena a = {0};
  qa_cleanup arena_node;
  qa_cleanup_push(&arena_node, arena_cleanup, &a);
  size_t preset_count = 0;
  const preset_t *presets = presets_build(m, &a, &preset_count);
  const preset_t *selected = NULL;
  for (size_t i = 0; i < preset_count && selected == NULL; i++) if (eq(presets[i].choice.id, id)) selected = &presets[i];
  if (selected == NULL) fail("Installed official campaign preset unavailable: %s", id);
  if (selected->choice.unavailable != NULL) fail("%s", selected->choice.unavailable);
  double level = has_difficulty ? difficulty : qa_js_string_to_number(selected->default_skill);
  bool valid_level = false;
  for (size_t i = 0; i < selected->difficulty_count; i++) if (qa_js_string_to_number(selected->difficulties[i].id) == level) valid_level = true;
  if (!valid_level) fail("Invalid preset difficulty");
  const char *product_id = selected->product_id != NULL ? selected->product_id : id;
  const qa_catalog_discovered_product *product = cat_require(m, product_id);
  qa_game_family family = product->expectation.family;
  launch_backing *backing = xcalloc(1, sizeof *backing);
  out->backing = backing;
  launch_scope scope = {out};
  qa_cleanup launch_node;
  qa_cleanup_push(&launch_node, launch_scope_cleanup, &scope);
  if (eq(campaign_of(product), "missionpack") && level_is(level, 1, 5)) {
    if (m->team_campaign == NULL) fail("Team Arena metadata is not prepared");
    qa_app_team_arena_cursor cursor = {3, 0};
    qa_app_team_arena_teams teams = {m->team_player, m->team_opponent};
    qa_app_team_arena_skirmish_init_default(&backing->skirmish, m->team_campaign, level, cursor, teams);
    backing->has_skirmish = true;
  }
  const qa_app_arena_selection_row *selected_arena = NULL;
  if (arena_map != NULL && m->has_arenas)
    for (size_t i = 0; i < m->arenas.row_count && selected_arena == NULL; i++)
      if (eq(m->arenas.rows[i].arena->map, arena_map)) selected_arena = &m->arenas.rows[i];
  if (arena_map != NULL && (!eq(product_id, "q3-baseq3") || selected_arena == NULL || !selected_arena->available))
    fail("That arena is not unlocked in this profile.");
  const pchoices *playable = smap_get(m->playable_maps, product_id);
  if (playable == NULL) playable = &empty_choices;
  const char *preset_mode = selected->mode != NULL ? selected->mode : "singleplayer";
  const choice_t *mode_map = NULL;
  if (eq(selected->rules, "horde")) {
    mode_map = authored_for(m, product, playable, preset_mode, "horde", &a);
    for (size_t i = 0; mode_map == NULL && i < playable->count; i++) if (contains_ci(playable->items[i].id, "horde")) mode_map = &playable->items[i];
  } else if (eq(selected->rules, "deathball")) {
    mode_map = authored_for(m, product, playable, preset_mode, "deathball", &a);
    for (size_t i = 0; mode_map == NULL && i < playable->count; i++) if (contains_ci(playable->items[i].id, "dball")) mode_map = &playable->items[i];
  } else if (eq(selected->rules, "tag")) {
    for (size_t i = 0; mode_map == NULL && i < playable->count; i++) if (tag_map(playable->items[i].id)) mode_map = &playable->items[i];
  } else if (eq(selected->rules, "ctf")) {
    mode_map = authored_for(m, product, playable, preset_mode, "ctf", &a);
    for (size_t i = 0; mode_map == NULL && i < playable->count; i++) if (ctf_map(playable->items[i].id)) mode_map = &playable->items[i];
  }
  const char *preferred = NULL;
  if (mode_map != NULL) preferred = mode_map->id;
  else if (selected_arena != NULL) preferred = selected_arena->arena->map;
  else if (backing->has_skirmish) preferred = backing->skirmish.map;
  else if (family == QA_GAME_FAMILY_Q3) preferred = arena_keep(&a, q3_training_map(m, product));
  else {
    preferred = smap_get(m->authored_default_maps, product_id);
    if (preferred == NULL) preferred = product->expectation.map_witness;
    if (preferred == NULL && family == QA_GAME_FAMILY_Q1) preferred = "maps/start.bsp";
  }
  const choice_t *map = NULL;
  for (size_t i = 0; preferred != NULL && map == NULL && i < playable->count; i++)
    if (equals_ci(playable->items[i].id, preferred)) map = &playable->items[i];
  if (map == NULL || map->unavailable != NULL)
    fail("%s: %s", selected->choice.label, map != NULL ? map->unavailable : "authored campaign start map is unavailable");
  const char *character_model = backing->has_skirmish ? backing->skirmish.player_model : default_model(family);
  const char *model_unavailable = NULL;
  const char *model = model_lookup(m, product, character_model, &model_unavailable, NULL, &a);
  if (model == NULL || model_unavailable != NULL) fail("%s: native %s model is unavailable", selected->choice.label, character_model);
  double skill = family == QA_GAME_FAMILY_Q3 ? 1 : level;
  if (skill != 0 && skill != 1 && skill != 2 && skill != 3) fail("Invalid campaign difficulty");
  const char *renderer = value(m, F_RENDERER);
  if (!eq(renderer, "gl") && !eq(renderer, "cpu")) fail("Invalid renderer selection");
  /* preferences: initial without teamArenaSkirmish, botSkill, serverProfile(Path),
   * mapProduct, quakeCProgram, weaponBehavior, mods, remoteContent, q1/q2Protocol. */
  qa_app_options *options = &out->options;
  qa_app_startup_options_copy(options, &m->initial);
  options->team_arena_skirmish = NULL;
  options->has_bot_skill = false;
  options->server_profile = NULL;
  free(options->server_profile_path); options->server_profile_path = NULL;
  free(options->map_product); options->map_product = NULL;
  free(options->quakec_program); options->quakec_program = NULL;
  options->has_weapon_behavior = false;
  free(options->weapon_behavior_product); options->weapon_behavior_product = NULL;
  free(options->weapon_behavior_id); options->weapon_behavior_id = NULL;
  set_mods(options, NULL, 0);
  options->has_remote_content = false;
  free(options->remote_content.directory); memset(&options->remote_content, 0, sizeof options->remote_content);
  options->has_q1_protocol = false;
  options->has_q2_protocol = false;
  apply_display(m, options);
  options->renderer = eq(renderer, "gl") ? "gl" : "cpu";
  if (backing->has_skirmish) options->team_arena_skirmish = &backing->skirmish;
  set_text(&options->product, product_id);
  set_text(&options->map, map->id);
  options->movement = family;
  set_text(&options->movement_product, product->expectation.id);
  options->character = family;
  set_text(&options->character_model, character_model);
  options->skill = skill;
  if (family == QA_GAME_FAMILY_Q3 && level_is(level, 1, 5)) { options->has_bot_skill = true; options->bot_skill = level; }
  options->mode = selected->mode != NULL ? selected->mode : "singleplayer";
  options->rules = selected->rules != NULL ? selected->rules : "standard";
  options->seats = 1;
  options->dedicated = false;
  options->network_kind = "offline";
  free(options->network_host); options->network_host = NULL;
  free(options->network_remote); options->network_remote = NULL;
  options->network_port = 0;
  apply_selected_server_profile(m, options);
  backing->movement.provider = arena_printf(&backing->strings, "%s:movement", family_text(family));
  backing->movement.content = product->id;
  backing->character.definition.provider = arena_printf(&backing->strings, "%s:character", family_text(family));
  backing->character.definition.content = product->id;
  qa_app_native_provider_sources sources = {backing->movement, backing->character.definition};
  if (!qa_app_application_preset_for_catalog(m->current, options, &sources, NULL, &backing->preset)) qa_range_error("Out of memory");
  backing->has_preset = true;
  launch_resolve(m, backing, &backing->preset.preset, qa_preset_choice(backing->preset.preset.id));
  out->recipe = backing->resolved.recipe;
  options->explicit_skill = options->explicit_mode = options->explicit_capacity = true;
  qa_cleanup_pop(&launch_node);
  qa_cleanup_pop(&arena_node);
  arena_free(&a);
}
void qa_app_startup_selection_resolve_preset(qa_app_startup_selection_model *m, const char *id,
    bool has_difficulty, double difficulty, const char *arena_map, qa_app_startup_launch *out) {
  resolve_preset(m, id, has_difficulty, difficulty, arena_map, out);
}

/* ---------------------------------------------------------------- */
/* Rows.                                                             */

/* TS baseChoices(). */
static void base_choices(qa_app_startup_selection_model *m, arena *a, cvec *out) {
  for (size_t i = 0; i < m->current->product_count; i++) {
    const qa_catalog_discovered_product *product = &m->current->products[i];
    const char *edition = edition_of(product), *campaign = campaign_of(product);
    qa_game_family family = product->expectation.family;
    if (((eq(edition, "classic") || eq(edition, "rerelease"))
         && ((family == QA_GAME_FAMILY_Q1 && eq(campaign, "id1")) || (family == QA_GAME_FAMILY_Q2 && eq(campaign, "baseq2"))))
        || eq(product->expectation.id, "q3-baseq3"))
      cvec_push(out, product_choice(product, a));
  }
}
typedef struct row_vec { qa_app_startup_selection_row *items; size_t count, cap; } row_vec;
static void row_push(row_vec *rows, arena *a, const char *id, const char *label, cvec *choices, const char *row_value) {
  if (rows->count == rows->cap) { rows->cap = rows->cap ? rows->cap * 2 : 24; rows->items = xrealloc(rows->items, rows->cap * sizeof *rows->items); }
  qa_app_startup_selection_row *row = &rows->items[rows->count++];
  row->id = id;
  row->label = label;
  row->value = arena_dup(a, row_value);
  row->choices = cvec_finish(choices, a, &row->choice_count);
}
static void pchoices_push_all(cvec *out, const pchoices *list) {
  for (size_t i = 0; i < list->count; i++) cvec_push(out, list->items[i]);
}
static const char *match_rule_label(const char *rule) {
  return eq(rule, "tag") ? "Tag" : eq(rule, "deathball") ? "DeathBall" : "Horde";
}
static const char *values_mode(qa_app_startup_selection_model *m) {
  const char *mode = value(m, F_MODE);
  return eq(mode, "deathmatch") ? "deathmatch" : eq(mode, "coop") ? "coop" : "singleplayer";
}
/* TS rows(); everything copied into `a`. */
static qa_app_startup_selection_row *rows_build(qa_app_startup_selection_model *m, arena *a, size_t *out_count) {
  qa_content_catalog *catalog = m->current;
  const qa_catalog_discovered_product *current = product_field(m, F_PRODUCT);
  choice_t current_choice = product_choice(current, a);
  const char *current_label = current_choice.label;
  qa_game_family family = current->expectation.family;
  qa_app_options defaults_options;
  memset(&defaults_options, 0, sizeof defaults_options);
  defaults_options.product = m->values[F_PRODUCT];
  defaults_options.movement = family;
  defaults_options.character = family;
  defaults_options.network_kind = m->initial.network_kind;
  const char *default_movement = NULL, *default_character = NULL;
  qa_app_application_player_products(catalog, &defaults_options, false, false, &default_movement, &default_character);
  const qa_catalog_discovered_product *quakeworld = cat_product(m, "q1-quakeworld");
  choice_t quakeworld_choice = product_choice(quakeworld, a);
  quakeworld_choice.unavailable = unavailable_of(quakeworld, a) == NULL ? NULL
      : unavailable_of(cat_product(m, quakeworld->expectation.base_product != NULL ? quakeworld->expectation.base_product : "q1-classic-id1"), a);
  cvec movement_choices = {0}, character_choices = {0};
  base_choices(m, a, &movement_choices);
  cvec_push(&movement_choices, quakeworld_choice);
  base_choices(m, a, &character_choices);
  const char *movement_ids[2] = {default_movement, value(m, F_MOVEMENT)};
  for (int i = 0; i < 2; i++) if (!cvec_has(&movement_choices, movement_ids[i])) cvec_push(&movement_choices, product_choice(cat_product(m, movement_ids[i]), a));
  const char *character_ids[2] = {default_character, value(m, F_CHARACTER)};
  for (int i = 0; i < 2; i++) if (!cvec_has(&character_choices, character_ids[i])) cvec_push(&character_choices, product_choice(cat_product(m, character_ids[i]), a));
  roster_state *roster = roster_of(m);
  cvec source_choices = {0};
  monster_source_row(m, a, &source_choices);
  arena_keep(a, source_choices.items);
  const choice_t *source_choice = choices_find(source_choices.items, source_choices.count, roster->source);
  const char *source_label = source_choice != NULL ? source_choice->label : "Authored campaign monsters";
  bool customized = !eq(roster->def, "native") || roster->by_classname->count > 0;
  const char *monster_value = eq(value(m, F_ENEMIES), "custom") && !eq(roster->source, "native") && !customized ? roster->source : value(m, F_ENEMIES);
  qa_provider_reference provider = {arena_printf(a, "%s:official", family_text(family)), current->id};
  const qa_catalog_discovered_product *rules_product = eq(value(m, F_RULES), "ctf") || eq(value(m, F_RULES), "lmctf")
      ? cat_product(m, arena_printf(a, "q2-classic-%s", value(m, F_RULES))) : current;
  qa_equipment_selection defaults;
  if (unavailable_of(current, a) != NULL || unavailable_of(rules_product, a) != NULL) defaults = qa_disabled_equipment();
  else {
    qa_provider_reference match = eq(value(m, F_RULES), "standard") ? provider
        : (qa_provider_reference){arena_printf(a, "%s:%s", family_text(family), value(m, F_RULES)), rules_product->id};
    defaults = qa_native_equipment(catalog, provider, match);
  }
  choice_t native_weapons = choice3("native", arena_printf(a, "%s weapons", current_label), NULL);
  choice_t native_monsters = choice3("native", arena_printf(a, "%s authored monsters", current_label), NULL);
  hook_styles styles = hook_styles_of(m);
  qa_cleanup styles_node;
  qa_cleanup_push(&styles_node, hook_styles_cleanup, &styles);
  const char *placement = eq(value(m, F_GRAPPLE), "native")
      ? (defaults.grapple.is_enabled ? defaults.grapple.binding : "disabled") : value(m, F_GRAPPLE);
  const char *default_style = "";
  if (defaults.grapple.is_enabled)
    default_style = eq(defaults.grapple.mechanic, "q3-qvm") ? defaults.grapple.profile->id : defaults.grapple.mechanic;
  else {
    const char *found = NULL;
    for (size_t i = 0; i < styles.count && found == NULL; i++) if (styles.all[i].unavailable == NULL) found = styles.all[i].id;
    if (found == NULL && styles.count > 0) found = styles.all[0].id;
    if (found != NULL) default_style = found;
  }
  const char *style = eq(value(m, F_GRAPPLE_STYLE), "native") ? default_style : value(m, F_GRAPPLE_STYLE);
  bool any_style = false;
  for (size_t i = 0; i < styles.count; i++) if (styles.all[i].unavailable == NULL) any_style = true;
  const char *hook_unavailable = any_style ? NULL : "Install a game or mod that provides a hook";
  const qa_catalog_discovered_product *grenade_source = qa_offhand_grenade_source(catalog, current);
  const char *grenades = eq(value(m, F_GRENADES), "native")
      ? (defaults.hand_grenades.is_enabled ? "enabled" : "disabled") : value(m, F_GRENADES);
  const qa_catalog_discovered_product *environment_product = cat_product(m, "q2-rerelease-baseq2");
  row_vec rows = {0};
  cvec c = {0};
  cvec_push(&c, choice3("disabled", "Off", NULL));
  cvec_push(&c, choice3("audio-content", "Game default", NULL));
  cvec_push(&c, choice3("q2-rerelease-baseq2", "Quake II environments",
      unavailable_of(environment_product, a) == NULL ? NULL : "Requires Quake II rerelease data"));
  row_push(&rows, a, "environment", "Environment", &c, value(m, F_ENVIRONMENT));
  cvec_push(&c, choice3("source", "Game default", NULL));
  cvec_push(&c, choice3("disabled", "Off", NULL));
  row_push(&rows, a, "doppler", "Doppler", &c, value(m, F_DOPPLER));
  for (size_t i = 0; i < catalog->product_count; i++) cvec_push(&c, product_choice(&catalog->products[i], a));
  row_push(&rows, a, "product", "Game / mod", &c, value(m, F_PRODUCT));
  for (size_t i = 0; i < catalog->product_count; i++) cvec_push(&c, product_choice(&catalog->products[i], a));
  row_push(&rows, a, "mapProduct", "Map content", &c, value(m, F_MAP_PRODUCT));
  pchoices_push_all(&c, maps_of(m));
  row_push(&rows, a, "map", "Starting map", &c, value(m, F_MAP));
  row_push(&rows, a, "movement", "Movement", &movement_choices, value(m, F_MOVEMENT));
  for (size_t i = 0; i < character_choices.count; i++) cvec_push(&c, character_choices.items[i]);
  row_push(&rows, a, "character", "Character source", &c, value(m, F_CHARACTER));
  pchoices_push_all(&c, models_of(m));
  row_push(&rows, a, "model", "Character model", &c, value(m, F_MODEL));
  if (product_field(m, F_CHARACTER)->expectation.family == QA_GAME_FAMILY_Q1) {
    color_choices(&c);
    row_push(&rows, a, "topColor", "Shirt color", &c, value(m, F_TOP_COLOR));
    color_choices(&c);
    row_push(&rows, a, "bottomColor", "Pants color", &c, value(m, F_BOTTOM_COLOR));
  } else {
    skins_of(m, a, &c);
    row_push(&rows, a, "skin", "Skin", &c, value(m, F_SKIN));
  }
  cvec_push(&c, native_weapons);
  for (size_t i = 0; i < catalog->product_count; i++)
    if (qa_supports_selected_weapon_product(&catalog->products[i].expectation)) cvec_push(&c, product_choice(&catalog->products[i], a));
  row_push(&rows, a, "weapons", "Weapons", &c, value(m, F_WEAPONS));
  if (geometry(m)->expectation.family == QA_GAME_FAMILY_Q3) {
    cvec_push(&c, choice3("native", arena_printf(a, "%s enemies", product_choice(geometry(m), a).label), NULL));
    for (size_t i = 0; i < character_choices.count; i++) cvec_push(&c, character_choices.items[i]);
    row_push(&rows, a, "enemies", "Enemies", &c, value(m, F_ENEMIES));
  } else {
    const char *q3_note = family == QA_GAME_FAMILY_Q3 ? "This map has no supported authored monster roster" : NULL;
    cvec_push(&c, native_monsters);
    cvec_push(&c, choice3("custom", eq(value(m, F_ENEMIES), "custom") ? arena_printf(a, "%s (custom)", source_label) : "Custom roster", q3_note));
    for (size_t i = 0; i < source_choices.count; i++) {
      if (eq(source_choices.items[i].id, "native")) continue;
      choice_t source = source_choices.items[i];
      source.unavailable = family == QA_GAME_FAMILY_Q3 ? q3_note : source.unavailable;
      cvec_push(&c, source);
    }
    row_push(&rows, a, "enemies", "Monsters", &c, monster_value);
  }
  free(character_choices.items);
  cvec_push(&c, choice3("disabled", "Off", NULL));
  cvec_push(&c, choice3("slot", "Weapon slot", hook_unavailable));
  cvec_push(&c, choice3("offhand", "Offhand", hook_unavailable));
  row_push(&rows, a, "grapple", "Hook", &c, placement);
  if (!eq(placement, "disabled")) {
    for (size_t i = 0; i < styles.count; i++) cvec_push(&c, choice3(styles.all[i].id, styles.all[i].title, styles.all[i].unavailable));
    row_push(&rows, a, "grappleStyle", "Hook style", &c, style);
  }
  cvec_push(&c, choice3("disabled", "Off", NULL));
  cvec_push(&c, choice3("enabled", "On", grenade_source == NULL ? "Requires Quake II grenade assets" : NULL));
  row_push(&rows, a, "grenades", "Offhand grenades", &c, grenades);
  cvec_push(&c, choice3("singleplayer", "Single player", NULL));
  cvec_push(&c, choice3("coop", "Cooperative", NULL));
  cvec_push(&c, choice3("deathmatch", "Deathmatch", NULL));
  row_push(&rows, a, "mode", "Game mode", &c, value(m, F_MODE));
  const qa_catalog_discovered_product *source = qa_source_program_product(catalog, product_field(m, F_PRODUCT)->id);
  cvec_push(&c, choice3("standard", "Standard", NULL));
  static const char *const pvp[3] = {"tag", "deathball", "horde"};
  for (int i = 0; i < 3; i++)
    cvec_push(&c, choice3(pvp[i], match_rule_label(pvp[i]), qa_app_match_mode_unavailable(match_selection(source, values_mode(m), pvp[i]))));
  const qa_catalog_discovered_product *selected_product = product_field(m, F_PRODUCT);
  const char *selected_edition = eq(edition_of(selected_product), "rerelease") ? "rerelease" : "classic";
  const char *source_edition = eq(edition_of(source), "rerelease") ? "rerelease" : "classic";
  static const char *const flags[2] = {"ctf", "lmctf"};
  for (int i = 0; i < 2; i++) {
    const char *rule = flags[i];
    const char *note;
    if (eq(rule, "ctf") && selected_product->expectation.family == QA_GAME_FAMILY_Q3) note = NULL;
    else if (eq(rule, "ctf") && selected_product->expectation.family == QA_GAME_FAMILY_Q1)
      note = unavailable_of(cat_product(m, arena_printf(a, "q1-%s-ctf", selected_edition)), a);
    else if (eq(rule, "lmctf")) note = unavailable_of(cat_product(m, arena_printf(a, "q2-%s-lmctf", source_edition)), a);
    else if (selected_product->expectation.family != QA_GAME_FAMILY_Q2) note = "Requires a Quake II campaign";
    else note = unavailable_of(cat_product(m, arena_printf(a, "q2-%s-%s", selected_edition, rule)), a);
    cvec_push(&c, choice3(rule, eq(rule, "ctf") ? "Capture the Flag" : "Loki's Minions CTF", note));
  }
  row_push(&rows, a, "rules", "Match rules", &c, value(m, F_RULES));
  for (int i = 0; i < 4; i++) cvec_push(&c, choice3(difficulty_ids[i], difficulty_labels[i], NULL));
  row_push(&rows, a, "skill", "Difficulty", &c, value(m, F_SKILL));
  static const char *const seat_ids[4] = {"1", "2", "3", "4"};
  for (int i = 0; i < 4; i++) cvec_push(&c, choice1(seat_ids[i]));
  row_push(&rows, a, "seats", "Local players", &c, value(m, F_SEATS));
  cvec_push(&c, choice3("gl", "OpenGL", NULL));
  cvec_push(&c, choice3("cpu", "Software", NULL));
  row_push(&rows, a, "renderer", "Renderer", &c, value(m, F_RENDERER));
  qa_cleanup_pop(&styles_node);
  hook_styles_free(&styles);
  arena_keep(a, rows.items);
  *out_count = rows.count;
  return rows.items;
}
const qa_app_startup_selection_row *qa_app_startup_selection_rows(qa_app_startup_selection_model *m, size_t *out_count) {
  arena_reset(&m->rows_arena);
  return rows_build(m, &m->rows_arena, out_count);
}

/* ---------------------------------------------------------------- */
/* select / setDisplay / options / summary.                          */

void qa_app_startup_selection_select(qa_app_startup_selection_model *m, const char *field_name, const char *id) {
  arena a = {0};
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, arena_cleanup, &a);
  size_t row_count = 0;
  const qa_app_startup_selection_row *rows = rows_build(m, &a, &row_count);
  const choice_t *selected = NULL;
  for (size_t i = 0; i < row_count; i++)
    if (eq(rows[i].id, field_name)) { selected = choices_find(rows[i].choices, rows[i].choice_count, id); break; }
  if (selected == NULL) fail("Unknown %s selection: %s", field_name, id);
  if (selected->unavailable != NULL) fail("%s", selected->unavailable);
  qa_cleanup_pop(&cleanup);
  arena_free(&a);
  int field = field_of(field_name);
  if (field == F_ENEMIES && geometry(m)->expectation.family == QA_GAME_FAMILY_Q3) { set_value(m, F_ENEMIES, id); return; }
  if (field == F_ENEMIES && !eq(id, "native") && !eq(id, "custom")) { qa_app_startup_selection_select_monster_source(m, id); return; }
  set_value(m, field, id);
  if (field == F_PRODUCT) {
    const qa_catalog_discovered_product *selected_product = product_field(m, F_PRODUCT);
    if (selected_product->expectation.family != QA_GAME_FAMILY_Q1 || eq(edition_of(selected_product), "quakeworld"))
      m->initial.has_q1_protocol = false;
    set_value(m, F_MAP_PRODUCT, id);
    reset_default_map(m);
    if (product_field(m, F_PRODUCT)->expectation.family == QA_GAME_FAMILY_Q3) set_value(m, F_ENEMIES, "native");
  }
  if (field == F_MAP_PRODUCT) reset_default_map(m);
  if (field == F_MAP_PRODUCT && geometry(m)->expectation.family != QA_GAME_FAMILY_Q3 && !eq(value(m, F_ENEMIES), "native")
      && !eq(value(m, F_ENEMIES), "custom")) set_value(m, F_ENEMIES, "native");
  if (field == F_MODE || field == F_PRODUCT) {
    const qa_catalog_discovered_product *product = qa_source_program_product(m->current, product_field(m, F_PRODUCT)->id);
    const char *rules = value(m, F_RULES);
    if (eq(rules, "standard") || eq(rules, "ctf") || eq(rules, "lmctf") || eq(rules, "tag") || eq(rules, "deathball") || eq(rules, "horde"))
      if (qa_app_match_mode_unavailable(match_selection(product, values_mode(m), rules)) != NULL) set_value(m, F_RULES, "standard");
  }
  if (field == F_MODEL) smap_set(m->selected_models, value(m, F_CHARACTER), xdup(id));
  if (field == F_MODEL || field == F_CHARACTER) {
    if (value(m, F_SKIN)[0] != '\0') {
      arena skins_arena = {0};
      cvec skins = {0};
      skins_of(m, &skins_arena, &skins);
      bool known = cvec_has(&skins, value(m, F_SKIN));
      free(skins.items);
      arena_free(&skins_arena);
      if (!known) set_value(m, F_SKIN, "");
    }
  }
  if (field == F_CHARACTER) {
    qa_game_family family = product_field(m, F_CHARACTER)->expectation.family;
    const char *remembered = smap_get(m->selected_models, id);
    const char *preferred = remembered != NULL ? remembered : default_model(family);
    const pchoices *models = models_of(m);
    const char *model = NULL;
    for (size_t i = 0; i < models->count && model == NULL; i++) if (eq(models->items[i].id, preferred)) model = models->items[i].id;
    if (model == NULL) model = models->count > 0 ? models->items[0].id : "";
    set_value(m, F_MODEL, model);
    smap_set(m->selected_models, id, xdup(value(m, F_MODEL)));
  }
}
void qa_app_startup_selection_set_display(qa_app_startup_selection_model *m, double width, double height, double gamma) {
  double sizes[2] = {width, height};
  for (int i = 0; i < 2; i++)
    if (!(isfinite(sizes[i]) && trunc(sizes[i]) == sizes[i] && fabs(sizes[i]) <= (double)QA_MAX_SAFE_INTEGER) || sizes[i] <= 0 || sizes[i] > 16384)
      qa_range_error("Invalid display settings");
  if (!isfinite(gamma) || gamma < 0.5 || gamma > 3) qa_range_error("Invalid display settings");
  m->display_width = width;
  m->display_height = height;
  m->display_gamma = gamma;
  m->display_overrides_consumed = true;
}
/* TS get options(). */
static void options_build(qa_app_startup_selection_model *m, qa_app_options *out) {
  mode_rules selected = options_mode_rules(m);
  qa_app_startup_options_copy(out, &m->initial);
  bool keep_protocol = eq(m->initial.network_kind, "native-server") && qa_app_startup_selection_hosting(m).has_q1_protocol
      && m->initial.has_q1_protocol;
  out->has_q1_protocol = keep_protocol;
  out->has_weapon_behavior = false;
  free(out->weapon_behavior_product); out->weapon_behavior_product = NULL;
  free(out->weapon_behavior_id); out->weapon_behavior_id = NULL;
  if (m->has_selected_mods) {
    size_t count = 0;
    const qa_mod_selection *enabled = qa_mod_selection_set_enabled(&m->selected_mods, &count);
    set_mods(out, enabled, count);
  } else {
    qa_mod_selection *initial = NULL; size_t count = 0;
    enabled_initial_mods(&m->initial, &initial, &count);
    set_mods(out, initial, count);
    free(initial);
  }
  const qa_catalog_discovered_product *movement = product_field(m, F_MOVEMENT), *character = product_field(m, F_CHARACTER);
  set_text(&out->product, value(m, F_PRODUCT));
  set_text(&out->map_product, value(m, F_MAP_PRODUCT));
  set_text(&out->map, value(m, F_MAP));
  out->movement = movement->expectation.family;
  set_text(&out->movement_product, movement->expectation.id);
  out->character = character->expectation.family;
  set_text(&out->character_model, value(m, F_MODEL));
  if (value(m, F_SKIN)[0] != '\0') set_text(&out->character_skin, value(m, F_SKIN));
  out->has_character_top_color = true;
  out->character_top_color = qa_js_string_to_number(value(m, F_TOP_COLOR));
  out->has_character_bottom_color = true;
  out->character_bottom_color = qa_js_string_to_number(value(m, F_BOTTOM_COLOR));
  out->mode = selected.mode;
  out->rules = selected.rules;
  out->skill = selected.skill;
  out->seats = qa_js_string_to_number(value(m, F_SEATS));
  out->renderer = selected.renderer;
  apply_display(m, out);
  apply_selected_server_profile(m, out);
}
void qa_app_startup_selection_options(qa_app_startup_selection_model *m, qa_app_options *out) {
  qa_app_options options;
  options_build(m, &options);
  *out = options;
}
const char *const *qa_app_startup_selection_summary(qa_app_startup_selection_model *m, size_t *out_count) {
  arena *a = &m->summary_arena;
  arena_reset(a);
  size_t row_count = 0;
  const qa_app_startup_selection_row *rows = rows_build(m, a, &row_count);
  const char **lines = arena_alloc(a, (row_count + 1) * sizeof *lines);
  size_t count = 0;
  for (size_t i = 0; i < row_count; i++) {
    if (eq(rows[i].id, "renderer")) continue;
    const choice_t *c = choices_find(rows[i].choices, rows[i].choice_count, rows[i].value);
    lines[count++] = arena_printf(a, "%s: %s", rows[i].label, c != NULL ? c->label : rows[i].value);
  }
  lines[count++] = "Pickups: authored map items supply the selected arsenal through its admitted mappings. Independent pickup replacement is not implemented.";
  *out_count = count;
  return lines;
}

/* ---------------------------------------------------------------- */
/* Bindings and equipment.                                           */

static const qa_catalog_discovered_product *binding_product(qa_app_startup_selection_model *m) {
  return eq(value(m, F_WEAPONS), "native") ? product_field(m, F_PRODUCT) : cat_require(m, value(m, F_WEAPONS));
}
qa_weapon_binding_item *qa_app_startup_selection_binding_items(qa_app_startup_selection_model *m, size_t *out_count) {
  const qa_catalog_discovered_product *product = binding_product(m);
  qa_weapon_binding_item *items = qa_base_weapon_binding_items(product->expectation.family, product->expectation.campaign, NULL, out_count);
  if (items == NULL && *out_count > 0) qa_range_error("Out of memory");
  return items;
}
const char *qa_app_startup_selection_binding_items_key(qa_app_startup_selection_model *m) {
  arena_reset(&m->key_arena);
  const qa_catalog_discovered_product *product = binding_product(m);
  return arena_printf(&m->key_arena, "%s:%s", family_text(product->expectation.family),
      product->expectation.campaign != NULL ? product->expectation.campaign : "undefined");
}
/* TS selectedEquipment(baseline). */
static qa_equipment_selection selected_equipment(qa_app_startup_selection_model *m, qa_equipment_selection baseline) {
  qa_equipment_selection equipment = baseline;
  if (eq(value(m, F_GRAPPLE), "disabled")) equipment.grapple = qa_disabled_equipment().grapple;
  else {
    const char *binding = eq(value(m, F_GRAPPLE), "native") ? (baseline.grapple.is_enabled ? baseline.grapple.binding : "disabled") : value(m, F_GRAPPLE);
    if (!eq(binding, "disabled")) {
      if (!eq(binding, "slot") && !eq(binding, "offhand")) fail("Invalid hook placement");
      hook_styles styles = hook_styles_of(m);
      qa_cleanup cleanup;
      qa_cleanup_push(&cleanup, hook_styles_cleanup, &styles);
      const char *id = NULL;
      if (!eq(value(m, F_GRAPPLE_STYLE), "native")) id = value(m, F_GRAPPLE_STYLE);
      else if (baseline.grapple.is_enabled) id = eq(baseline.grapple.mechanic, "q3-qvm") ? baseline.grapple.profile->id : baseline.grapple.mechanic;
      else for (size_t i = 0; i < styles.count && id == NULL; i++) if (styles.all[i].unavailable == NULL) id = styles.all[i].id;
      const qa_grapple_style *style = NULL;
      for (size_t i = 0; id != NULL && i < styles.count && style == NULL; i++) if (eq(styles.all[i].id, id)) style = &styles.all[i];
      if (style == NULL || style->unavailable != NULL) {
        char message[1024];
        snprintf(message, sizeof message, "%s", style != NULL ? style->unavailable : "Select an installed hook style");
        fail("%s", message);
      }
      equipment.grapple = style->selection;
      equipment.grapple.binding = eq(binding, "slot") ? "slot" : "offhand";
      qa_cleanup_pop(&cleanup);
      hook_styles_free(&styles);
    }
  }
  if (eq(value(m, F_GRENADES), "disabled")) equipment.hand_grenades = qa_disabled_equipment().hand_grenades;
  else if (eq(value(m, F_GRENADES), "enabled")) {
    const qa_catalog_discovered_product *product = qa_offhand_grenade_source(m->current, product_field(m, F_PRODUCT));
    if (product == NULL) fail("Offhand grenades require Quake II grenade assets");
    const char *edition = edition_of(product);
    if (!eq(edition, "classic") && !eq(edition, "rerelease")) fail("Invalid grenade asset source");
    qa_hand_grenade_selection grenades;
    memset(&grenades, 0, sizeof grenades);
    grenades.is_enabled = true;
    grenades.source.provider = qa_equipment_provider_ids.hand_grenades;
    grenades.source.content = product->id;
    grenades.edition = edition;
    grenades.initial_ammo = 5;
    grenades.capacity = 50;
    equipment.hand_grenades = grenades;
  }
  return equipment;
}
typedef struct options_scope { qa_app_options options; bool live; qa_app_application_preset_result preset; bool has_preset; } options_scope;
static void options_scope_cleanup(void *arg) {
  options_scope *scope = arg;
  if (scope->has_preset) qa_app_application_preset_result_free(&scope->preset);
  if (scope->live) qa_app_options_destroy(&scope->options);
}
qa_ui_binding_capabilities qa_app_startup_selection_binding_capabilities(qa_app_startup_selection_model *m) {
  options_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, options_scope_cleanup, &scope);
  options_build(m, &scope.options);
  scope.live = true;
  if (!qa_app_application_preset_for_catalog(m->current, &scope.options, NULL, NULL, &scope.preset)) qa_range_error("Out of memory");
  scope.has_preset = true;
  qa_equipment_selection equipment = selected_equipment(m, scope.preset.preset.equipment);
  qa_game_family family = cat_require(m, scope.preset.preset.engine_behavior.content)->expectation.family;
  qa_ui_binding_capabilities out = {family != QA_GAME_FAMILY_Q1,
      family == QA_GAME_FAMILY_Q2 ? "score" : family == QA_GAME_FAMILY_Q3 ? "+scores" : NULL,
      equipment.grapple.is_enabled && eq(equipment.grapple.binding, "offhand"), equipment.hand_grenades.is_enabled};
  qa_cleanup_pop(&cleanup);
  options_scope_cleanup(&scope);
  return out;
}

/* ---------------------------------------------------------------- */
/* resolve().                                                        */

static void resolve(qa_app_startup_selection_model *m, qa_app_startup_launch *out) {
  memset(out, 0, sizeof *out);
  if (eq(value(m, F_MODE), "deathmatch") || !eq(value(m, F_RULES), "standard")) {
    char *path = xdup(value(m, F_MAP));
    qa_cleanup path_node;
    qa_cleanup_push(&path_node, free, path);
    prepare_map_classnames(m, path);
    qa_cleanup_pop(&path_node);
    free(path);
  }
  {
    arena a = {0};
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, arena_cleanup, &a);
    size_t row_count = 0;
    const qa_app_startup_selection_row *rows = rows_build(m, &a, &row_count);
    for (size_t i = 0; i < row_count; i++) {
      const choice_t *selected = choices_find(rows[i].choices, rows[i].choice_count, rows[i].value);
      if (selected == NULL || selected->unavailable != NULL)
        fail("%s: %s", rows[i].label, selected != NULL ? selected->unavailable : "choose an installed option");
    }
    qa_cleanup_pop(&cleanup);
    arena_free(&a);
  }
  launch_backing *backing = xcalloc(1, sizeof *backing);
  out->backing = backing;
  launch_scope scope = {out};
  qa_cleanup launch_node;
  qa_cleanup_push(&launch_node, launch_scope_cleanup, &scope);
  options_build(m, &out->options);
  const qa_app_options *options = &out->options;
  if (!qa_app_application_preset_for_catalog(m->current, options, NULL, NULL, &backing->preset)) qa_range_error("Out of memory");
  backing->has_preset = true;
  const qa_launch_preset *base = &backing->preset.preset;
  const qa_catalog_discovered_product *map_content = geometry(m);
  const qa_catalog_discovered_product *movement_product = product_field(m, F_MOVEMENT), *character_product = product_field(m, F_CHARACTER);
  backing->movement.provider = arena_printf(&backing->strings, "%s:movement", family_text(movement_product->expectation.family));
  backing->movement.content = qa_app_movement_content_product(m->current, movement_product->expectation.id)->id;
  backing->character.definition.provider = arena_printf(&backing->strings, "%s:character", family_text(character_product->expectation.family));
  backing->character.definition.content = character_product->id;
  if (!qa_native_provider_timing(backing->movement, movement_product->expectation.family, eq(edition_of(movement_product), "rerelease"),
                                 &backing->movement_timing)) qa_range_error("Out of memory");
  backing->has_movement_timing = true;
  if (!qa_native_provider_timing(backing->character.definition, character_product->expectation.family,
                                 eq(edition_of(character_product), "rerelease"), &backing->character_timing)) qa_range_error("Out of memory");
  backing->has_character_timing = true;
  backing->timing = xcalloc(base->timing_count, sizeof *backing->timing);
  for (size_t i = 0; i < base->timing_count; i++) {
    const qa_provider_timing *profile = &base->timing[i];
    if (eq(profile->provider, backing->movement.provider)) {
      backing->timing[i] = backing->movement_timing;
      if (eq(edition_of(movement_product), "quakeworld")) {
        memset(&backing->timing[i].clock, 0, sizeof backing->timing[i].clock);
        backing->timing[i].clock.kind = QA_CLOCK_Q1_QUAKEWORLD;
        backing->timing[i].clock.maximum_command_milliseconds = 255;
      }
    } else if (eq(profile->provider, backing->character.definition.provider)) {
      backing->timing[i] = backing->character_timing;
      if (eq(edition_of(character_product), "quakeworld")) {
        memset(&backing->timing[i].clock, 0, sizeof backing->timing[i].clock);
        backing->timing[i].clock.kind = QA_CLOCK_Q1_QUAKEWORLD;
        backing->timing[i].clock.maximum_command_milliseconds = 50;
      }
    } else backing->timing[i] = *profile;
  }
  qa_environment_selection environment;
  memset(&environment, 0, sizeof environment);
  if (eq(value(m, F_ENVIRONMENT), "audio-content")) environment.kind = QA_ENVIRONMENT_AUDIO_CONTENT;
  else if (eq(value(m, F_ENVIRONMENT), "disabled")) environment.kind = QA_ENVIRONMENT_DISABLED;
  else {
    environment.kind = QA_ENVIRONMENT_SELECTED;
    environment.resource.content = cat_require(m, value(m, F_ENVIRONMENT))->id;
    environment.resource.path = "sound/default.environments";
  }
  const char *doppler = value(m, F_DOPPLER);
  if (!eq(doppler, "source") && !eq(doppler, "disabled")) fail("Invalid Doppler selection");
  backing->effective = *base;
  backing->effective.timing = backing->timing;
  backing->effective.presentation.environment = environment;
  backing->effective.presentation.doppler.is_source = eq(doppler, "source");
  qa_launch_choice choice = qa_preset_choice(backing->effective.id);
  backing->map.geometry.content = map_content->id;
  backing->map.geometry.path = options->map;
  backing->map.entities = base->map.entities;
  choice.map.is_selected = true; choice.map.value = &backing->map;
  choice.movement.is_selected = true; choice.movement.value = &backing->movement;
  backing->character.appearance.provider = arena_printf(&backing->strings, "%s:model/%s", family_text(options->character),
      options->character_model != NULL ? options->character_model : "");
  backing->character.appearance.content = backing->character.definition.content;
  choice.character.is_selected = true; choice.character.value = &backing->character;
  if (eq(options->mode, "deathmatch")) memset(&backing->campaign, 0, sizeof backing->campaign);
  else backing->campaign = base->campaign;
  choice.campaign.is_selected = true; choice.campaign.value = &backing->campaign;
  if (!eq(value(m, F_WEAPONS), "native")) {
    const qa_catalog_discovered_product *product = cat_require(m, value(m, F_WEAPONS));
    qa_provider_reference weapon = {arena_printf(&backing->strings, "%s:official", family_text(product->expectation.family)), product->id};
    backing->weapon = qa_canonical_weapon_source(base->map.entities, weapon, m->current, &backing->weapon_provider);
    backing->weapons.values = &backing->weapon;
    backing->weapons.count = 1;
    choice.weapons.is_selected = true; choice.weapons.value = &backing->weapons;
  }
  if (map_content->expectation.family == QA_GAME_FAMILY_Q3 && !eq(value(m, F_ENEMIES), "native")) {
    const qa_catalog_discovered_product *product = cat_require(m, value(m, F_ENEMIES));
    memset(&backing->enemies, 0, sizeof backing->enemies);
    backing->enemies.kind = QA_ENEMY_SELECTION_PLAYERS;
    backing->enemies.character.provider = arena_printf(&backing->strings, "%s:character", family_text(product->expectation.family));
    backing->enemies.character.content = product->id;
    choice.enemies.is_selected = true; choice.enemies.value = &backing->enemies;
  } else if (eq(value(m, F_ENEMIES), "custom")) {
    backing->enemies = effective_monster_roster(m, true, &backing->strings);
    choice.enemies.is_selected = true; choice.enemies.value = &backing->enemies;
  }
  backing->equipment = selected_equipment(m, base->equipment);
  choice.equipment.is_selected = true; choice.equipment.value = &backing->equipment;
  launch_resolve(m, backing, &backing->effective, choice);
  out->recipe = backing->resolved.recipe;
  backing->applied = qa_app_apply_application_mods(&out->recipe, m->mod_choices != NULL ? m->mod_choices->items : NULL,
      m->mod_choices != NULL ? m->mod_choices->count : 0, options->mods, options->mod_count);
  backing->has_applied = true;
  out->recipe = backing->applied.recipe;
  out->options.explicit_skill = out->options.explicit_mode = out->options.explicit_capacity = true;
  qa_cleanup_pop(&launch_node);
}
void qa_app_startup_selection_resolve(qa_app_startup_selection_model *m, qa_app_startup_launch *out) { resolve(m, out); }

/* ---------------------------------------------------------------- */
/* refreshCatalog / createStartupSelection.                          */

static void model_shell_destroy(qa_app_startup_selection_model *m) { qa_app_startup_selection_model_destroy(m); }
static void candidate_cleanup(void *arg) { model_shell_destroy(arg); }
void qa_app_startup_selection_refresh_catalog(qa_app_startup_selection_model *m) {
  qa_content_catalog *catalog = discover_catalog(m->current, true);
  qa_cleanup catalog_node;
  qa_cleanup_push(&catalog_node, catalog_release, catalog);
  const qa_catalog_discovered_product *initial_product = NULL;
  for (size_t i = 0; i < catalog->product_count && initial_product == NULL; i++)
    if (eq(catalog->products[i].expectation.id, m->initial.product) && installed(&catalog->products[i])) initial_product = &catalog->products[i];
  for (size_t i = 0; i < catalog->product_count && initial_product == NULL; i++)
    if (installed(&catalog->products[i])) initial_product = &catalog->products[i];
  if (initial_product == NULL) fail("No installed game content remains");
  qa_app_options candidate_options;
  qa_app_startup_options_copy(&candidate_options, &m->initial);
  set_text(&candidate_options.product, initial_product->expectation.id);
  qa_cleanup_pop(&catalog_node);
  qa_app_startup_selection_model *candidate = model_create(catalog, &candidate_options);
  qa_app_options_destroy(&candidate_options);
  qa_cleanup candidate_node;
  qa_cleanup_push(&candidate_node, candidate_cleanup, candidate);
  prepare_maps(candidate);
  qa_cleanup_pop(&candidate_node);
  /* Adopt the candidate's catalogs, initial options, campaign, mod choices and caches. */
  for (size_t i = 0; i < candidate->owned.count; i++) retain(&m->owned, candidate->owned.items[i], candidate->owned.frees[i]);
  candidate->owned.count = 0;
  m->current = candidate->current;
  qa_app_options_destroy(&m->initial);
  m->initial = candidate->initial;
  memset(&candidate->initial, 0, sizeof candidate->initial);
  if (m->team_campaign) qa_app_team_arena_campaign_destroy(m->team_campaign);
  m->team_campaign = candidate->team_campaign;
  candidate->team_campaign = NULL;
  m->mod_choices = candidate->mod_choices;
  qvm_styles_free(m);
  m->qvm_styles = candidate->qvm_styles; m->qvm_style_count = candidate->qvm_style_count;
  candidate->qvm_styles = NULL; candidate->qvm_style_count = 0;
  if (m->has_selected_mods && m->mod_choices != NULL) {
    qa_mod_description *descriptions = choice_descriptions(m->mod_choices);
    qa_mod_selection_set_refresh(&m->selected_mods, descriptions, m->mod_choices->count);
    free(descriptions);
  }
#define ADOPT(FIELD) do { smap *swap = m->FIELD; m->FIELD = candidate->FIELD; candidate->FIELD = swap; } while (0)
  ADOPT(playable_maps);
  ADOPT(authored_default_maps);
  ADOPT(map_classnames);
  ADOPT(monster_classes);
#undef ADOPT
  /* looseModels: copied with fresh generations (TS assigns the candidate's list objects). */
  smap_clear(m->loose_models);
  for (size_t i = 0; i < candidate->loose_models->count; i++)
    smap_set(m->loose_models, candidate->loose_models->keys[i], slist_copy(candidate->loose_models->values[i], ++m->loose_generation));
  smap_clear(m->eligible_maps);
  smap_clear(m->model_choices);
  if (find_product_by_id(catalog, value(m, F_MAP_PRODUCT)) == NULL) set_value(m, F_MAP_PRODUCT, initial_product->expectation.id);
  if (find_product_by_id(catalog, value(m, F_PRODUCT)) == NULL) {
    set_value(m, F_PRODUCT, initial_product->expectation.id);
    set_value(m, F_MAP_PRODUCT, value(m, F_PRODUCT));
    reset_default_map(m);
  }
  model_shell_destroy(candidate);
}
static void free_addon_roots(void *arg) {
  qa_app_options *options = arg;
  qa_nightdive_addon_roots_free(options->addon_content_roots, options->addon_content_root_count);
  free(options->user_content_root);
}
qa_app_startup_selection_model *qa_app_create_startup_selection(const qa_app_options *options) {
  qa_app_options roots;
  memset(&roots, 0, sizeof roots);
  roots.user_content_root = options->user_content_root != NULL ? xdup(options->user_content_root) : qa_default_user_content_root();
  if (roots.user_content_root == NULL) qa_range_error("Out of memory");
  if (options->addon_content_roots != NULL) {
    roots.addon_content_roots = xcalloc(options->addon_content_root_count, sizeof *roots.addon_content_roots);
    roots.addon_content_root_count = options->addon_content_root_count;
    for (size_t i = 0; i < options->addon_content_root_count; i++) {
      roots.addon_content_roots[i].family = options->addon_content_roots[i].family;
      roots.addon_content_roots[i].root = xdup(options->addon_content_roots[i].root);
    }
  } else if (!qa_default_addon_content_roots(NULL, &roots.addon_content_roots, &roots.addon_content_root_count))
    qa_range_error("Out of memory");
  qa_content_catalog *catalog = xcalloc(1, sizeof *catalog);
  qa_discover_content_options discover;
  memset(&discover, 0, sizeof discover);
  discover.corpus_root = options->corpus_root;
  discover.user_content_root = roots.user_content_root;
  discover.addon_content_roots = (const qa_catalog_nightdive_addon_root *)roots.addon_content_roots;
  discover.addon_content_root_count = roots.addon_content_root_count;
  discover.discover_mods = true;
  discover.inspector = qa_app_default_archive_inspector();
  qa_cleanup roots_node;
  qa_cleanup_push(&roots_node, free_addon_roots, &roots);
  if (!qa_discover_installed_content(&discover, catalog)) { free(catalog); qa_range_error("Out of memory"); }
  qa_cleanup_pop(&roots_node);
  free_addon_roots(&roots);
  qa_app_startup_selection_model *model = model_create(catalog, options);
  qa_app_startup_stamp("discover-installed-content");
  qa_cleanup model_node;
  qa_cleanup_push(&model_node, candidate_cleanup, model);
  prepare_maps(model);
  qa_cleanup_pop(&model_node);
  return model;
}

/* ---------------------------------------------------------------- */
/* Base arenas (TS refreshBaseArenas with readArenaSelection).       */

static char **arena_list_files(void *ctx, const char *directory, const char *extension, size_t *out_count) {
  *out_count = 0;
  char **files = qa_content_mounted_list_files(ctx, directory, extension, out_count);
  if (!files && *out_count > 0) qa_range_error("Out of memory");
  return files;
}
static char *arena_open_text(void *ctx, const char *path) {
  qa_content_opened_resource opened = {0};
  if (!qa_content_mounted_open(ctx, path, NULL, NULL, &opened)) return NULL;
  char *text = bytes_text(opened.bytes, opened.byte_length);
  free(opened.bytes);
  return text;
}
typedef struct arena_read {
  qa_app_options options; bool has_options;
  void *q3_backing;
  qa_app_application_preset_result preset; bool has_preset;
  qa_app_configuration_content *content;
  qa_config_store store; bool has_store;
  qa_cvar_archive_entry *entries; size_t entry_count;
  qa_cvar_archive_entry *kept;
} arena_read;
static void arena_read_release(void *arg) {
  arena_read *read = arg;
  free(read->kept);
  qa_app_cvar_archive_entries_free(read->entries, read->entry_count);
  if (read->has_store) qa_config_store_destroy(&read->store);
  if (read->content) { qa_app_configuration_content_close(read->content); qa_app_configuration_content_destroy(read->content); }
  if (read->has_preset) qa_app_application_preset_result_free(&read->preset);
  qa_app_q3_product_backing_free(read->q3_backing);
  if (read->has_options) qa_app_options_destroy(&read->options);
  memset(read, 0, sizeof *read);
}
/* TS readArenaSelection(installed, options). */
static void read_arena_selection(qa_app_startup_selection_model *m, const qa_app_options *launch_options) {
  arena_read read;
  memset(&read, 0, sizeof read);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, arena_read_release, &read);
  qa_app_startup_options_copy(&read.options, launch_options);
  read.has_options = true;
  /* openInitialConfigurationContent(options, undefined, installed). */
  qa_app_prepared_q3_product prepared;
  qa_app_prepare_q3_application_product_native(m->current, read.options.product, &read.options, &prepared, &read.q3_backing);
  qa_content_catalog *catalog = (qa_content_catalog *)prepared.catalog;
  if (prepared.has_q3_product) { read.options.has_q3_product = true; read.options.q3_product = prepared.q3_product; }
  if (!qa_app_application_configuration_preset_for_catalog(catalog, &read.options, NULL, &read.preset)) qa_range_error("Out of memory");
  read.has_preset = true;
  qa_app_configuration_request_native request;
  memset(&request, 0, sizeof request);
  request.kind = "launch";
  request.preset = &read.preset.preset;
  request.choice = qa_preset_choice(read.preset.preset.id);
  read.content = qa_app_open_application_configuration_content_native(catalog, request,
      read.options.has_q3_product ? &read.options.q3_product : NULL);
  if (read.content == NULL) qa_range_error("Out of memory");
  qa_content_mounted *mounts = (qa_content_mounted *)qa_app_configuration_content_mounts(read.content);
  qa_app_arena_mounts arena_mounts = {mounts, arena_list_files, arena_open_text};
  qa_app_base_arena_catalog arenas = qa_app_read_base_arena_catalog(arena_mounts);
  arena_state_free(m);
  m->arena_catalog = arenas;
  if (!qa_identity_owner_create(&m->arena_identity, "arena-selection")) qa_range_error("Out of memory");
  m->has_arena_identity = true;
  qa_cvar_registry_options cvar_options;
  memset(&cvar_options, 0, sizeof cvar_options);
  cvar_options.dialect = QA_COMMAND_DIALECT_Q3;
  cvar_options.context.session = m->arena_identity.session;
  cvar_options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  m->arena_cvars = qa_cvar_registry_create(&cvar_options);
  if (m->arena_cvars == NULL) qa_range_error("Out of memory");
  qa_app_arena_progression_catalog progression = {arenas.regular_count, arenas.regular_count, -1, -1};
  for (size_t i = 0; i < arenas.arena_count; i++) if (arenas.arenas[i].special[0] != '\0') progression.total_levels++;
  for (size_t i = 0; i < arenas.arena_count; i++) {
    char *special = lower_dup(arenas.arenas[i].special);
    if (progression.training < 0 && strcmp(special, "training") == 0) progression.training = arenas.arenas[i].number;
    if (progression.final < 0 && strcmp(special, "final") == 0) progression.final = arenas.arenas[i].number;
    free(special);
  }
  m->arena_progression = qa_app_base_arena_progression_create(m->arena_cvars, progression);
  if (m->arena_progression == NULL) qa_range_error("Out of memory");
  if (!qa_cvar_registry_register(m->arena_cvars, "ui_spSelection", "", QA_CVAR_FLAG_ARCHIVE, NULL)) qa_range_error("Out of memory");
  qa_app_application_configuration_selection selection = qa_app_configuration_content_selection(read.content);
  if (!qa_app_configuration_store_for_catalog(catalog, &read.options, selection.source.content, &read.store)) qa_range_error("Out of memory");
  read.has_store = true;
  const char *owner[3] = {"source", selection.source.content, selection.source.provider};
  read.entries = qa_app_load_cvar_archive(&read.store, owner, 3, QA_COMMAND_DIALECT_Q3, &read.entry_count);
  read.kept = xcalloc(read.entry_count, sizeof *read.kept);
  size_t kept = 0;
  for (size_t i = 0; i < read.entry_count; i++) {
    qa_cvar_read found;
    if (qa_cvar_registry_find(m->arena_cvars, read.entries[i].name, &found)) {
      qa_cvar_read_free(&found);
      read.kept[kept++] = read.entries[i];
    }
  }
  qa_cvar_registry_apply_archive(m->arena_cvars, read.kept, kept);
  char *saved = qa_cvar_registry_variable_string(m->arena_cvars, "ui_spSelection");
  m->arenas = qa_app_arena_selection_make(&m->arena_catalog, m->arena_progression, saved);
  m->has_arenas = true;
  free(saved);
  qa_cleanup_pop(&cleanup);
  arena_read_release(&read);
}
const qa_app_arena_selection *qa_app_startup_selection_base_arenas(qa_app_startup_selection_model *m) {
  return m->has_arenas ? &m->arenas : NULL;
}
void qa_app_startup_selection_refresh_base_arenas(qa_app_startup_selection_model *m) {
  qa_app_startup_launch launch;
  resolve_preset(m, "q3-baseq3", false, 0, NULL, &launch);
  launch_scope scope = {&launch};
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, launch_scope_cleanup, &scope);
  read_arena_selection(m, &launch.options);
  qa_cleanup_pop(&cleanup);
  qa_app_startup_launch_free(&launch);
}
