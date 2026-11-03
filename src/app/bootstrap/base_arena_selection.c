/* Port of src/app/bootstrap/base-arena-selection.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/base_arena_selection.h"
#include "qa/network/common/js_number.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
static char *dup(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
static int by_selection(const void *a, const void *b) {
  double x = (*(const qa_app_base_arena *const *)a)->selection, y = (*(const qa_app_base_arena *const *)b)->selection;
  return x < y ? -1 : x > y ? 1 : 0;
}
qa_app_arena_selection qa_app_arena_selection_make(const qa_app_base_arena_catalog *catalog, qa_app_base_arena_progression *progress, const char *saved_selection) {
  saved_selection = saved_selection ? saved_selection : "";
  const qa_app_base_arena **sorted = malloc((catalog->arena_count ? catalog->arena_count : 1) * sizeof *sorted);
  size_t sorted_count = 0;
  for (size_t i = 0; i < catalog->arena_count; i++)
    if (catalog->arenas[i].special[0] != '\0' || catalog->arenas[i].number < catalog->regular_count) sorted[sorted_count++] = &catalog->arenas[i];
  qa_js_sort(sorted, sorted_count, sizeof *sorted, by_selection);
  qa_app_arena_selection out = {0};
  for (size_t i = 0; i < sorted_count; i++) {
    const qa_app_base_arena *arena = sorted[i];
    char special[256]; size_t li = 0;
    for (; arena->special[li] && li < sizeof special - 1; li++) special[li] = (char)tolower((unsigned char)arena->special[li]);
    special[li] = '\0';
    char tier[64];
    if (strcmp(special, "training") == 0) snprintf(tier, sizeof tier, "training");
    else if (strcmp(special, "final") == 0) snprintf(tier, sizeof tier, "final");
    else snprintf(tier, sizeof tier, "%.0f", trunc(arena->number / 4) + 1);
    bool known = false;
    for (size_t t = 0; t < out.tier_count; t++) if (strcmp(out.tiers[t].id, tier) == 0) known = true;
    if (!known) {
      char label[80];
      if (strcmp(tier, "training") == 0) snprintf(label, sizeof label, "Training");
      else if (strcmp(tier, "final") == 0) snprintf(label, sizeof label, "Final arena");
      else snprintf(label, sizeof label, "Tier %s", tier);
      out.tiers = realloc(out.tiers, (out.tier_count + 1) * sizeof *out.tiers);
      out.tiers[out.tier_count++] = (qa_app_arena_tier){dup(tier), dup(label)};
    }
    qa_app_arena_best best = qa_app_base_arena_progression_best(progress, arena->number);
    char record[128];
    if (best.rank == 0) snprintf(record, sizeof record, "Not completed");
    else snprintf(record, sizeof record, "Rank %s · Skill %s", QA_JS_NUMBER(best.rank), QA_JS_NUMBER(best.skill));
    out.rows = realloc(out.rows, (out.row_count + 1) * sizeof *out.rows);
    out.rows[out.row_count++] = (qa_app_arena_selection_row){arena, dup(tier), dup(record),
        qa_app_base_arena_progression_level_available(progress, arena->number)};
  }
  /* current: saved selection (trimmed, Number()) else currentLevel map else first available. */
  bool blank = true;
  for (const char *p = saved_selection; *p; p++) if (!isspace((unsigned char)*p)) blank = false;
  const char *current = NULL;
  if (!blank) {
    double wanted = qa_js_string_to_number(saved_selection); /* Number(savedSelection) */
    for (size_t i = 0; i < out.row_count; i++)
      if (out.rows[i].available && out.rows[i].arena->selection == wanted) { current = out.rows[i].arena->map; break; }
  }
  if (!current) {
    double level = qa_app_base_arena_progression_current_level(progress);
    for (size_t i = 0; i < catalog->arena_count; i++)
      if (catalog->arenas[i].number == level) { current = catalog->arenas[i].map; break; }
  }
  if (!current)
    for (size_t i = 0; i < out.row_count; i++) if (out.rows[i].available) { current = out.rows[i].arena->map; break; }
  out.current = current;
  free(sorted);
  return out;
}
qa_app_arena_selection qa_app_read_arena_selection(qa_app_arena_selection_host host, qa_app_base_arena_catalog *out_catalog, qa_app_base_arena_progression **out_progression) {
  qa_app_base_arena_catalog catalog = {0};
  qa_app_base_arena_progression *progress = NULL;
  char *saved = NULL;
  const char *error = host.load(host.ctx, &catalog, &progress, &saved);
  if (error) qa_range_error("%s", error);
  qa_app_arena_selection selection = qa_app_arena_selection_make(&catalog, progress, saved ? saved : "");
  free(saved);
  *out_catalog = catalog;
  *out_progression = progress;
  return selection;
}
void qa_app_arena_selection_destroy(qa_app_arena_selection *selection) {
  if (!selection) return;
  for (size_t i = 0; i < selection->tier_count; i++) { free(selection->tiers[i].id); free(selection->tiers[i].label); }
  for (size_t i = 0; i < selection->row_count; i++) { free(selection->rows[i].tier); free(selection->rows[i].record); }
  free(selection->tiers);
  free(selection->rows);
  *selection = (qa_app_arena_selection){0};
}
