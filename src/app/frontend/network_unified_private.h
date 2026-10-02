#ifndef QA_FRONTEND_NETWORK_UNIFIED_PRIVATE_H
#define QA_FRONTEND_NETWORK_UNIFIED_PRIVATE_H

#include "network_unified.h"
#define UNIFIED_PEERS 264u

typedef struct unified_peer {
    qa_net_connect request;
    qa_net_seat_binding binding;
    qa_net_client_id client;
    qa_unified_session *session;
    application_unified_server *server;
    frontend_remote_unified *remote;
    qa_buffer source_import;
    bool occupied, staging, travel_prepared, frame_published;
} unified_peer;
struct frontend_network_unified {
    frontend_network_unified_options options;
    qa_unified_bootstrap *bootstrap;
    unified_peer peers[UNIFIED_PEERS];
    application_unified_source source, travel_source;
    qa_buffer bootstrap_import;
    qa_recipe_sidecar *restored_sidecars;
    size_t travel_cursor;
    uint32_t epoch;
    uint64_t frame_before;
    unsigned calls;
    bool closing, traveling, frame_boundary, restore_pending, imported;
};
bool frontend_network_unified_import_admit(frontend_network_unified *, const qa_net_connect *, qa_error *);
qa_unified_bootstrap_hooks frontend_network_unified_bootstrap_hooks(frontend_network_unified *);

#endif
