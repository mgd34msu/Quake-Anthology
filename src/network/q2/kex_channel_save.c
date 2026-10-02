#include "kex_channel_internal.h"
#include "qa/network_kex_save.h"

#include <stdlib.h>
#include <string.h>

bool qa_kex_channel_valid(const qa_kex_channel *c)
{
    if (!c || !c->emit || c->entered || c->pending_count > 32767 ||
        c->pending_bytes > QA_KEX_MESSAGE_BYTES * 2 ||
        c->fragment_capacity > QA_KEX_MESSAGE_BYTES || c->expanded_capacity > QA_KEX_MESSAGE_BYTES ||
        c->fragment_size > c->fragment_capacity || c->expanded_size > c->expanded_capacity ||
        (!!c->fragment_capacity != !!c->fragments) || (!!c->expanded_capacity != !!c->expanded) ||
        (c->ack != 0 && c->ack != 4 && c->ack != 6)) return false;
    size_t count = 0, bytes = 0;
    const struct pending *last = NULL;
    for (const struct pending *p = c->head; p; p = p->next) {
        if (++count > c->pending_count || p->size > QA_KEX_DATAGRAM_BYTES) return false;
        qa_kex_packet packet;
        qa_error invalid = {0};
        if (!qa_kex_packet_read((qa_bytes){p->bytes, p->size}, &packet, &invalid) ||
            (packet.flags & 3u) != QA_KEX_RELIABLE || packet.reliable != p->reliable ||
            (last && p->reliable != (uint16_t)(last->reliable + 1))) return false;
        bytes += p->size;
        last = p;
    }
    return count == c->pending_count && bytes == c->pending_bytes && last == c->tail &&
        (!last || last->reliable == c->reliable);
}

bool qa_kex_channel_checkpoint(const qa_kex_channel *c, qa_buffer *out, qa_error *e)
{
    if (!out || !qa_kex_channel_valid(c)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "KEX channel continuation requires an idle valid owner"); return false;
    }
    size_t capacity = 96 + c->pending_count * 7 + c->pending_bytes + c->fragment_capacity + c->expanded_capacity;
    uint8_t *bytes = malloc(capacity);
    if (!bytes) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Encoding KEX channel continuation"); return false; }
    qa_net_writer w;
    qa_net_writer_init(&w, bytes, capacity, e);
    bool ok = qa_net_write_data(&w, "QAKC", 4) && qa_net_write_u32(&w, 1) &&
        qa_net_write_u16(&w, c->sequence) && qa_net_write_u16(&w, c->reliable) &&
        qa_net_write_u16(&w, c->incoming_sequence) && qa_net_write_u16(&w, c->incoming_reliable) &&
        qa_net_write_u16(&w, c->fragment_sequence) && qa_net_write_u8(&w, c->fragment_kind) &&
        qa_net_write_u8(&w, c->ack) && qa_net_write_u8(&w, c->fragmented) &&
        qa_net_write_u32(&w, c->retries) && qa_net_write_u64(&w, c->retry_at) && qa_net_write_u64(&w, c->received_at) &&
        qa_net_write_u32(&w, (uint32_t)c->pending_count) && qa_net_write_u32(&w, (uint32_t)c->pending_bytes) &&
        qa_net_write_u32(&w, (uint32_t)c->fragment_size) && qa_net_write_u32(&w, (uint32_t)c->fragment_capacity) &&
        qa_net_write_u32(&w, (uint32_t)c->expanded_size) && qa_net_write_u32(&w, (uint32_t)c->expanded_capacity);
    for (const struct pending *p = c->head; ok && p; p = p->next)
        ok = qa_net_write_u16(&w, p->reliable) && qa_net_write_u8(&w, p->sent) &&
            qa_net_write_u32(&w, (uint32_t)p->size) && qa_net_write_data(&w, p->bytes, p->size);
    ok = ok && qa_net_write_data(&w, c->fragments, c->fragment_capacity) &&
        qa_net_write_data(&w, c->expanded, c->expanded_capacity);
    if (!ok || w.failed) { free(bytes); return false; }
    *out = (qa_buffer){bytes, qa_net_writer_size(&w)};
    return true;
}

bool qa_kex_channel_restore(qa_bytes bytes, qa_kex_emit_fn emit, void *user,
                            qa_kex_channel **out, qa_error *e)
{
    if (!out || !emit || !bytes.data || bytes.size < 65 ||
        bytes.size > 96u + 32767u * 7u + QA_KEX_MESSAGE_BYTES * 4u || memcmp(bytes.data, "QAKC", 4)) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid KEX channel continuation envelope"); return false;
    }
    qa_kex_channel *c = calloc(1, sizeof(*c));
    if (!c) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring KEX channel continuation"); return false; }
    c->emit = emit;
    c->user = user;
    qa_net_reader r;
    qa_net_reader_init(&r, bytes, e);
    r.bit = 32;
    bool ok = qa_net_read_u32(&r) == 1;
    c->sequence = qa_net_read_u16(&r);
    c->reliable = qa_net_read_u16(&r);
    c->incoming_sequence = qa_net_read_u16(&r);
    c->incoming_reliable = qa_net_read_u16(&r);
    c->fragment_sequence = qa_net_read_u16(&r);
    c->fragment_kind = qa_net_read_u8(&r);
    c->ack = qa_net_read_u8(&r);
    uint8_t fragmented = qa_net_read_u8(&r);
    c->fragmented = fragmented != 0;
    c->retries = qa_net_read_u32(&r);
    c->retry_at = qa_net_read_u64(&r);
    c->received_at = qa_net_read_u64(&r);
    c->pending_count = qa_net_read_u32(&r);
    c->pending_bytes = qa_net_read_u32(&r);
    c->fragment_size = qa_net_read_u32(&r);
    c->fragment_capacity = qa_net_read_u32(&r);
    c->expanded_size = qa_net_read_u32(&r);
    c->expanded_capacity = qa_net_read_u32(&r);
    ok = ok && !r.failed && fragmented <= 1 && c->pending_count <= 32767 &&
        c->pending_count <= qa_net_reader_remaining(&r) / 7 && c->pending_bytes <= QA_KEX_MESSAGE_BYTES * 2 &&
        c->fragment_capacity <= QA_KEX_MESSAGE_BYTES && c->expanded_capacity <= QA_KEX_MESSAGE_BYTES &&
        c->fragment_size <= c->fragment_capacity && c->expanded_size <= c->expanded_capacity;
    for (size_t i = 0; ok && i < c->pending_count; ++i) {
        struct pending *p = calloc(1, sizeof(*p));
        if (!p) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring KEX reliable packet"); ok = false; break; }
        if (c->tail) c->tail->next = p;
        else c->head = p;
        c->tail = p;
        p->reliable = qa_net_read_u16(&r);
        uint8_t sent = qa_net_read_u8(&r);
        p->sent = sent != 0;
        p->size = qa_net_read_u32(&r);
        ok = !r.failed && sent <= 1 && p->size <= QA_KEX_DATAGRAM_BYTES && qa_net_read_data(&r, p->bytes, p->size);
    }
    if (ok && c->fragment_capacity) {
        c->fragments = malloc(c->fragment_capacity);
        if (!c->fragments) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring KEX fragment backing"); ok = false; }
        else ok = qa_net_read_data(&r, c->fragments, c->fragment_capacity);
    }
    if (ok && c->expanded_capacity) {
        c->expanded = malloc(c->expanded_capacity);
        if (!c->expanded) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Restoring KEX expanded backing"); ok = false; }
        else ok = qa_net_read_data(&r, c->expanded, c->expanded_capacity);
    }
    if (!ok || !qa_net_reader_finish(&r) || !qa_kex_channel_valid(c)) {
        qa_kex_channel_destroy(c);
        if (!e || e->code == QA_OK) qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid KEX channel retained state");
        return false;
    }
    *out = c;
    return true;
}
