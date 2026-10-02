#ifndef QA_KEX_TRANSPORT_INTERNAL_H
#define QA_KEX_TRANSPORT_INTERNAL_H

#include "qa/network_kex_transport.h"
#include "qa/network_kex_save.h"
#include "kex_lan_internal.h"
#include "kex_discovery_owner_internal.h"

struct qa_kex_transport {
    qa_net_transport *raw, *dispatch;
    qa_kex_lan *lobby;
    qa_kex_mdns_owner *discovery;
    qa_kex_transport_hooks hooks;
    size_t raw_limit;
    bool raw_owned, published, entered;
};

bool qa_kex_transport_finish(qa_kex_transport *, qa_net_transport **, qa_error *);
void qa_kex_transport_destroy(qa_kex_transport *);
bool qa_kex_transport_valid(const qa_kex_transport *);

#endif
