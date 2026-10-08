#ifndef QA_NETWORK_EVENTS_H
#define QA_NETWORK_EVENTS_H

#include "qa/network.h"
#include "qa/platform_events.h"

/* Copies transport-owned address and payload storage into the common queue.
 * The caller supplies the shared host time and its bounded receive budget. */
bool qa_network_events_collect(qa_net_transport *, qa_platform_events *,
    uint64_t now_ns, uint32_t budget, qa_error *);
/* Borrows one collector-produced event until the common queue consumes it. */
void qa_network_event_packet(const qa_platform_event *, qa_bytes, qa_net_datagram *);

#endif
