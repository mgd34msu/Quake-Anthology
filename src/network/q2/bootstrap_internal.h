#ifndef QA_Q2_BOOTSTRAP_INTERNAL_H
#define QA_Q2_BOOTSTRAP_INTERNAL_H
#include "qa/network_q2_bootstrap.h"
#include "session_internal.h"
typedef struct qa_q2_pending_connect {
    qa_net_address from;
    qa_q2_connect_request request;
    uint64_t received_ns;
} qa_q2_pending_connect;
struct qa_network_q2_bootstrap {
    qa_network_runtime *runtime;
    qa_net_protocol_id protocols[8];
    size_t protocol_count;
    bool server, busy, canceled;
    union {
        struct {
            qa_q2_server_bootstrap_hooks hooks;
            qa_q2_challenges *challenges;
            qa_q2_pending_connect *pending;
            size_t pending_count, pending_capacity;
        } server;
        struct {
            qa_q2_client_bootstrap_hooks hooks;
            qa_q2_handshake handshake;
            qa_net_client_id id;
            uint64_t generation, started_ns, received_ns, timeout_ns;
            bool attached, started, received, notified;
        } client;
    } state;
};
bool q2_bootstrap_idle(qa_network_q2_bootstrap *, qa_error *);
bool q2_bootstrap_server_hooks_valid(const qa_q2_server_bootstrap_hooks *);
bool q2_bootstrap_client_hooks_valid(const qa_q2_client_bootstrap_hooks *);
#endif
