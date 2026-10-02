#ifndef QA_NETWORK_Q2_BOOTSTRAP_H
#define QA_NETWORK_Q2_BOOTSTRAP_H
#include "qa/network_q2_session.h"

typedef struct qa_network_q2_bootstrap qa_network_q2_bootstrap;
/* A prepared claim belongs to the actual Source/canonical admission owner.
 * The connection's ordered bindings remain borrowed through commit or abort. */
typedef struct qa_q2_server_admission {
    qa_net_connect connection;
    qa_network_q2_server_policy policy;
    qa_network_q2_server_hooks hooks;
    void *source_claim;
} qa_q2_server_admission;
typedef struct qa_q2_client_admission {
    qa_net_connect connection;
    qa_network_q2_client_policy policy;
    qa_network_q2_client_hooks hooks;
    void *source_claim;
} qa_q2_client_admission;
typedef struct qa_q2_discovery {
    qa_q2_status status;
    const char *name, *map;
    uint32_t players, maximum;
} qa_q2_discovery;
typedef struct qa_q2_client_identity {
    char userinfo[8193], social_ids[QA_Q2_MAX_SEATS][512];
    size_t social_count;
} qa_q2_client_identity;
typedef struct qa_q2_server_bootstrap_hooks {
    void *context;
    bool (*enabled)(void *, bool *, qa_error *);
    bool (*rejects)(void *, const qa_net_address *, bool *, qa_error *);
    /* Required for native KEX offers. The actual LAN admission proves this;
     * a qport or endpoint alone cannot admit a lobby player. */
    bool (*transport_admitted)(void *, const qa_net_address *, bool *, qa_error *);
    bool (*prepare)(void *, const qa_net_address *, const qa_q2_connect_request *,
        qa_q2_server_admission *, bool *allowed, char reason[1024], qa_error *);
    bool (*committed)(void *, const qa_q2_server_admission *, qa_net_client_id, qa_error *);
    bool (*abort)(void *, const qa_q2_server_admission *, qa_error *);
    bool (*discovery)(void *, qa_q2_discovery *, qa_error *);
    /* Validated URL from the actual Source sv_downloadserver policy. */
    bool (*download_server)(void *, const char **, qa_error *);
} qa_q2_server_bootstrap_hooks;
typedef struct qa_q2_server_bootstrap_options {
    const qa_net_protocol_id *protocols;
    size_t protocol_count, challenge_capacity, pending_capacity;
    qa_q2_random_fn random;
    void *random_context;
    qa_q2_server_bootstrap_hooks hooks;
} qa_q2_server_bootstrap_options;
typedef struct qa_q2_client_bootstrap_hooks {
    void *context;
    bool (*identity)(void *, qa_q2_client_identity *, qa_error *);
    /* KEX uses the actual retained LAN join. Other dialects are ready. */
    bool (*transport_ready)(void *, bool *, qa_error *);
    /* WAITING resumes this actual retained constructor generation. READY
     * returns its real CLIENT Source and canonical connection claim. */
    bool (*prepare)(void *, const qa_net_address *, const qa_q2_connect_request *,
        const char *download_server, uint64_t generation, qa_q2_client_admission *,
        qa_q2_preparation *, qa_error *);
    bool (*committed)(void *, const qa_q2_client_admission *, qa_net_client_id, qa_error *);
    bool (*abort)(void *, const qa_q2_client_admission *, qa_error *);
    bool (*cancel)(void *, uint64_t generation, qa_error *);
    bool (*print)(void *, const char *, qa_error *);
    bool (*failed)(void *, const char *, qa_error *);
} qa_q2_client_bootstrap_hooks;
typedef struct qa_q2_client_bootstrap_options {
    qa_net_address remote;
    const qa_net_protocol_id *protocols;
    size_t protocol_count, payload_bytes;
    uint16_t qport;
    uint64_t timeout_ns;
    qa_q2_client_bootstrap_hooks hooks;
} qa_q2_client_bootstrap_options;

/* Borrows the sole existing runtime and its transport. No second socket or
 * poll owner is created. Source claim callbacks run only in continue/tick at
 * the enclosing idle point, never inside connectionless reception. */
bool qa_network_q2_bootstrap_server(qa_network_runtime *, const qa_q2_server_bootstrap_options *,
    qa_network_q2_bootstrap **, qa_error *);
bool qa_network_q2_bootstrap_client(qa_network_runtime *, const qa_q2_client_bootstrap_options *,
    qa_network_q2_bootstrap **, qa_error *);
void qa_network_q2_bootstrap_destroy(qa_network_q2_bootstrap *);
bool qa_network_q2_bootstrap_receive(qa_network_q2_bootstrap *, const qa_net_datagram *,
    bool *recognized, qa_error *);
bool qa_network_q2_bootstrap_continue(qa_network_q2_bootstrap *, uint64_t now_ns, qa_error *);
bool qa_network_q2_bootstrap_tick(qa_network_q2_bootstrap *, uint64_t now_ns, qa_error *);
bool qa_network_q2_bootstrap_cancel(qa_network_q2_bootstrap *, qa_error *);
/* Called by the sole canonical retirement owner after this actual client was
 * detached. It clears the retained committed ID without reentering Source. */
bool qa_network_q2_bootstrap_disconnected(qa_network_q2_bootstrap *, qa_net_client_id, qa_error *);
bool qa_network_q2_bootstrap_client_id(const qa_network_q2_bootstrap *, qa_net_client_id *, bool *present, qa_error *);
/* Shared binary master/browser and rcon/admin services remain in the sole
 * enclosing connectionless dispatcher when recognized is false. */

#endif
