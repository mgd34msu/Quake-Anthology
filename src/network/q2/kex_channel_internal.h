#ifndef QA_KEX_CHANNEL_INTERNAL_H
#define QA_KEX_CHANNEL_INTERNAL_H

#include "qa/network_q2_kex.h"
#include "qa/pool.h"

struct pending {
    struct pending *next;
    uint16_t reliable;
    uint64_t serial;
    bool final;
    bool sent;
    size_t size;
    size_t slot, payload;
    bool is_reliable;
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
    qa_network_reliable_receipt receipt;
    uint8_t *fragments, *expanded, *compressed;
    size_t compressed_capacity;
    uint8_t packet[QA_KEX_DATAGRAM_BYTES];
    qa_q2_compression *compression;
    qa_arena pending_storage, payload_storage, buffer_storage;
    qa_pool pending_pool, payload_pool;
};

struct pending *qa_kex_pending_take(qa_kex_channel *);
bool qa_kex_pending_store(qa_kex_channel *, struct pending *, qa_bytes);
void qa_kex_pending_read(const qa_kex_channel *, const struct pending *, uint8_t *);
void qa_kex_pending_release(qa_kex_channel *, struct pending *);
bool qa_kex_channel_valid(const qa_kex_channel *);
bool qa_kex_channel_tick_expiry(qa_kex_channel *, uint64_t, bool *, qa_error *);

#endif
