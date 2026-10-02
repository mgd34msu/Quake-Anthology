#ifndef QA_UNIFIED_CHANNEL_INTERNAL_H
#define QA_UNIFIED_CHANNEL_INTERNAL_H
#include "qa/network_unified_save.h"

typedef struct sent_fragment {
    uint64_t at;
    uint32_t attempts;
    bool acknowledged;
} sent_fragment;
typedef struct outgoing {
    struct outgoing *next;
    qa_buffer payload;
    sent_fragment *sent;
    uint32_t sequence, required;
    uint32_t next_fragment, acknowledged;
    uint16_t fragments;
} outgoing;
typedef struct assembly {
    qa_buffer payload;
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
    outgoing *reliable, *tail, *frame, *pending_frame;
    uint32_t reliable_count, reliable_cursor;
    size_t queued_bytes, received_bytes;
    assembly *assemblies[64], *frame_assembly, *waiting_frame;
    uint32_t cumulative_sequence;
    uint16_t cumulative_fragment;
    uint8_t *packet;
    unsigned lane;
    bool cumulative_pending, closed, busy;
};
bool qa_unified_channel_valid(const qa_unified_channel *, qa_error *);
bool qa_unified_channel_reliable_batch(qa_unified_channel *, const qa_bytes *, size_t,
    uint32_t *first, uint32_t *last, qa_error *);
#endif
