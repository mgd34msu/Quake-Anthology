#ifndef QA_APPLICATION_NATIVE_Q2_DELIVERY_H
#define QA_APPLICATION_NATIVE_Q2_DELIVERY_H

#include "qa/application.h"
#include "qa/native.h"
#include "qa/network.h"

typedef enum qa_application_q2_delivery_kind {
    QA_APPLICATION_Q2_PVS,
    QA_APPLICATION_Q2_PHS,
    QA_APPLICATION_Q2_ALL,
    QA_APPLICATION_Q2_UNICAST
} qa_application_q2_delivery_kind;

typedef struct qa_application_q2_recipient {
    qa_actor_id actor;
    qa_vec3 origin;
    int32_t area, cluster;
    /* Historical transport admission at emission, distinct from the physical
     * actor and seat. Absence never grants a split-message recipient. */
    qa_net_client_id connection;
    qa_net_seat_id connection_seat;
    uint64_t connection_epoch;
    uint8_t remote_index;
    bool has_connection;
} qa_application_q2_recipient;

typedef struct qa_application_q2_audience {
    qa_actor_owner source, world_source;
    qa_source_frame source_frame;
    uint64_t source_time_ns, map_identity;
    qa_vec3 multicast_origin;
    int32_t area, cluster;
    const qa_application_q2_recipient *recipients;
    size_t count;
    qa_application_q2_delivery_kind kind;
    bool captured, positioned;
} qa_application_q2_audience;

typedef struct qa_application_q2_protocol_delivery {
    qa_native_profile profile;
    qa_application_q2_audience audience;
    /* Original API2023 unicast group key; connection grouping is owned by
     * the actual transport, independently of actor or physical-seat IDs. */
    uint32_t dupe_key;
    /* No eligible original client existed at emission when audience is
     * absent: multicast needs connected clients; unicast needs its actual
     * addressed source slot. The profile still qualifies the message bytes. */
    bool original;
} qa_application_q2_protocol_delivery;

/* Borrows the actual emission-time recipients until queue clear.
 * captured=true with count=0 is a real delivery to no clients. These values
 * do not grant live source access or recompute visibility from a camera. */
bool qa_application_event_q2_audience_at(const qa_application *, size_t,
    qa_application_q2_audience *);
bool qa_application_q2_map_event_audience_at(const qa_application *, size_t,
    qa_application_q2_audience *);
bool qa_application_protocol_q2_delivery_at(const qa_application *, size_t,
    qa_application_q2_protocol_delivery *);

#endif
