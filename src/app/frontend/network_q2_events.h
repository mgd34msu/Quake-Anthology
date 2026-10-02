#ifndef QA_FRONTEND_NETWORK_Q2_EVENTS_H
#define QA_FRONTEND_NETWORK_Q2_EVENTS_H

#include "qa/application_network_q2.h"
#include "qa/application_native_q2_delivery.h"

/* Encodes one copied Original GAME packet for an actual admitted connection.
 * The caller queues the complete owned result once, then commits its journal
 * cursor. An empty result is an authentic absence from the captured audience. */
bool frontend_network_q2_event_packet(qa_application_network_q2 *, qa_actor_owner host_source,
    const qa_application_protocol_event *, const qa_application_q2_protocol_delivery *,
    const qa_net_client *, uint64_t connection_epoch, const qa_q2_codec *, size_t capacity,
    qa_buffer *, qa_error *);

#endif
