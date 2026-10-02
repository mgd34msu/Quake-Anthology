#ifndef QA_KEX_CHANNEL_INTERNAL_H
#define QA_KEX_CHANNEL_INTERNAL_H

#include "qa/network_q2_kex.h"

struct pending {
    struct pending *next;
    uint16_t reliable;
    bool sent;
    size_t size;
    uint8_t bytes[QA_KEX_DATAGRAM_BYTES];
};

struct qa_kex_channel {
    qa_kex_emit_fn emit;
    void *user;
    uint16_t sequence, reliable, incoming_sequence, incoming_reliable, fragment_sequence;
    uint8_t fragment_kind, ack;
    bool fragmented, entered;
    struct pending *head, *tail;
    size_t pending_bytes, pending_count, fragment_size, fragment_capacity, expanded_capacity;
    size_t expanded_size;
    unsigned retries;
    uint64_t retry_at, received_at;
    uint8_t *fragments, *expanded;
};

bool qa_kex_channel_valid(const qa_kex_channel *);

#endif
