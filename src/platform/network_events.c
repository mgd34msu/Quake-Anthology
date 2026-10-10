#include "qa/network_events.h"

#include <string.h>

bool qa_network_events_collect(const qa_network_event_source *source, qa_platform_events *events,
    uint64_t now_ns, uint32_t budget, qa_error *error)
{
    for (uint32_t i = 0; i < budget; ++i) {
        if (qa_platform_events_admit(events, QA_PLATFORM_EVENT_PACKET,
            source->datagram_bytes) != QA_PLATFORM_EVENT_ACCEPTED) break;
        qa_net_transport_event input;
        if (!source->collect(source->context, now_ns, &input, error)) return false;
        const qa_net_datagram *packet=&input.packet;
        size_t size = packet->kind == QA_NET_POLL_PACKET ? packet->payload.size : 0;
        qa_sys_event event = {.kind = QA_PLATFORM_EVENT_PACKET,
            .time_ns = packet->kind == QA_NET_POLL_EMPTY ? now_ns : packet->received_ns,
            .data.packet = {.source_id = source->id, .from = packet->from,
                .source = input.source, .route = input.route, .result = packet->kind,
                .destination = source->destination}};
        (void)qa_platform_events_push(events, &event,
            (qa_bytes){packet->payload.data, size}, (qa_bytes){0});
        if (packet->kind == QA_NET_POLL_EMPTY) break;
    }
    return true;
}

void qa_network_event_packet(const qa_sys_event *event, qa_bytes bytes,
    uint64_t *source_id, qa_net_transport_event *input)
{
    *source_id = event->data.packet.source_id;
    *input = (qa_net_transport_event){.packet = {.kind = event->data.packet.result,
        .from = event->data.packet.from, .payload = bytes, .received_ns = event->time_ns},
        .source = event->data.packet.source, .route = event->data.packet.route};
}
