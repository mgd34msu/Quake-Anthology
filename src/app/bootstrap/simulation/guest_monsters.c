/* Port of src/app/bootstrap/simulation/guest-monsters.ts (filterEntityText/entitySpans;
 * planGuestMonsters needs unlanded monster-runtime ordinary sets + Q2 field
 * parsing of the selection, carried in monster_runtime.h). GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/guest_monsters.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
typedef struct span { size_t a, b; } span;
static span *entity_spans(const char *text, size_t *out_n) {
  size_t cap = 8, n = 0; span *sp = malloc(cap * sizeof *sp);
  if (sp == NULL) qa_range_error("Out of memory");
  long depth = 0; long start = -1; bool quoted = false;
  size_t len = strlen(text);
  for (size_t i = 0; i < len; i++) {
    char ch = text[i];
    if (quoted) { if (ch == '"' && (i == 0 || text[i - 1] != '\\')) quoted = false; continue; }
    if (ch == '"') quoted = true;
    else if (ch == '{') { if (depth == 0) start = (long)i; depth++; }
    else if (ch == '}') { depth--; if (depth == 0 && start >= 0) {
      if (n == cap) { cap *= 2; sp = realloc(sp, cap * sizeof *sp); if (sp == NULL) qa_range_error("Out of memory"); }
      sp[n].a = (size_t)start; sp[n].b = i + 1; n++; start = -1; } }
  }
  *out_n = n; return sp;
}
char *qa_app_sim_filter_entity_text(const char *text, const double *keep, size_t keep_count) {
  size_t n = 0; span *sp = entity_spans(text, &n);
  size_t total = 0;
  for (size_t i = 0; i < n; i++) {
    bool k = i == 0; for (size_t j = 0; j < keep_count; j++) if (keep[j] == (double)i) k = true;
    if (k) total += (sp[i].b - sp[i].a) + 1;
  }
  char *out = malloc(total + 1); if (out == NULL) qa_range_error("Out of memory");
  size_t o = 0;
  for (size_t i = 0; i < n; i++) {
    bool k = i == 0; for (size_t j = 0; j < keep_count; j++) if (keep[j] == (double)i) k = true;
    if (k) { memcpy(out + o, text + sp[i].a, sp[i].b - sp[i].a); o += sp[i].b - sp[i].a; out[o++] = '\n'; }
  }
  out[o] = '\0'; free(sp); return out;
}
void qa_app_sim_guest_monster_plan_destroy(qa_app_sim_guest_monster_plan *plan) {
  free(plan->entities); free(plan->replacements); free(plan->kept);
  plan->entities = NULL; plan->replacements = NULL; plan->kept = NULL; plan->replacement_count = 0; plan->kept_count = 0;
}
