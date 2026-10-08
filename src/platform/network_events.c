#include "qa/network_events.h"

#include <string.h>

bool qa_network_events_collect(qa_net_transport *transport, qa_platform_events *events,
    uint64_t now_ns, uint32_t budget, qa_error *error)
{
    uint8_t bytes[sizeof(qa_net_address) + UINT16_MAX];
    for (uint32_t i = 0; i < budget; ++i) {
        qa_net_datagram packet;
        if (!qa_net_transport_receive(transport, now_ns, &packet, error)) return false;
        if (packet.kind == QA_NET_POLL_EMPTY) break;
        size_t size = packet.kind == QA_NET_POLL_PACKET ? packet.payload.size : 0;
        memcpy(bytes, &packet.from, sizeof(packet.from));
        if (size) memcpy(bytes + sizeof(packet.from), packet.payload.data, size);
        qa_platform_events_push(events, QA_PLATFORM_EVENT_PACKET, packet.received_ns,
            (int32_t)packet.kind, 0, (qa_bytes){bytes, sizeof(packet.from) + size});
    }
    return true;
}

void qa_network_event_packet(const qa_platform_event *event, qa_bytes bytes, qa_net_datagram *packet)
{
    *packet = (qa_net_datagram){.kind = (qa_net_poll_kind)event->value,
        .payload = {bytes.data + sizeof(packet->from), bytes.size - sizeof(packet->from)},
        .received_ns = event->time_ns};
    memcpy(&packet->from, bytes.data, sizeof(packet->from));
}
