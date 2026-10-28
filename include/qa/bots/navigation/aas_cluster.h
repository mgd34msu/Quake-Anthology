/* Port of src/bots/navigation/aas-cluster.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_AAS_CLUSTER_H
#define QA_BOTS_NAVIGATION_AAS_CLUSTER_H
#include <stdbool.h>
#include "qa/bots/navigation/aas.h"
typedef void (*qa_aas_cluster_print_fn)(void *ctx, const char *text);
/* Frees only the settings/portals/portal-index/cluster replacements. */
void qa_aas_clustering_result_destroy(qa_aas_asset *asset);
/* TS clusterAas: *out is a shallow copy of asset with lumps emptied and
 * owned settings/portals/portalIndexes/clusters slices. print NULL is
 * the TS default noop. False only on OOM. */
bool qa_cluster_aas(qa_aas_asset *out, const qa_aas_asset *asset, void *print_ctx, qa_aas_cluster_print_fn print);
#endif
