#ifndef QA_UNIFIED_CHANNEL_INTERNAL_H
#define QA_UNIFIED_CHANNEL_INTERNAL_H
#include "qa/network_unified_save.h"
#include "qa/pool.h"

typedef struct sent_fragment {
    uint64_t at;
    uint32_t attempts;
    bool acknowledged;
} sent_fragment;
typedef struct channel_payload {
    size_t size, pages, page_bytes;
    uint8_t *data;
    size_t slot;
    size_t *directory;
    qa_pool *backing, *indices;
    bool sent_pages;
} channel_payload;
typedef struct outgoing {
    struct outgoing *next;
    channel_payload payload;
    size_t slot;
    bool reliable;
    uint32_t sequence, required;
    uint32_t next_fragment, acknowledged;
    uint16_t fragments;
} outgoing;
typedef struct assembly {
    channel_payload payload;
    size_t slot;
    uint8_t *received, *pending_ack;
    uint64_t started;
    uint32_t sequence, required, fragment_bytes, received_count;
    uint16_t fragments;
} assembly;
struct qa_unified_channel {
    qa_unified_token token;
    qa_unified_limits limits;
    uint64_t next_reliable, next_frame;
    uint32_t reliable_received, reliable_acknowledged, frame_received, newest_frame;
    uint32_t frame_admitted, frame_acknowledged, frame_transmitted;
    outgoing *reliable, *tail, *frame, *pending_frame;
    uint32_t reliable_count, reliable_cursor;
    size_t queued_bytes, received_bytes;
    assembly *assemblies[64], *frame_assembly, *waiting_frame;
    uint32_t cumulative_sequence;
    uint16_t cumulative_fragment;
    uint8_t *packet;
    qa_arena storage;
    qa_pool outgoing_records, assembly_records;
    qa_pool tx_pages, rx_pages, frame_tx_payloads, frame_rx_payloads, head_rx_payload;
    qa_pool tx_indices, rx_indices;
    size_t directory_count, storage_bytes;
    uint64_t delivery_copied_bytes, delivery_copies;
    unsigned lane;
    bool cumulative_pending, frame_ack_pending, closed, busy;
};
bool qa_unified_channel_storage_prepare(qa_unified_channel *, qa_error *);
outgoing *qa_unified_outgoing_acquire(qa_unified_channel *, size_t bytes, uint16_t fragments,
    bool reliable, qa_error *);
void qa_unified_outgoing_release(qa_unified_channel *, outgoing *);
assembly *qa_unified_assembly_acquire(qa_unified_channel *, size_t bytes, uint16_t fragments,
    bool reliable, uint32_t sequence, qa_error *);
void qa_unified_assembly_release(qa_unified_channel *, assembly *);
void *qa_unified_payload_page(const channel_payload *, size_t page);
sent_fragment *qa_unified_sent_fragment(const outgoing *, uint32_t fragment);
void qa_unified_payload_copy(const channel_payload *, size_t offset, const void *, size_t bytes);
void qa_unified_payload_gather(const channel_payload *, void *);
void qa_unified_assembly_promote_head(qa_unified_channel *, assembly *);
bool qa_unified_payload_equal(const channel_payload *, qa_bytes);
bool qa_unified_payload_write(qa_net_writer *, const channel_payload *);
bool qa_unified_payload_read(qa_net_reader *, const channel_payload *);
uint8_t qa_unified_payload_byte(const channel_payload *, size_t offset);
bool qa_unified_channel_valid(const qa_unified_channel *, qa_error *);
typedef bool (*qa_unified_admit_delivery_fn)(void *, const qa_unified_delivery *);
/* Declining a delivery retains its complete assembly without advancing the
 * cumulative receive receipt. ACK and outgoing transport still progress. */
bool qa_unified_channel_receive_buffered(qa_unified_channel *, qa_bytes, uint64_t,
    qa_unified_admit_delivery_fn, qa_unified_deliver_fn, void *, qa_error *);
bool qa_unified_channel_resume(qa_unified_channel *, qa_unified_admit_delivery_fn,
    qa_unified_deliver_fn, void *, qa_error *);
bool qa_unified_channel_reliable_ready(const qa_unified_channel *, const qa_bytes *, size_t,
    bool *, qa_error *);
bool qa_unified_channel_reliable_batch(qa_unified_channel *, const qa_bytes *, size_t,
    uint32_t *first, uint32_t *last, qa_error *);
bool qa_unified_channel_frame_applied(qa_unified_channel *, uint32_t, qa_error *);
#endif
