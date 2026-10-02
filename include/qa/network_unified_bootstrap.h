#ifndef QA_NETWORK_UNIFIED_BOOTSTRAP_H
#define QA_NETWORK_UNIFIED_BOOTSTRAP_H
#include "qa/network_unified_session.h"

typedef struct qa_unified_bootstrap qa_unified_bootstrap;
typedef struct qa_unified_bootstrap_hooks {
    void *context;
    /* Build the genuine one-seat request and Source callbacks, attach its
     * session, and queue the current real server offer before returning. */
    bool (*attach)(void *, qa_network_runtime *, const qa_net_address *,
        qa_unified_token, qa_unified_token nonce, bool server, uint64_t now_ns,
        qa_net_client_id *, qa_unified_session **borrowed_control, qa_error *);
} qa_unified_bootstrap_hooks;
typedef struct qa_unified_bootstrap_options {
    bool server;
    uint32_t max_clients;
    qa_net_address remote;
    qa_unified_bootstrap_hooks hooks;
} qa_unified_bootstrap_options;
bool qa_unified_bootstrap_create(qa_network_runtime *, const qa_unified_bootstrap_options *,
    qa_unified_bootstrap **, qa_error *);
void qa_unified_bootstrap_destroy(qa_unified_bootstrap *);
bool qa_unified_bootstrap_idle(const qa_unified_bootstrap *);
bool qa_unified_bootstrap_closed(const qa_unified_bootstrap *);
/* Called by the sole raw dispatcher. Never attaches a peer during pumping. */
bool qa_unified_bootstrap_receive(qa_unified_bootstrap *, const qa_net_datagram *,
    bool *recognized, qa_error *);
/* Runs actual pending attachment/retries outside runtime callbacks. */
bool qa_unified_bootstrap_process(qa_unified_bootstrap *, uint64_t now_ns,
    bool *waiting, qa_error *);
bool qa_unified_bootstrap_client(const qa_unified_bootstrap *, qa_net_client_id *,
    qa_unified_session **borrowed_control);
bool qa_unified_bootstrap_current_limits(qa_unified_bootstrap *, uint32_t max_clients, qa_error *);
bool qa_unified_bootstrap_domain(const qa_unified_bootstrap *, bool *server, uint32_t *max_clients,
    qa_net_address *remote, qa_error *);
bool qa_unified_bootstrap_checkpoint(const qa_unified_bootstrap *, qa_buffer *, qa_error *);
/* Restored receipt IDs are qualified against the real restored generic table
 * and actual session owner through resolve; no new peer is fabricated. */
typedef bool (*qa_unified_bootstrap_resolve)(void *, qa_net_client_id, qa_unified_session **, qa_error *);
bool qa_unified_bootstrap_restore(qa_bytes, qa_network_runtime *, uint64_t connection_owner,
    const qa_unified_bootstrap_hooks *, qa_unified_bootstrap_resolve, void *,
    qa_unified_bootstrap **, qa_error *);
#endif
