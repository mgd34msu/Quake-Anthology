/* Port of src/persistence/q1-selection.ts. GPL-2.0-or-later. */
#include "qa/persistence/q1_selection.h"

#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static noreturn void qa_error(const char *message) {
  fprintf(stderr, "Error: %s\n", message);
  abort();
}

typedef struct view {
  const char *text;
  size_t length;
} view;

static char lower_char(char c) {
  return c >= 'A' && c <= 'Z' ? (char)(c + ('a' - 'A')) : c;
}
static bool view_eq_lower(view left, const char *right, size_t right_length) {
  if (left.length != right_length) return false;
  for (size_t i = 0; i < left.length; i++)
    if (lower_char(left.text[i]) != lower_char(right[i])) return false;
  return true;
}
static bool str_eq(const char *left, const char *right) {
  return left != NULL && right != NULL && strcmp(left, right) == 0;
}
static bool str_eq_view(const char *left, view right) {
  return left != NULL && strlen(left) == right.length &&
         memcmp(left, right.text, right.length) == 0;
}

/* node:path posix basename() as a view into `path`. */
static view basename_view(const char *path) {
  if (path == NULL) return (view){"", 0};
  size_t end = strlen(path);
  while (end > 0 && path[end - 1] == '/') end--;
  size_t start = end;
  while (start > 0 && path[start - 1] != '/') start--;
  return (view){path + start, end - start};
}
/* node:path posix basename(dirname(path)) as a view; "." when the
 * TS result is "." (no directory component). */
static view directory_label(const char *path) {
  static const char dot[] = ".";
  if (path == NULL || path[0] == '\0') return (view){dot, 1};
  size_t end = strlen(path);
  while (end > 0 && path[end - 1] == '/') end--;
  if (end == 0) return (view){path, 0}; /* basename("/") is "" */
  size_t slash = end;
  bool found = false;
  for (size_t i = end; i > 0; i--) {
    if (path[i - 1] == '/') {
      slash = i - 1;
      found = true;
      break;
    }
  }
  if (!found) return (view){dot, 1};
  size_t dir_end = slash;
  while (dir_end > 1 && path[dir_end - 1] == '/') dir_end--;
  if (dir_end == 0) return (view){path, 0};
  if (dir_end == 1 && path[0] == '/') return (view){path, 0};
  size_t start = dir_end;
  while (start > 0 && path[start - 1] != '/') start--;
  return (view){path + start, dir_end - start};
}

typedef struct selection {
  const qa_installed_catalog *catalog;
  const qa_q1_save_data *save;
  bool has_game;
  view game; /* lower-cased on compare */
} selection;

static bool map_matches(const selection *sel, const char *map_path) {
  const char *map = sel->save->map != NULL ? sel->save->map : "";
  size_t map_length = strlen(map);
  if (map_path == NULL || strlen(map_path) != 5 + map_length + 4) return false;
  return view_eq_lower((view){map_path, 5}, "maps/", 5) &&
         view_eq_lower((view){map_path + 5, map_length}, map, map_length) &&
         view_eq_lower((view){map_path + 5 + map_length, 4}, ".bsp", 4);
}

static bool is_candidate(const selection *sel, const qa_catalog_product *product) {
  if (!str_eq(product->expectation.family, "q1")) return false;
  if (!str_eq(product->availability.kind, "installed")) return false;
  bool any_map = false;
  for (size_t i = 0; i < product->map_count; i++)
    if (map_matches(sel, product->maps[i].path)) any_map = true;
  if (!any_map) return false;
  if (sel->has_game) {
    view base = basename_view(product->expectation.content_directory);
    if (!view_eq_lower(base, sel->game.text, sel->game.length)) return false;
  }
  return true;
}

static bool valid_directory_segment(view segment) {
  if (segment.length == 0) return false;
  for (size_t i = 0; i < segment.length; i++) {
    char c = segment.text[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

const qa_catalog_product *qa_select_q1_save_product(
    const qa_installed_catalog *catalog, const qa_q1_save_data *save,
    const char *path, const char *selected) {
  view directory = directory_label(path);
  selection sel = {catalog, save, false, {NULL, 0}};
  if (save->format_version == 6) {
    const char *directories =
        save->game_directories != NULL ? save->game_directories : "";
    bool invalid = false;
    bool any = false;
    size_t start = 0;
    for (size_t i = 0;; i++) {
      if (directories[i] == ';' || directories[i] == '\0') {
        view segment = {directories + start, i - start};
        if (segment.length > 0) { /* split(";").filter(Boolean) */
          if (!valid_directory_segment(segment)) invalid = true;
          sel.game = segment;
          any = true;
        }
        start = i + 1;
        if (directories[i] == '\0') break;
      }
    }
    if (!any || invalid) qa_error("Invalid source save game directories.");
    sel.has_game = true;
  }
  if (selected != NULL) {
    for (size_t i = 0; i < catalog->product_count; i++) {
      const qa_catalog_product *product = &catalog->products[i];
      if (is_candidate(&sel, product) &&
          (str_eq(product->expectation.id, selected) ||
           str_eq(product->id, selected)))
        return product;
    }
    qa_error("Selected source game does not match this save.");
  }
  const qa_catalog_product *contextual = NULL;
  size_t contextual_count = 0;
  const qa_catalog_product *only = NULL;
  size_t candidate_count = 0;
  const qa_catalog_product *rerelease = NULL;
  size_t rerelease_count = 0;
  for (size_t i = 0; i < catalog->product_count; i++) {
    const qa_catalog_product *product = &catalog->products[i];
    if (!is_candidate(&sel, product)) continue;
    candidate_count++;
    only = product;
    if (str_eq_view(product->expectation.id, directory)) {
      contextual_count++;
      contextual = product;
    }
    if (str_eq(product->expectation.edition, "rerelease")) {
      rerelease_count++;
      rerelease = product;
    }
  }
  if (contextual_count == 1) return contextual;
  if (candidate_count == 1) return only;
  /* Version 6 is the KEX-lineage header; when editions still tie,
   * the rerelease product is the save's native home. */
  if (save->format_version == 6 && rerelease_count == 1) return rerelease;
  qa_error(candidate_count == 0
               ? "Required source save game content is not installed."
               : "Select the source game for this save; its original format "
                 "does not identify the program.");
}
