/* Port of src/bots/navigation/estimates.ts. Generic Dijkstra tails with
 * a 128-entry LRU cache; AAS graphs delegate to estimate_aas.
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_ESTIMATES_H
#define QA_BOTS_NAVIGATION_ESTIMATES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/bots/navigation/types.h"
/* flags: TS `undefined` is has_flags false. */
typedef bool (*qa_nav_estimate_eligibility)(void *ctx, const qa_navigation_edge *edge, bool has_flags, int64_t flags);
typedef struct qa_nav_estimate_tail { bool has; double seconds; const qa_navigation_edge *first_edge; } qa_nav_estimate_tail;
typedef struct qa_nav_estimate_cache_entry {
  int64_t goal; bool has_flags; int64_t flags; /* has_flags: TS does flags|0 */
  qa_nav_estimate_tail *tails; /* per graph node index */
} qa_nav_estimate_cache_entry;
struct qa_aas_navigation_estimates;
typedef struct qa_navigation_estimates {
  struct qa_aas_navigation_estimates *aas; /* owned, NULL unless AAS graph */
  const qa_navigation_graph *graph; /* borrowed */
  void *ctx; qa_nav_estimate_eligibility allowed;
  const qa_navigation_edge ***incoming; size_t *incoming_counts; /* per node index */
  qa_nav_estimate_cache_entry *caches; size_t cache_count, cache_cap; /* LRU order, oldest first */
} qa_navigation_estimates;
/* False only on OOM; invalid edges abort with the TS RangeError text. */
bool qa_navigation_estimates_init(qa_navigation_estimates *e, const qa_navigation_graph *graph,
                                  void *ctx, qa_nav_estimate_eligibility allowed);
void qa_navigation_estimates_destroy(qa_navigation_estimates *e);
void qa_navigation_estimates_invalidate(qa_navigation_estimates *e);
qa_navigation_estimate_result qa_navigation_estimates_estimate(qa_navigation_estimates *e,
                                                               qa_navigation_estimate_query query);
#endif
