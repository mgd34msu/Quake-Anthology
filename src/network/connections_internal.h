#ifndef QA_NETWORK_CONNECTIONS_INTERNAL_H
#define QA_NETWORK_CONNECTIONS_INTERNAL_H
#include "qa/network.h"

bool qa_net_connections_restart_ready(qa_net_connections *,qa_net_client_id,
    const uint64_t *,qa_error *);
/* The exact admitted connection remains retained across the peer callback. */
void qa_net_connections_restart_commit(qa_net_connections *,qa_net_client_id,
    const uint64_t *);
#endif
