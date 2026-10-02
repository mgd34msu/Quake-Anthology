#ifndef QA_KEX_DISCOVERY_OWNER_INTERNAL_H
#define QA_KEX_DISCOVERY_OWNER_INTERNAL_H

#include "qa/network_kex_discovery.h"
#include "kex_discovery_native.h"

struct qa_kex_mdns_owner {
    qa_kex_mdns_socket *socket;
    uint16_t advertised_port;
    qa_kex_mdns_hooks hooks;
    qa_kex_mdns_endpoint endpoints[256];
    qa_kex_mdns_address addresses[256];
    size_t endpoint_count, address_count;
    bool entered, closed, published, announce_pending, bound_published;
};

bool qa_kex_mdns_owner_valid(const qa_kex_mdns_owner *);
bool qa_kex_mdns_text_equal(qa_buffer, qa_buffer);
void qa_kex_mdns_owner_release_records(qa_kex_mdns_owner *);

#endif
