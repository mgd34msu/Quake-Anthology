#ifndef QA_UNIFIED_BOOTSTRAP_INTERNAL_H
#define QA_UNIFIED_BOOTSTRAP_INTERNAL_H
#include "qa/network_unified_bootstrap.h"

typedef struct qa_unified_pending {
    qa_net_address address;
    qa_unified_token nonce, token;
    uint64_t created;
    bool connect;
} qa_unified_pending;
typedef struct qa_unified_peer_receipt {
    qa_net_address address;
    qa_unified_token nonce, token;
    qa_net_client_id id;
    qa_unified_session *session;
    bool flush;
} qa_unified_peer_receipt;
struct qa_unified_bootstrap {
    qa_network_runtime *runtime;
    qa_unified_bootstrap_options options;
    qa_unified_pending pending[256];
    qa_unified_peer_receipt peers[264];
    size_t pending_count, peer_count;
    qa_unified_token nonce, token;
    uint64_t now_ns, handshake_started, last_handshake;
    bool has_token, started, sent, attach_pending, closed, entered;
};
bool qa_unified_bootstrap_valid(const qa_unified_bootstrap *, bool bound, qa_error *);
#endif
