#ifndef QA_FRONTEND_NETWORK_KEX_PRIVATE_H
#define QA_FRONTEND_NETWORK_KEX_PRIVATE_H

#include "network_kex.h"

typedef struct frontend_kex_query {
    qa_net_address address;
    uint64_t sent_ns;
} frontend_kex_query;

struct frontend_kex_browser {
    qa_server_browser *browser;
    frontend_kex_browser_hooks hooks;
    qa_kex_mdns_owner *discovery;
    frontend_kex_query queries[256];
    size_t query_count;
    bool entered;
};

qa_kex_mdns_hooks frontend_kex_browser_mdns_hooks(frontend_kex_browser *);

#endif
