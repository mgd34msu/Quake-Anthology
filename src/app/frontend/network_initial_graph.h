#ifndef QA_FRONTEND_NETWORK_INITIAL_GRAPH_H
#define QA_FRONTEND_NETWORK_INITIAL_GRAPH_H
#include "network_restore_attempt.h"

typedef struct frontend_network_initial_graph_view {
    const void *network;
    frontend_network_client_attempt attempt;
    frontend_remote_q3_initial *parent;
    frontend_remote_q3_modules *modules;
    bool present, restore_candidate;
} frontend_network_initial_graph_view;
/* A retained partial parent is present. Its actual resource and module owners
 * separately prove completion; this observer does not grant Init readiness. */
bool frontend_network_initial_graph_read(const qa_frontend *,
    frontend_network_initial_graph_view *, qa_error *);
bool frontend_network_initial_graph_current(const qa_frontend *,
    const frontend_network_initial_graph_view *);
#endif
