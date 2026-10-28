/* Port of src/bots/navigation/graph.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_GRAPH_H
#define QA_BOTS_NAVIGATION_GRAPH_H
#include <stdbool.h>
#include "qa/bots/navigation/aas.h"
#include "qa/bots/navigation/nav.h"
#include "qa/bots/navigation/types.h"
qa_travel_mode qa_aas_travel_mode(int64_t type);
qa_travel_mode qa_kex_travel_mode(int64_t type);
int64_t qa_aas_travel_flag(int64_t type);
int64_t qa_aas_area_travel_flags(int64_t contents, int64_t flags);
/* Clusters own their node arrays; caller frees each list's nodes + array. */
qa_navigation_cluster_list *qa_navigation_clusters(const qa_navigation_node *nodes, size_t node_count, const qa_navigation_edge *edges, size_t edge_count, size_t *out_count);
void qa_navigation_clusters_destroy(qa_navigation_cluster_list *clusters, size_t count);
/* Exactly one of aas/kex non-NULL. Graph arrays owned (free with
 * qa_navigation_graph_destroy); asset pointers borrowed. */
bool qa_navigation_from_asset(qa_navigation_graph *out, qa_navigation_map_identity map, const qa_aas_asset *aas, const qa_kex_navigation_asset *kex, qa_navigation_profile profile, const qa_navigation_world *world);
int64_t qa_navigation_edge_travel_flag(const qa_navigation_edge *edge);
#endif
