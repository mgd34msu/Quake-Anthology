#ifndef QA_NETWORK_IPX_NATIVE_PRIVATE_H
#define QA_NETWORK_IPX_NATIVE_PRIVATE_H

#include "qa/network.h"

bool qa_net_ipx_native_transport_open(const qa_net_address *, qa_net_limits,
    uint8_t packet_type, qa_net_transport **, qa_error *);

#endif
