/* See header. GPL-2.0-or-later. */
#include "qa/bots/navigation/types.h"
#include <stdlib.h>
#include <string.h>
static const char *const NAMES[QA_TM_COUNT] = {
    "walk",    "crouch",      "jump",      "drop",        "swim",        "water-jump",
    "ladder",  "teleport",    "mover",     "jump-pad",    "rocket-jump", "bfg-jump",
    "grapple", "double-jump", "ramp-jump", "strafe-jump", "unknown"};
const char *qa_travel_mode_name(qa_travel_mode m) {
  return (m >= 0 && m < QA_TM_COUNT) ? NAMES[m] : "unknown";
}
bool qa_travel_mode_from_name(const char *n, qa_travel_mode *o) {
  if (!n)
    return false;
  for (int i = 0; i < QA_TM_COUNT; i++)
    if (!strcmp(n, NAMES[i])) {
      *o = (qa_travel_mode)i;
      return true;
    }
  return false;
}
void qa_navigation_graph_destroy(qa_navigation_graph *g) {
  if (!g)
    return;
  /* graph.c AAS edges own their raw [face,edge] pairs; other sources borrow. */
  if (g->aas_asset)
    for (size_t i = 0; i < g->edge_count; i++) free((void *)g->edges[i].entity.raw);
  free(g->nodes);
  free(g->edges);
  for (size_t i = 0; i < g->cluster_count; i++) free((void *)g->clusters[i].nodes);
  free(g->clusters);
  for (size_t i = 0; i < g->rejected_count; i++) free((void *)g->rejected[i].reason);
  free(g->rejected);
  memset(g, 0, sizeof *g);
}
void qa_traversal_admission_destroy(qa_traversal_admission *a) {
  if (!a)
    return;
  free(a->trajectory);
  a->trajectory = NULL;
  a->trajectory_count = 0;
}
void qa_navigation_route_destroy(qa_navigation_route *r) {
  if (!r)
    return;
  free(r->nodes);
  free(r->edges);
  free(r->points);
  memset(r, 0, sizeof *r);
}
