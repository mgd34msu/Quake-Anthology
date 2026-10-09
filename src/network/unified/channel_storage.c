#include "channel_internal.h"

#include <string.h>

#define PAGE_INDICES 64u

static bool extent(size_t *total, size_t count, size_t stride, size_t alignment, qa_error *error)
{
    if (stride > SIZE_MAX - (alignment - 1)) goto overflow;
    stride = (stride + alignment - 1) & ~(alignment - 1);
    if (count > SIZE_MAX / stride || count > SIZE_MAX / sizeof(size_t)) goto overflow;
    size_t bytes = count * stride, indices = count * sizeof(size_t);
    size_t padding = (size_t)(-bytes) & (_Alignof(size_t) - 1);
    size_t storage_alignment = alignment > _Alignof(size_t) ? alignment : _Alignof(size_t);
    if (padding > SIZE_MAX - bytes || indices > SIZE_MAX - bytes - padding ||
        storage_alignment - 1 > SIZE_MAX - bytes - padding - indices ||
        bytes + padding + indices + storage_alignment - 1 > SIZE_MAX - *total) goto overflow;
    *total += bytes + padding + indices + storage_alignment - 1;
    return true;
overflow:
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Unified fixed storage exceeds native capacity");
    return false;
}

static size_t page_bound(size_t count, size_t pages_per_message, size_t budget, size_t page_bytes)
{
    size_t aggregate = budget / page_bytes;
    if (aggregate > SIZE_MAX - count) aggregate = SIZE_MAX;
    else aggregate += count;
    if (count > SIZE_MAX / pages_per_message) return aggregate;
    size_t all = count * pages_per_message;
    return all < aggregate ? all : aggregate;
}

bool qa_unified_channel_storage_prepare(qa_unified_channel *c, qa_error *error)
{
    size_t page_bytes = c->limits.datagram_bytes - QA_UNIFIED_HEADER_BYTES;
    size_t max_pages = ((size_t)c->limits.message_bytes - 1) / page_bytes + 1;
    size_t records = c->limits.queued_reliable_messages;
    size_t window = c->limits.reliable_window_messages < records ?
        c->limits.reliable_window_messages : records;
    size_t tx = page_bound(records, max_pages, c->limits.queued_reliable_bytes, page_bytes);
    size_t rx = page_bound(window, max_pages, c->limits.queued_reliable_bytes, page_bytes);
    c->directory_count = (max_pages - 1) / PAGE_INDICES + 1;
    if (records > SIZE_MAX - 2 ||
        c->directory_count > (SIZE_MAX - sizeof(assembly) - 2u * c->limits.fragments) / sizeof(size_t) ||
        c->directory_count > (SIZE_MAX - sizeof(outgoing)) / sizeof(size_t)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Unified fixed record extent exceeds storage"); return false;
    }
    size_t out_stride = sizeof(outgoing) + c->directory_count * sizeof(size_t);
    size_t in_stride = sizeof(assembly) + c->directory_count * sizeof(size_t) + 2u * c->limits.fragments;
    if (records > SIZE_MAX - tx / PAGE_INDICES || window > SIZE_MAX - rx / PAGE_INDICES) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Unified fixed index extent exceeds storage"); return false;
    }
    size_t tx_tables = tx / PAGE_INDICES + records;
    size_t rx_tables = rx / PAGE_INDICES + window;
    const size_t table_stride = PAGE_INDICES * sizeof(size_t);
    size_t capacity = c->limits.datagram_bytes;
    if (c->limits.message_bytes > SIZE_MAX - capacity - 64) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Unified delivery extent exceeds storage"); return false;
    }
    capacity += 64;
    if (!extent(&capacity, records + 2, out_stride, _Alignof(outgoing), error) ||
        !extent(&capacity, window + 2, in_stride, _Alignof(assembly), error) ||
        !extent(&capacity, tx, page_bytes + sizeof(sent_fragment), _Alignof(sent_fragment), error) ||
        !extent(&capacity, rx, page_bytes, 1, error) ||
        !extent(&capacity, 2, c->limits.message_bytes, 1, error) ||
        !extent(&capacity, 2, c->limits.message_bytes, 1, error) ||
        !extent(&capacity, 1, c->limits.message_bytes, 1, error) ||
        !extent(&capacity, tx_tables, table_stride, _Alignof(size_t), error) ||
        !extent(&capacity, rx_tables, table_stride, _Alignof(size_t), error)) return false;
    qa_arena_init(&c->storage, capacity);
    if (!qa_arena_reserve(&c->storage, capacity, error)) return false;
    bool okay = qa_pool_prepare(&c->outgoing_records, &c->storage, records + 2, out_stride, _Alignof(outgoing), error) &&
        qa_pool_prepare(&c->assembly_records, &c->storage, window + 2, in_stride, _Alignof(assembly), error) &&
        qa_pool_prepare(&c->tx_pages, &c->storage, tx, page_bytes + sizeof(sent_fragment), _Alignof(sent_fragment), error) &&
        qa_pool_prepare(&c->rx_pages, &c->storage, rx, page_bytes, 1, error) &&
        qa_pool_prepare(&c->frame_tx_payloads, &c->storage, 2, c->limits.message_bytes, 1, error) &&
        qa_pool_prepare(&c->frame_rx_payloads, &c->storage, 2, c->limits.message_bytes, 1, error) &&
        qa_pool_prepare(&c->head_rx_payload, &c->storage, 1, c->limits.message_bytes, 1, error) &&
        qa_pool_prepare(&c->tx_indices, &c->storage, tx_tables, table_stride, _Alignof(size_t), error) &&
        qa_pool_prepare(&c->rx_indices, &c->storage, rx_tables, table_stride, _Alignof(size_t), error);
    if (okay) {
        c->packet = qa_arena_alloc(&c->storage, c->limits.datagram_bytes, 1, error);
        okay = c->packet != NULL;
    }
    if (!okay) return false;
    memset(c->packet, 0, c->limits.datagram_bytes);
    c->storage_bytes = capacity;
    qa_arena_seal(&c->storage);
    return true;
}

static size_t page_bytes(const channel_payload *p)
{
    return p->page_bytes;
}

static size_t payload_page_slot(const channel_payload *p, size_t page)
{
    const size_t *indices = qa_pool_at(p->indices, p->directory[page / PAGE_INDICES]);
    return indices[page % PAGE_INDICES];
}

void *qa_unified_payload_page(const channel_payload *p, size_t page)
{
    if (p->data) return p->data + page * p->page_bytes;
    uint8_t *value = qa_pool_at(p->backing, payload_page_slot(p, page));
    return value + (p->sent_pages ? sizeof(sent_fragment) : 0);
}

sent_fragment *qa_unified_sent_fragment(const outgoing *m, uint32_t fragment)
{
    return qa_pool_at(m->payload.backing, payload_page_slot(&m->payload, fragment));
}

static void payload_release(channel_payload *p)
{
    if (p->data) {
        qa_pool_release(p->backing, p->slot);
        p->data = NULL; p->pages = 0;
        return;
    }
    for (size_t i = 0; i < p->pages; ++i)
        qa_pool_release(p->backing, payload_page_slot(p, i));
    size_t groups = p->pages ? (p->pages - 1) / PAGE_INDICES + 1 : 0;
    for (size_t i = 0; i < groups; ++i) qa_pool_release(p->indices, p->directory[i]);
    p->pages = 0;
}

static bool payload_prepare(channel_payload *p, size_t bytes, qa_pool *pages, qa_pool *indices,
    size_t *directory, size_t chunk, bool sent, qa_error *error)
{
    *p = (channel_payload){.size=bytes, .directory=directory, .backing=pages, .indices=indices, .page_bytes=chunk, .sent_pages=sent};
    if (!indices) {
        p->data = qa_pool_take(pages, &p->slot);
        if (!p->data) goto exhausted;
        p->pages = 1;
        return true;
    }
    size_t count = bytes ? (bytes - 1) / chunk + 1 : 1;
    for (size_t i = 0; i < count; ++i) {
        if (i % PAGE_INDICES == 0) {
            size_t slot;
            if (!qa_pool_take(indices, &slot)) goto exhausted;
            directory[i / PAGE_INDICES] = slot;
        }
        size_t slot;
        void *value = qa_pool_take(pages, &slot);
        if (!value) {
            if (i % PAGE_INDICES == 0) qa_pool_release(indices, directory[i / PAGE_INDICES]);
            goto exhausted;
        }
        size_t *table = qa_pool_at(indices, directory[i / PAGE_INDICES]);
        table[i % PAGE_INDICES] = slot;
        ++p->pages;
        if (sent) memset(value, 0, sizeof(sent_fragment));
    }
    return true;
exhausted:
    payload_release(p);
    qa_error_set(error, QA_ERROR_MEMORY, 0, "Unified fixed storage capacity exhausted");
    return false;
}

outgoing *qa_unified_outgoing_acquire(qa_unified_channel *c, size_t bytes, uint16_t fragments,
    bool reliable, qa_error *error)
{
    size_t slot;
    outgoing *m = qa_pool_take(&c->outgoing_records, &slot);
    if (!m) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Unified outgoing record capacity exhausted"); return NULL; }
    *m = (outgoing){.slot=slot, .fragments=fragments, .reliable=reliable};
    if (!payload_prepare(&m->payload, bytes, reliable ? &c->tx_pages : &c->frame_tx_payloads,
        reliable ? &c->tx_indices : NULL, reliable ? (size_t *)(m + 1) : NULL,
        c->limits.datagram_bytes - QA_UNIFIED_HEADER_BYTES, reliable, error)) {
        qa_pool_release(&c->outgoing_records, slot); return NULL;
    }
    return m;
}

void qa_unified_outgoing_release(qa_unified_channel *c, outgoing *m)
{
    if (!m) return;
    payload_release(&m->payload);
    qa_pool_release(&c->outgoing_records, m->slot);
}

assembly *qa_unified_assembly_acquire(qa_unified_channel *c, size_t bytes, uint16_t fragments,
    bool reliable, uint32_t sequence, qa_error *error)
{
    size_t slot;
    assembly *a = qa_pool_take(&c->assembly_records, &slot);
    if (!a) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Unified assembly record capacity exhausted"); return NULL; }
    *a = (assembly){.slot=slot, .fragments=fragments};
    size_t *directory = (size_t *)(a + 1);
    a->received = (uint8_t *)(directory + c->directory_count);
    a->pending_ack = reliable ? a->received + c->limits.fragments : NULL;
    memset(a->received, 0, fragments);
    if (reliable) memset(a->pending_ack, 0, fragments);
    bool paged = reliable && (sequence != (uint64_t)c->reliable_received + 1 || c->head_rx_payload.active);
    qa_pool *backing = paged ? &c->rx_pages : reliable ? &c->head_rx_payload : &c->frame_rx_payloads;
    if (!payload_prepare(&a->payload, bytes, backing, paged ? &c->rx_indices : NULL,
        paged ? directory : NULL, c->limits.datagram_bytes - QA_UNIFIED_HEADER_BYTES, false, error)) {
        qa_pool_release(&c->assembly_records, slot); return NULL;
    }
    if (a->payload.data) {
        if (bytes) memset(a->payload.data, 0, bytes);
    } else {
        size_t remain = bytes;
        for (size_t i = 0; i < a->payload.pages; ++i) {
            size_t chunk = page_bytes(&a->payload), size = remain < chunk ? remain : chunk;
            if (size) memset(qa_unified_payload_page(&a->payload, i), 0, size);
            remain -= size;
        }
    }
    return a;
}

void qa_unified_assembly_promote_head(qa_unified_channel *c, assembly *a)
{
    if (a->payload.data) return;
    size_t slot;
    uint8_t *data = qa_pool_take(&c->head_rx_payload, &slot);
    size_t size = a->payload.size, chunk = a->payload.page_bytes;
    qa_unified_payload_gather(&a->payload, data);
    payload_release(&a->payload);
    a->payload = (channel_payload){.size=size, .pages=1, .page_bytes=chunk, .data=data,
        .slot=slot, .backing=&c->head_rx_payload};
    c->delivery_copied_bytes += size; ++c->delivery_copies;
}

void qa_unified_assembly_release(qa_unified_channel *c, assembly *a)
{
    if (!a) return;
    payload_release(&a->payload);
    qa_pool_release(&c->assembly_records, a->slot);
}

void qa_unified_payload_copy(const channel_payload *p, size_t offset, const void *source, size_t bytes)
{
    if (p->data) {
        if (bytes) memcpy(p->data + offset, source, bytes);
        return;
    }
    const uint8_t *input = source;
    size_t chunk = page_bytes(p);
    while (bytes) {
        size_t page = offset / chunk, within = offset % chunk;
        size_t size = chunk - within;
        if (size > bytes) size = bytes;
        memcpy((uint8_t *)qa_unified_payload_page(p, page) + within, input, size);
        offset += size; input += size; bytes -= size;
    }
}

void qa_unified_payload_gather(const channel_payload *p, void *destination)
{
    if (p->data) {
        if (p->size) memcpy(destination, p->data, p->size);
        return;
    }
    uint8_t *output = destination;
    size_t remain = p->size, chunk = page_bytes(p);
    for (size_t i = 0; remain; ++i) {
        size_t size = remain < chunk ? remain : chunk;
        memcpy(output, qa_unified_payload_page(p, i), size);
        output += size; remain -= size;
    }
}

bool qa_unified_payload_equal(const channel_payload *p, qa_bytes bytes)
{
    if (p->size != bytes.size) return false;
    if (p->data) return !bytes.size || !memcmp(p->data, bytes.data, bytes.size);
    size_t remain = p->size, chunk = page_bytes(p);
    const uint8_t *input = bytes.data;
    for (size_t i = 0; remain; ++i) {
        size_t size = remain < chunk ? remain : chunk;
        if (memcmp(qa_unified_payload_page(p, i), input, size)) return false;
        input += size; remain -= size;
    }
    return true;
}

bool qa_unified_payload_write(qa_net_writer *w, const channel_payload *p)
{
    if (p->data) return qa_net_write_data(w, p->data, p->size);
    size_t remain = p->size, chunk = page_bytes(p);
    for (size_t i = 0; remain; ++i) {
        size_t size = remain < chunk ? remain : chunk;
        if (!qa_net_write_data(w, qa_unified_payload_page(p, i), size)) return false;
        remain -= size;
    }
    return true;
}

bool qa_unified_payload_read(qa_net_reader *r, const channel_payload *p)
{
    if (p->data) return qa_net_read_data(r, p->data, p->size);
    size_t remain = p->size, chunk = page_bytes(p);
    for (size_t i = 0; remain; ++i) {
        size_t size = remain < chunk ? remain : chunk;
        if (!qa_net_read_data(r, qa_unified_payload_page(p, i), size)) return false;
        remain -= size;
    }
    return true;
}

uint8_t qa_unified_payload_byte(const channel_payload *p, size_t offset)
{
    if (p->data) return p->data[offset];
    size_t chunk = page_bytes(p);
    return ((const uint8_t *)qa_unified_payload_page(p, offset / chunk))[offset % chunk];
}
