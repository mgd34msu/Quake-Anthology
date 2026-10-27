/* See header. GPL-2.0-or-later. */
#include "qa/bots/navigation/estimates.h"
#include "qa/bots/navigation/nav.h"
#include "qa/foundation/support.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int64_t node; double seconds; } cost_t;
typedef struct { cost_t *v; size_t n, cap; } cost_queue;
static bool queue_push(cost_queue *q, cost_t value) {
  if (q->n == q->cap) {
    cost_t *g = realloc(q->v, (q->cap ? q->cap * 2 : 16) * sizeof *g);
    if (!g) return false;
    q->v = g; q->cap = q->cap ? q->cap * 2 : 16;
  }
  size_t index = q->n; q->v[q->n++] = value;
  while (index > 0) {
    size_t parent = (index - 1) / 2;
    if (q->v[parent].seconds <= value.seconds) break;
    q->v[index] = q->v[parent]; index = parent;
  }
  q->v[index] = value; return true;
}
static bool queue_pop(cost_queue *q, cost_t *out) {
  if (q->n == 0) return false;
  cost_t first = q->v[0], last = q->v[--q->n];
  if (q->n == 0) { *out = first; return true; }
  size_t index = 0;
  while (index * 2 + 1 < q->n) {
    size_t child = index * 2 + 1;
    if (child + 1 < q->n && q->v[child + 1].seconds < q->v[child].seconds) child++;
    if (last.seconds <= q->v[child].seconds) break;
    q->v[index] = q->v[child]; index = child;
  }
  q->v[index] = last; *out = first; return true;
}
static int64_t node_index(const qa_navigation_estimates *e, int64_t id) {
  for (size_t i = 0; i < e->graph->node_count; i++) if (e->graph->nodes[i].id == id) return (int64_t)i;
  return -1;
}
bool qa_navigation_estimates_init(qa_navigation_estimates *e, const qa_navigation_graph *graph,
                                  void *ctx, qa_nav_estimate_eligibility allowed) {
  memset(e, 0, sizeof *e);
  e->graph = graph; e->ctx = ctx; e->allowed = allowed;
  size_t n = graph->node_count ? graph->node_count : 1;
  e->incoming = calloc(n, sizeof *e->incoming);
  e->incoming_counts = calloc(n, sizeof *e->incoming_counts);
  size_t *caps = calloc(n, sizeof *caps);
  if (!e->incoming || !e->incoming_counts || !caps) { free(caps); qa_navigation_estimates_destroy(e); return false; }
  for (size_t i = 0; i < graph->edge_count; i++) {
    const qa_navigation_edge *edge = &graph->edges[i];
    int64_t to = node_index(e, edge->to);
    if (to < 0) qa_range_error("Estimate edge has no destination node");
    if (!isfinite(edge->travel_seconds) || edge->travel_seconds < 0) qa_range_error("Invalid estimated edge duration");
    if (e->incoming_counts[to] == caps[to]) {
      const qa_navigation_edge **g = realloc((void *)e->incoming[to], (caps[to] ? caps[to] * 2 : 4) * sizeof *g);
      if (!g) { free(caps); qa_navigation_estimates_destroy(e); return false; }
      e->incoming[to] = g; caps[to] = caps[to] ? caps[to] * 2 : 4;
    }
    e->incoming[to][e->incoming_counts[to]++] = edge;
  }
  free(caps);
  return true;
}
static void free_caches(qa_navigation_estimates *e) {
  for (size_t i = 0; i < e->cache_count; i++) free(e->caches[i].tails);
  e->cache_count = 0;
}
void qa_navigation_estimates_destroy(qa_navigation_estimates *e) {
  if (!e) return;
  if (e->incoming) for (size_t i = 0; i < (e->graph ? e->graph->node_count : 0); i++) free((void *)e->incoming[i]);
  free((void *)e->incoming); free(e->incoming_counts);
  free_caches(e); free(e->caches);
  memset(e, 0, sizeof *e);
}
void qa_navigation_estimates_invalidate(qa_navigation_estimates *e) { free_caches(e); }
/* Returns cache entry index, or -1 on OOM. */
static int64_t tails_entry(qa_navigation_estimates *e, int64_t goal, bool has_flags, int64_t flags) {
  int64_t key_flags = has_flags ? (int64_t)(int32_t)flags : 0; /* TS key uses flags|0 */
  for (size_t i = 0; i < e->cache_count; i++) {
    qa_nav_estimate_cache_entry *c = &e->caches[i];
    if (c->goal == goal && c->has_flags == has_flags && (!has_flags || c->flags == key_flags)) {
      qa_nav_estimate_cache_entry hit = *c; /* LRU: move to newest */
      memmove(&e->caches[i], &e->caches[i + 1], (e->cache_count - i - 1) * sizeof *c);
      e->caches[e->cache_count - 1] = hit;
      return (int64_t)(e->cache_count - 1);
    }
  }
  size_t nodes = e->graph->node_count;
  qa_nav_estimate_tail *tails = calloc(nodes ? nodes : 1, sizeof *tails);
  if (!tails) return -1;
  int64_t goal_i = node_index(e, goal);
  tails[goal_i] = (qa_nav_estimate_tail){true, 0, NULL};
  cost_queue q = {0};
  if (!queue_push(&q, (cost_t){goal, 0})) { free(tails); return -1; }
  cost_t current;
  while (queue_pop(&q, &current)) {
    int64_t ci = node_index(e, current.node);
    if (!tails[ci].has || tails[ci].seconds != current.seconds) continue;
    for (size_t i = 0; i < e->incoming_counts[ci]; i++) {
      const qa_navigation_edge *edge = e->incoming[ci][i];
      if (!e->allowed(e->ctx, edge, has_flags, flags)) continue;
      double seconds = current.seconds + edge->travel_seconds;
      int64_t fi = node_index(e, edge->from);
      if (fi < 0) continue; /* TS keys tails by raw id; unknown from ids are unreachable in estimate() */
      if (seconds >= (tails[fi].has ? tails[fi].seconds : INFINITY)) continue;
      tails[fi] = (qa_nav_estimate_tail){true, seconds, edge};
      if (!queue_push(&q, (cost_t){edge->from, seconds})) { free(q.v); free(tails); return -1; }
    }
  }
  free(q.v);
  if (e->cache_count == e->cache_cap) {
    qa_nav_estimate_cache_entry *g = realloc(e->caches, (e->cache_cap ? e->cache_cap * 2 : 8) * sizeof *g);
    if (!g) { free(tails); return -1; }
    e->caches = g; e->cache_cap = e->cache_cap ? e->cache_cap * 2 : 8;
  }
  e->caches[e->cache_count++] = (qa_nav_estimate_cache_entry){goal, has_flags, key_flags, tails};
  if (e->cache_count > 128) {
    free(e->caches[0].tails);
    memmove(&e->caches[0], &e->caches[1], (e->cache_count - 1) * sizeof *e->caches);
    e->cache_count--;
  }
  return (int64_t)(e->cache_count - 1);
}
qa_navigation_estimate_result qa_navigation_estimates_estimate(qa_navigation_estimates *e,
                                                               qa_navigation_estimate_query query) {
  qa_navigation_estimate_result out; memset(&out, 0, sizeof out);
  int64_t start_i = node_index(e, query.start_node), goal_i = node_index(e, query.goal_node);
  if (start_i < 0 || goal_i < 0) return out;
  if (query.has_travel_flags && (query.travel_flags < -0x80000000LL || query.travel_flags > 0xffffffffLL))
    qa_range_error("Estimate travel flags must be a 32-bit mask");
  if (query.has_origin && (!isfinite((float)query.origin.x) || !isfinite((float)query.origin.y)
                           || !isfinite((float)query.origin.z)))
    qa_range_error("Estimate origin must contain finite float32 coordinates");
  /* AAS delegation (estimate-aas.ts) not yet ported; generic path below. */
  if (query.start_node == query.goal_node) { out.is_estimate = true; out.travel_time = 1; return out; }
  int64_t entry = tails_entry(e, query.goal_node, query.has_travel_flags, query.travel_flags);
  if (entry < 0) return out;
  qa_nav_estimate_tail tail = e->caches[entry].tails[start_i];
  if (!tail.has) return out;
  double seconds = tail.seconds;
  if (query.has_origin && tail.first_edge) {
    qa_vec3 start = tail.first_edge->start, origin = query.origin;
    double heuristic = e->graph->kex_asset ? e->graph->kex_asset->heuristic : 1;
    seconds += hypot(hypot(start.x - origin.x, start.y - origin.y), start.z - origin.z) * heuristic / 320;
  }
  double hundredths = trunc((float)(seconds * 100));
  out.is_estimate = true;
  out.travel_time = hundredths > 1 ? hundredths : 1;
  if (query.has_origin && tail.first_edge) { out.has_first_edge = true; out.first_edge = *tail.first_edge; }
  return out;
}
