#include "channel_internal.h"

#include <stdlib.h>
#include <string.h>

static bool add(size_t *capacity, size_t bytes, qa_error *e)
{
    if (bytes > SIZE_MAX - *capacity) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Unified continuation exceeds storage"); return false; }
    *capacity += bytes; return true;
}

static bool outgoing_extent(const outgoing *m, bool reliable, size_t *capacity, qa_error *e)
{
    return !m || (add(capacity, 24, e) && add(capacity, m->payload.size, e) &&
        (!reliable || add(capacity, (size_t)m->fragments * 13, e)));
}
static bool assembly_extent(const assembly *a, bool reliable, size_t *capacity, qa_error *e)
{
    return !a || (add(capacity, 31, e) && add(capacity, a->payload.size, e) &&
        add(capacity, (size_t)a->fragments * (reliable ? 2u : 1u), e));
}

static bool limits_write(qa_net_writer *w, const qa_unified_limits *l)
{
    return qa_net_write_u32(w, l->datagram_bytes) && qa_net_write_u32(w, l->message_bytes) &&
        qa_net_write_u64(w, (uint64_t)l->queued_reliable_bytes) && qa_net_write_u32(w, l->queued_reliable_messages) &&
        qa_net_write_u32(w, l->reliable_window_messages) && qa_net_write_u16(w, l->fragments) &&
        qa_net_write_u32(w, l->packets_per_flush) && qa_net_write_u32(w, l->maximum_transmissions) &&
        qa_net_write_u64(w, l->retry_ns) && qa_net_write_u64(w, l->assembly_ns);
}

static bool limits_read(qa_net_reader *r, qa_unified_limits *l)
{
    l->datagram_bytes = qa_net_read_u32(r); l->message_bytes = qa_net_read_u32(r);
    uint64_t queued = qa_net_read_u64(r);
    if (queued > SIZE_MAX) return qa_net_reader_fail(r, "Unified reliable budget exceeds native storage");
    l->queued_reliable_bytes = (size_t)queued;
    l->queued_reliable_messages = qa_net_read_u32(r); l->reliable_window_messages = qa_net_read_u32(r);
    l->fragments = qa_net_read_u16(r); l->packets_per_flush = qa_net_read_u32(r);
    l->maximum_transmissions = qa_net_read_u32(r); l->retry_ns = qa_net_read_u64(r); l->assembly_ns = qa_net_read_u64(r);
    return !r->failed;
}

static bool outgoing_write(qa_net_writer *w, const outgoing *m, bool reliable)
{
    if (!qa_net_write_u8(w, m != NULL) || !m) return !w->failed;
    bool ok = qa_net_write_u32(w, m->sequence) && qa_net_write_u32(w, m->required) &&
        qa_net_write_u32(w, m->next_fragment) && qa_net_write_u32(w, m->acknowledged) &&
        qa_net_write_u16(w, m->fragments) && qa_net_write_u32(w, (uint32_t)m->payload.size) &&
        qa_unified_payload_write(w, &m->payload);
    for (uint32_t i = 0; ok && reliable && i < m->fragments; ++i)
        ok = qa_net_write_u64(w, qa_unified_sent_fragment(m,i)->at) && qa_net_write_u32(w, qa_unified_sent_fragment(m,i)->attempts) && qa_net_write_u8(w, qa_unified_sent_fragment(m,i)->acknowledged);
    return ok;
}

static bool assembly_write(qa_net_writer *w, const assembly *a, bool reliable)
{
    if (!qa_net_write_u8(w, a != NULL) || !a) return !w->failed;
    return qa_net_write_u64(w, a->started) && qa_net_write_u32(w, a->sequence) && qa_net_write_u32(w, a->required) &&
        qa_net_write_u32(w, a->fragment_bytes) && qa_net_write_u32(w, a->received_count) &&
        qa_net_write_u16(w, a->fragments) && qa_net_write_u32(w, (uint32_t)a->payload.size) &&
        qa_unified_payload_write(w, &a->payload) && qa_net_write_data(w, a->received, a->fragments) &&
        (!reliable || qa_net_write_data(w, a->pending_ack, a->fragments));
}

bool qa_unified_channel_checkpoint(const qa_unified_channel *c, qa_buffer *out, qa_error *e)
{
    if (!out || !qa_unified_channel_idle(c) || !qa_unified_channel_valid(c, e)) {
        if (!e || e->code == QA_OK) qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Unified channel capture requires its complete idle owner");
        return false;
    }
    size_t capacity = 256;
    bool ok = add(&capacity, c->limits.datagram_bytes, e);
    for (const outgoing *m = c->reliable; ok && m; m = m->next) ok = outgoing_extent(m, true, &capacity, e);
    ok = ok && outgoing_extent(c->frame, false, &capacity, e) && outgoing_extent(c->pending_frame, false, &capacity, e);
    for (size_t i = 0; ok && i < 64; ++i) ok = assembly_extent(c->assemblies[i], true, &capacity, e);
    ok = ok && assembly_extent(c->frame_assembly, false, &capacity, e) && assembly_extent(c->waiting_frame, false, &capacity, e);
    uint8_t *data = ok ? malloc(capacity) : NULL;
    if (ok && !data) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Capturing complete unified channel"); return false; }
    qa_net_writer w; qa_net_writer_init(&w, data, ok ? capacity : 0, e);
    ok = ok && qa_net_write_data(&w, "QAUC", 4) && qa_net_write_data(&w, c->token.bytes, 16) && limits_write(&w, &c->limits) &&
        qa_net_write_u64(&w, c->next_reliable) && qa_net_write_u64(&w, c->next_frame) &&
        qa_net_write_u32(&w, c->reliable_received) && qa_net_write_u32(&w, c->reliable_acknowledged) &&
        qa_net_write_u32(&w, c->frame_received) && qa_net_write_u32(&w, c->newest_frame) &&
        qa_net_write_u32(&w, c->frame_admitted) && qa_net_write_u32(&w, c->frame_acknowledged) &&
        qa_net_write_u32(&w, c->frame_transmitted) &&
        qa_net_write_u32(&w, c->reliable_count) && qa_net_write_u32(&w, c->reliable_cursor) &&
        qa_net_write_u64(&w, (uint64_t)c->queued_bytes) && qa_net_write_u64(&w, (uint64_t)c->received_bytes) &&
        qa_net_write_u32(&w, c->cumulative_sequence) && qa_net_write_u16(&w, c->cumulative_fragment) &&
        qa_net_write_u8(&w, c->cumulative_pending) && qa_net_write_u8(&w, c->frame_ack_pending) &&
        qa_net_write_u8(&w, c->closed) && qa_net_write_u8(&w, (uint8_t)c->lane) &&
        qa_net_write_data(&w, c->packet, c->limits.datagram_bytes);
    for (const outgoing *m = c->reliable; ok && m; m = m->next) ok = outgoing_write(&w, m, true);
    ok = ok && outgoing_write(&w, c->frame, false) && outgoing_write(&w, c->pending_frame, false);
    for (size_t i = 0; ok && i < 64; ++i) ok = assembly_write(&w, c->assemblies[i], true);
    ok = ok && assembly_write(&w, c->frame_assembly, false) && assembly_write(&w, c->waiting_frame, false);
    if (!ok || w.failed) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&w)}; return true;
}

static bool flag(qa_net_reader *r, bool *out)
{
    uint8_t value = qa_net_read_u8(r);
    if (value > 1) {
        qa_net_reader_fail(r, "Invalid unified channel flag");
        return false;
    }
    *out = value != 0; return !r->failed;
}

static bool outgoing_read(qa_net_reader *r, qa_unified_channel *c, bool reliable, size_t *budget, outgoing **out)
{
    bool present;
    if (!flag(r, &present) || !present) return !r->failed;
    uint32_t sequence = qa_net_read_u32(r), required = qa_net_read_u32(r);
    uint32_t next_fragment = qa_net_read_u32(r), acknowledged = qa_net_read_u32(r);
    uint16_t fragments = qa_net_read_u16(r);
    uint32_t size = qa_net_read_u32(r);
    size_t page_bytes = c->limits.datagram_bytes - QA_UNIFIED_HEADER_BYTES;
    size_t expected_fragments = size ? ((size_t)size - 1) / page_bytes + 1 : 1;
    if (r->failed || size > c->limits.message_bytes || size > qa_net_reader_remaining(r) ||
        fragments != expected_fragments || fragments > c->limits.fragments ||
        (reliable && size > c->limits.queued_reliable_bytes - *budget))
        return qa_net_reader_fail(r, "Invalid unified outgoing extent");
    outgoing *m = qa_unified_outgoing_acquire(c,size,fragments,reliable,r->error);
    if (!m) return qa_net_reader_fail(r, "Invalid unified outgoing extent");
    *out = m;
    m->sequence = sequence; m->required = required;
    m->next_fragment = next_fragment; m->acknowledged = acknowledged;
    if (!qa_unified_payload_read(r,&m->payload)) return false;
    for (uint32_t i = 0; reliable && i < m->fragments; ++i) {
        sent_fragment *sent = qa_unified_sent_fragment(m,i);
        sent->at = qa_net_read_u64(r); sent->attempts = qa_net_read_u32(r);
        if (!flag(r, &sent->acknowledged)) return false;
    }
    if (reliable) *budget += size;
    return !r->failed;
}

static bool assembly_read(qa_net_reader *r, qa_unified_channel *c, bool reliable, size_t *budget, assembly **out)
{
    bool present;
    if (!flag(r, &present) || !present) return !r->failed;
    uint64_t started = qa_net_read_u64(r);
    uint32_t sequence = qa_net_read_u32(r), required = qa_net_read_u32(r);
    uint32_t fragment_bytes = qa_net_read_u32(r), received_count = qa_net_read_u32(r);
    uint16_t fragments = qa_net_read_u16(r);
    uint32_t size = qa_net_read_u32(r);
    if (r->failed || size > c->limits.message_bytes || size > qa_net_reader_remaining(r) || !fragments ||
        fragments > c->limits.fragments || (reliable && size > c->limits.queued_reliable_bytes - *budget))
        return qa_net_reader_fail(r, "Invalid unified assembly extent");
    assembly *a = qa_unified_assembly_acquire(c,size,fragments,reliable,sequence,r->error);
    if (!a) return qa_net_reader_fail(r, "Invalid unified assembly extent");
    *out = a;
    a->started = started; a->sequence = sequence; a->required = required;
    a->fragment_bytes = fragment_bytes; a->received_count = received_count;
    if (!qa_unified_payload_read(r,&a->payload) || !qa_net_read_data(r,a->received,fragments) ||
        (reliable && !qa_net_read_data(r,a->pending_ack,fragments))) return false;
    if (reliable) *budget += size;
    return !r->failed;
}

bool qa_unified_channel_restore(qa_bytes bytes, qa_unified_channel **out, qa_error *e)
{
    if (!out || !bytes.data || bytes.size < 4 || memcmp(bytes.data, "QAUC", 4)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Unknown unified channel continuation"); return false;
    }
    qa_net_reader r; qa_net_reader_init(&r, bytes, e); r.bit = 32;
    qa_unified_token token; qa_unified_limits limits = {0};
    if (!qa_net_read_data(&r, token.bytes, 16) || !limits_read(&r, &limits)) return false;
    qa_unified_channel *c = NULL;
    if (!qa_unified_channel_create(token, &limits, &c, e)) return false;
    c->next_reliable = qa_net_read_u64(&r); c->next_frame = qa_net_read_u64(&r);
    c->reliable_received = qa_net_read_u32(&r); c->reliable_acknowledged = qa_net_read_u32(&r);
    c->frame_received = qa_net_read_u32(&r); c->newest_frame = qa_net_read_u32(&r);
    c->frame_admitted = qa_net_read_u32(&r); c->frame_acknowledged = qa_net_read_u32(&r);
    c->frame_transmitted = qa_net_read_u32(&r);
    c->reliable_count = qa_net_read_u32(&r); c->reliable_cursor = qa_net_read_u32(&r);
    uint64_t queued = qa_net_read_u64(&r), received = qa_net_read_u64(&r);
    c->cumulative_sequence = qa_net_read_u32(&r); c->cumulative_fragment = qa_net_read_u16(&r);
    bool ok = queued <= SIZE_MAX && received <= SIZE_MAX && c->reliable_count <= limits.queued_reliable_messages;
    c->queued_bytes = (size_t)queued; c->received_bytes = (size_t)received;
    ok = ok && flag(&r, &c->cumulative_pending) && flag(&r, &c->frame_ack_pending) && flag(&r, &c->closed);
    c->lane = qa_net_read_u8(&r);
    ok = ok && qa_net_read_data(&r, c->packet, limits.datagram_bytes);
    size_t queued_budget = 0, received_budget = 0;
    outgoing **next = &c->reliable;
    for (uint32_t i = 0; ok && i < c->reliable_count; ++i) {
        ok = outgoing_read(&r, c, true, &queued_budget, next);
        if (ok && !*next) ok = qa_net_reader_fail(&r, "Missing retained unified reliable record");
        if (ok) { c->tail = *next; next = &(*next)->next; }
    }
    ok = ok && outgoing_read(&r, c, false, &queued_budget, &c->frame) && outgoing_read(&r, c, false, &queued_budget, &c->pending_frame);
    for (size_t i = 0; ok && i < 64; ++i) ok = assembly_read(&r, c, true, &received_budget, &c->assemblies[i]);
    ok = ok && assembly_read(&r, c, false, &received_budget, &c->frame_assembly) &&
        assembly_read(&r, c, false, &received_budget, &c->waiting_frame) && qa_net_reader_finish(&r) && qa_unified_channel_valid(c, e);
    if (!ok) {
        qa_unified_channel_destroy(c);
        if (!e || e->code == QA_OK) qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid retained unified channel graph");
        return false;
    }
    *out = c; return true;
}
