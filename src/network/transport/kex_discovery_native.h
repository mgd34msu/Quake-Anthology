#ifndef QA_KEX_DISCOVERY_NATIVE_H
#define QA_KEX_DISCOVERY_NATIVE_H

#include "qa/network_kex_discovery.h"

typedef struct qa_kex_mdns_socket qa_kex_mdns_socket;
bool qa_kex_mdns_native_open(qa_kex_mdns_socket **, qa_error *);
bool qa_kex_mdns_native_send(qa_kex_mdns_socket *, qa_bytes, qa_error *);
bool qa_kex_mdns_native_collect(qa_kex_mdns_socket *, uint64_t,
                                qa_net_transport_event *, qa_error *);
bool qa_kex_mdns_native_close(qa_kex_mdns_socket *, qa_error *);
bool qa_kex_mdns_native_hostname(char label[64], qa_error *);

#endif
