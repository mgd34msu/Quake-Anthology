/* Port of src/app/bootstrap/base-arena-selection.ts. The async
 * readArenaSelection profile load becomes host callbacks returning the
 * catalog/progress/saved selection. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_BASE_ARENA_SELECTION_H
#define QA_APP_BOOTSTRAP_BASE_ARENA_SELECTION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/base_arena_catalog.h"
#include "qa/app/bootstrap/base_arena_progression.h"
/* Owned strings; arena borrowed from the catalog. */
typedef struct qa_app_arena_selection_row {
  const qa_app_base_arena *arena;
  char *tier, *record;
  bool available;
} qa_app_arena_selection_row;
typedef struct qa_app_arena_tier { char *id, *label; } qa_app_arena_tier;
typedef struct qa_app_arena_selection {
  qa_app_arena_tier *tiers; size_t tier_count;
  qa_app_arena_selection_row *rows; size_t row_count;
  const char *current; /* borrowed arena map, NULL = TS null */
} qa_app_arena_selection;
typedef struct qa_app_arena_selection_host {
  void *ctx;
  /* Fill catalog + progression + saved ui_spSelection; return error or NULL. */
  const char *(*load)(void *ctx, qa_app_base_arena_catalog *catalog, qa_app_base_arena_progression **progression, char **saved_selection);
} qa_app_arena_selection_host;
/* saved_selection NULL mirrors the TS default "". */
qa_app_arena_selection qa_app_arena_selection_make(const qa_app_base_arena_catalog *catalog, qa_app_base_arena_progression *progress, const char *saved_selection);
qa_app_arena_selection qa_app_read_arena_selection(qa_app_arena_selection_host host, qa_app_base_arena_catalog *out_catalog, qa_app_base_arena_progression **out_progression);
void qa_app_arena_selection_destroy(qa_app_arena_selection *selection);
#endif
