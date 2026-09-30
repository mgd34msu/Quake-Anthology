#ifndef QA_NAVIGATION_GRAPH_SAVE_H
#define QA_NAVIGATION_GRAPH_SAVE_H

#include "qa/navigation.h"
#include "qa/session.h"

/* Immutable admitted topology, including source-conditioned origins and edges.
 * The caller qualifies the pinned map and optional actual navigation asset.
 * Restore rebuilds adjacency/estimate topology without trace/source callbacks.
 * Outputs must be empty; failure leaves them unchanged. */
bool qa_navigation_graph_save_capture(qa_session *, const qa_nav_graph *, qa_buffer *, qa_error *);
bool qa_navigation_graph_save_restore(qa_session *, qa_bytes, const qa_nav_map *,
                                      qa_nav_asset *, qa_nav_graph **, qa_error *);

#endif
