#ifndef QA_NETWORK_EVENTS_H
#define QA_NETWORK_EVENTS_H

#include "qa/network.h"
#include "qa/platform_events.h"

typedef struct qa_network_event_source {
    uint64_t id;
    int32_t destination;
    void *context;
    bool (*collect)(void *, uint64_t now_ns, qa_net_collect_policy, qa_net_transport_event *, qa_error *);
} qa_network_event_source;

/* Copies allowed input into the common queue. The empty boundary also queues
 * continuation work; dispatch never polls the source again. */
bool qa_network_events_collect(const qa_network_event_source *, qa_platform_events *,
    uint64_t now_ns, qa_net_collect_policy, uint32_t budget, qa_error *);
void qa_network_event_packet(const qa_platform_event *, qa_bytes, uint64_t *source_id,
    qa_net_transport_event *);

#endif
