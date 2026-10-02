#ifndef QA_FRONTEND_NETWORK_UNIFIED_H
#define QA_FRONTEND_NETWORK_UNIFIED_H

#include "remote_unified.h"
#include "../application/network_unified.h"
#include "../application/unified_output_capture.h"
#include "qa/network_unified_bootstrap.h"

typedef struct frontend_network_unified frontend_network_unified;
typedef struct frontend_network_unified_options {
    qa_frontend *frontend;
    qa_network_runtime *runtime;
    bool server;
    uint64_t seat_owner;
    uint32_t remote_seat_base;
    qa_net_address remote;
    void *context;
    bool (*current)(void *, const frontend_network_unified *);
    /* These are the actual CLIENT factory's owners and callbacks. The remote
     * bridge consumes them only after the genuine challenge is received. */
    frontend_remote_unified_options client;
    const qa_recipe_sidecar *sidecars;
    size_t sidecar_count;
} frontend_network_unified_options;

bool frontend_network_unified_create(const frontend_network_unified_options *,
    frontend_network_unified **, qa_error *);
bool frontend_network_unified_receive(frontend_network_unified *,
    const qa_net_datagram *, bool *recognized, qa_error *);
/* Recognizes only the complete request currently staged by the actual
 * bootstrap attachment. No Source player is admitted at this boundary. */
bool frontend_network_unified_admit(frontend_network_unified *,
    const qa_net_connect *, bool *recognized, qa_error *);
bool frontend_network_unified_tick(frontend_network_unified *, uint64_t,
    bool *waiting, qa_error *);
bool frontend_network_unified_pre_frame(frontend_network_unified *, qa_error *);
bool frontend_network_unified_publish(frontend_network_unified *,
    const application_unified_output_external *, qa_error *);
bool frontend_network_unified_travel(frontend_network_unified *,
    const qa_recipe_sidecar *, size_t, qa_error *);
bool frontend_network_unified_client(const frontend_network_unified *,
    qa_net_client_id *, frontend_remote_unified **);
bool frontend_network_unified_close(frontend_network_unified *, qa_net_client_id,
    const char *reason, qa_error *);
bool frontend_network_unified_idle(const frontend_network_unified *);
/* Detaches the actual lower peers before releasing their callback owners.
 * Actual retired Source custody uses the quiet bridge retirement witnesses. */
bool frontend_network_unified_destroy(frontend_network_unified **, qa_error *);

#endif
