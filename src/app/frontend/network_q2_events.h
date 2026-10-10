#ifndef QA_FRONTEND_NETWORK_Q2_EVENTS_H
#define QA_FRONTEND_NETWORK_Q2_EVENTS_H

#include "qa/application_network_q2.h"
#include "qa/application_native_q2_delivery.h"
#include "qa/network_q2_messages.h"

/* Encodes into caller-owned reusable storage for the admitted connection.
 * The caller queues the result before advancing its event cursor.
 * An empty result means the captured audience excludes that connection. */
bool frontend_network_q2_event_packet(qa_application_network_q2 *, qa_actor_owner host_source,
    const qa_application_protocol_event *, const qa_application_q2_protocol_delivery *,
    const qa_net_client *, uint64_t connection_epoch, const qa_q2_codec *, qa_q2_messages *const decoders[2], uint8_t *storage,
    size_t capacity, qa_bytes *, qa_error *);

bool frontend_network_q2_print_packet(const qa_application_q2_player_event *,
    const qa_net_client *, uint64_t connection_epoch, const qa_q2_codec *, uint8_t *storage,
    size_t capacity, qa_bytes *, qa_error *);

#endif
