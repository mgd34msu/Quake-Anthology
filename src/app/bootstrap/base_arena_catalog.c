/* Port of src/app/bootstrap/base-arena-catalog.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/base_arena_catalog.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/common_parse.h"
#include "qa/foundation/support.h"
typedef struct { char *key, *value; } field;
typedef struct { field *fields; size_t count; } info;
typedef struct { info *rows; size_t count, cap; } infos;
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static const char *get(const info *row, const char *key) {
  for (size_t i = 0; i < row->count; i++) if (strcmp(row->fields[i].key, key) == 0) return row->fields[i].value;
  return NULL;
}
static void free_infos(infos *all) {
  for (size_t i = 0; i < all->count; i++) {
    for (size_t j = 0; j < all->rows[i].count; j++) { free(all->rows[i].fields[j].key); free(all->rows[i].fields[j].value); }
    free(all->rows[i].fields);
  }
  free(all->rows);
}
/* Returns false on malformed text when tolerant (read path); aborts otherwise. */
static bool infos_from_text(const char *text, infos *all, bool tolerant) {
  qa_common_parse_state parser; qa_common_parse_state_init(&parser);
  qa_common_parse_cursor cursor;
  if (!qa_common_parse_cursor_init(&cursor, text, strlen(text), false)) return false;
  for (;;) {
    const char *token = qa_common_parse_state_parse(&parser, &cursor, true);
    if (!token || token[0] == '\0') break;
    if (strcmp(token, "{") != 0) {
      qa_common_parse_cursor_destroy(&cursor);
      if (tolerant) return false;
      qa_range_error("Malformed base arena definition");
    }
    info row = {NULL, 0};
    for (;;) {
      const char *key = qa_common_parse_state_parse(&parser, &cursor, true);
      if (key && strcmp(key, "}") == 0) break;
      if (!key || key[0] == '\0') {
        for (size_t j = 0; j < row.count; j++) { free(row.fields[j].key); free(row.fields[j].value); }
        free(row.fields);
        qa_common_parse_cursor_destroy(&cursor);
        if (tolerant) return false;
        qa_range_error("Unterminated base arena definition");
      }
      char *k = dup(key);
      const char *v = qa_common_parse_state_parse(&parser, &cursor, false);
      row.fields = realloc(row.fields, (row.count + 1) * sizeof *row.fields);
      row.fields[row.count++] = (field){k, dup(v && v[0] ? v : "<NULL>")};
    }
    if (all->count == all->cap) { all->cap = all->cap ? all->cap * 2 : 8; all->rows = realloc(all->rows, all->cap * sizeof *all->rows); }
    all->rows[all->count++] = row;
  }
  qa_common_parse_cursor_destroy(&cursor);
  return true;
}
static bool valid_map_name(const char *name) {
  if (!name || !name[0]) return false;
  for (const char *p = name; *p; p++)
    if (!isalnum((unsigned char)*p) && *p != '_' && *p != '/' && *p != '-') return false;
  return true;
}
static int parse_int10(const char *text) { return text ? (int)strtol(text, NULL, 10) : 0; }
static qa_app_base_arena_catalog catalog_from_infos(infos *all) {
  /* singles: type includes "single" and a usable map name. */
  size_t regular = 0;
  for (size_t i = 0; i < all->count; i++) {
    const char *type = get(&all->rows[i], "type");
    if (type && strstr(type, "single") && valid_map_name(get(&all->rows[i], "map")) && !get(&all->rows[i], "special")) regular++;
  }
  double regular_count = (double)((regular / 4) * 4);
  qa_app_base_arena_catalog catalog = {0};
  catalog.regular_count = regular_count;
  catalog.tier_count = regular_count / 4;
  double single = 0, special_number = regular_count;
  for (size_t i = 0; i < all->count; i++) {
    const info *row = &all->rows[i];
    const char *type = get(row, "type");
    if (!type || !strstr(type, "single") || !valid_map_name(get(row, "map"))) continue;
    const char *special = get(row, "special");
    special = special ? special : "";
    const char *name = get(row, "map");
    double number = special[0] ? special_number++ : single++;
    int frag = parse_int10(get(row, "fraglimit")), time = parse_int10(get(row, "timelimit"));
    qa_app_base_arena arena = {0};
    arena.number = number;
    char map[1024]; snprintf(map, sizeof map, "maps/%s.bsp", name);
    arena.map = dup(map);
    arena.title = dup(get(row, "longname") ? get(row, "longname") : name);
    arena.special = dup(special);
    char lower[256]; size_t li = 0;
    for (; special[li] && li < sizeof lower - 1; li++) lower[li] = (char)tolower((unsigned char)special[li]);
    lower[li] = '\0';
    arena.selection = strcmp(lower, "training") == 0 ? -4 : strcmp(lower, "final") == 0 ? regular_count : number;
    arena.frag_limit = frag == 0 && time == 0 ? 10 : frag;
    arena.time_limit = time;
    const char *bots = get(row, "bots");
    bots = bots ? bots : "";
    char *copy = dup(bots), *save = NULL;
    for (char *tok = strtok_r(copy, " \t\n\r\f\v", &save); tok; tok = strtok_r(NULL, " \t\n\r\f\v", &save)) {
      arena.bots = realloc(arena.bots, (arena.bot_count + 1) * sizeof *arena.bots);
      arena.bots[arena.bot_count++] = dup(tok);
    }
    free(copy);
    catalog.arenas = realloc(catalog.arenas, (catalog.arena_count + 1) * sizeof *catalog.arenas);
    catalog.arenas[catalog.arena_count++] = arena;
  }
  return catalog;
}
qa_app_base_arena_catalog qa_app_parse_base_arena_catalog(const char *const *texts, size_t text_count) {
  infos all = {0};
  for (size_t i = 0; i < text_count; i++) infos_from_text(texts[i], &all, false);
  qa_app_base_arena_catalog catalog = catalog_from_infos(&all);
  free_infos(&all);
  return catalog;
}
qa_app_base_arena_catalog qa_app_read_base_arena_catalog(qa_app_arena_mounts mounts) {
  infos all = {0};
  size_t name_count = 0;
  char **names = mounts.list_files(mounts.ctx, "scripts", ".arena", &name_count);
  /* files = arenas.txt then scripts/<name> for each .arena */
  char *text = mounts.open_text(mounts.ctx, "scripts/arenas.txt");
  if (text) { infos_from_text(text, &all, true); free(text); }
  for (size_t i = 0; i < name_count; i++) {
    char path[1024]; snprintf(path, sizeof path, "scripts/%s", names[i]);
    text = mounts.open_text(mounts.ctx, path);
    if (text) { infos_from_text(text, &all, true); free(text); }
    free(names[i]);
  }
  free(names);
  qa_app_base_arena_catalog catalog = catalog_from_infos(&all);
  free_infos(&all);
  return catalog;
}
void qa_app_base_arena_catalog_destroy(qa_app_base_arena_catalog *catalog) {
  if (!catalog) return;
  for (size_t i = 0; i < catalog->arena_count; i++) {
    free(catalog->arenas[i].map); free(catalog->arenas[i].title); free(catalog->arenas[i].special);
    for (size_t j = 0; j < catalog->arenas[i].bot_count; j++) free(catalog->arenas[i].bots[j]);
    free(catalog->arenas[i].bots);
  }
  free(catalog->arenas);
  *catalog = (qa_app_base_arena_catalog){0};
}
const qa_app_base_arena *qa_app_base_arena_for_map(const qa_app_base_arena_catalog *catalog, const char *map) {
  char path[1024], wanted[1100];
  size_t len = strlen(map), out = 0;
  for (size_t i = 0; i < len && out < sizeof path - 1; i++) path[out++] = (char)tolower((unsigned char)map[i]);
  path[out] = '\0';
  const char *stem = strncmp(path, "maps/", 5) == 0 ? path + 5 : path;
  char stem_buf[1024]; snprintf(stem_buf, sizeof stem_buf, "%s", stem);
  size_t slen = strlen(stem_buf);
  if (slen >= 4 && strcmp(stem_buf + slen - 4, ".bsp") == 0) stem_buf[slen - 4] = '\0';
  snprintf(wanted, sizeof wanted, "maps/%s.bsp", stem_buf);
  for (size_t i = 0; i < catalog->arena_count; i++) {
    bool eq = true;
    const char *a = catalog->arenas[i].map, *b = wanted;
    while (*a && *b) { if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) { eq = false; break; } a++; b++; }
    if (eq && *a == *b) return &catalog->arenas[i];
  }
  qa_range_error("No authored single-player arena for %s", map);
}
