#include "qa/network_events.h"

#include <string.h>

typedef struct packet_header {
    uint64_t source_id;
    qa_net_address from;
    uint32_t source, route;
} packet_header;

bool qa_network_events_collect(const qa_network_event_source *source, qa_platform_events *events,
    uint64_t now_ns, uint32_t budget, qa_error *error)
{
    for (uint32_t i = 0; i < budget; ++i) {
        if (qa_platform_events_admit(events, QA_PLATFORM_EVENT_PACKET,
            sizeof(packet_header) + source->datagram_bytes) != QA_PLATFORM_EVENT_ACCEPTED) break;
        qa_net_transport_event input;
        if (!source->collect(source->context, now_ns, &input, error)) return false;
        const qa_net_datagram *packet=&input.packet;
        size_t size = packet->kind == QA_NET_POLL_PACKET ? packet->payload.size : 0;
        packet_header header={.source_id=source->id, .from=packet->from,
            .source=input.source, .route=input.route};
        (void)qa_platform_events_push(events, QA_PLATFORM_EVENT_PACKET,
            packet->kind==QA_NET_POLL_EMPTY ? now_ns : packet->received_ns,
            (int32_t)packet->kind, source->destination,
            (qa_bytes){(const uint8_t *)&header, sizeof(header)},
            (qa_bytes){packet->payload.data, size});
        if (packet->kind == QA_NET_POLL_EMPTY) break;
    }
    return true;
}

void qa_network_event_packet(const qa_platform_event *event, qa_bytes bytes,
    uint64_t *source_id, qa_net_transport_event *input)
{
    packet_header header;
    memcpy(&header, bytes.data, sizeof(header));
    *source_id=header.source_id;
    *input = (qa_net_transport_event){.packet={.kind = (qa_net_poll_kind)event->value,
        .from=header.from,
        .payload = {bytes.data + sizeof(header), bytes.size - sizeof(header)},
        .received_ns = event->time_ns}, .source=header.source, .route=header.route};
}
