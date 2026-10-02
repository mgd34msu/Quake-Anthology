#include "internal.h"
#include "../service_save_fields.h"
#include <stdlib.h>
#include <string.h>

static bool entry_write(qa_net_writer *w, const qa_server_entry *v)
{
    return q3_save_address(w, &v->address) && service_save_protocol(w, v->protocol) &&
        qa_net_write_u32(w, v->sources) && qa_net_write_u32(w, v->players) && qa_net_write_u32(w, v->maximum_players) &&
        qa_net_write_u64(w, v->updated_ns) && qa_net_write_u64(w, v->ping_ns) &&
        qa_net_write_u8(w, v->available) && qa_net_write_u8(w, v->pending) && qa_net_write_u8(w, v->timed_out) &&
        qa_net_write_u8(w, v->has_ping) &&
        service_save_text(w, v->name, sizeof(v->name)) && service_save_text(w, v->map, sizeof(v->map)) &&
        service_save_text(w, v->rules, sizeof(v->rules));
}
static bool entry_read(qa_net_reader *r, qa_server_entry *v)
{
    if (!q3_restore_address(r, &v->address) || !service_restore_protocol(r, &v->protocol)) return false;
    v->sources = qa_net_read_u32(r); v->players = qa_net_read_u32(r); v->maximum_players = qa_net_read_u32(r);
    v->updated_ns = qa_net_read_u64(r); v->ping_ns = qa_net_read_u64(r);
    v->available = q3_save_bool(r); v->pending = q3_save_bool(r); v->timed_out = q3_save_bool(r);
    v->has_ping = q3_save_bool(r);
    return qa_net_read_string(r, v->name, sizeof(v->name)) && qa_net_read_string(r, v->map, sizeof(v->map)) &&
        qa_net_read_string(r, v->rules, sizeof(v->rules));
}
bool qa_browser_q3_valid(const qa_server_browser *b)
{
    if (!b || !b->q3 || b->q3->master_source < -1 || b->q3->master_source > 2 ||
        b->q3->master_source == 0 || (b->q3->broadcast_count && !b->q3->broadcasts) ||
        (b->q3->status_count && !b->q3->status_queue)) return false;
    for (size_t i = 0; i < b->q3->status_count; ++i) {
        const browser_status_query *query=&b->q3->status_queue[i];
        const qa_net_address *address=&query->address;
        if (!service_address_valid(address) || !address->port || !qa_net_protocol_valid(query->protocol,NULL) ||
            query->protocol.kind==QA_NET_Q2KEX_DEMO_2022 || query->protocol.kind==QA_NET_UNIFIED_1) return false;
        for (size_t j = 0; j < i; ++j) {
            const browser_status_query *other=&b->q3->status_queue[j];
            if (query->protocol.kind==other->protocol.kind && query->protocol.revision==other->protocol.revision &&
                query->protocol.flags==other->protocol.flags && qa_net_address_equal(address,&other->address,true)) return false;
        }
    }
    for (uint32_t i = 0; i < b->capacity; ++i) {
        const browser_record *record = &b->records[i];
        if (!qa_browser_q3_response_valid(record)) return false;
        if (!record->occupied) { if (record->q3_order) return false; continue; }
        if (record->entry.protocol.kind == QA_NET_Q3_68) {
            if (!record->q3_order || (b->q3->next_entry_order && record->q3_order >= b->q3->next_entry_order)) return false;
            for (uint32_t j = 0; j < i; ++j) if (b->records[j].occupied && b->records[j].q3_order == record->q3_order) return false;
        } else if (record->q3_order) return false;
        if (!record->entry.pending) continue;
        if (!record->query || (b->next_query && record->query >= b->next_query)) return false;
        for (uint32_t j = 0; j < i; ++j)
            if (b->records[j].occupied && b->records[j].entry.pending && b->records[j].query == record->query) return false;
        for (size_t j = 0; j < 48; ++j) if (b->q3->requests[j].id == record->query) return false;
        for (size_t j = 0; j < b->q3->broadcast_count; ++j) if (b->q3->broadcasts[j].query == record->query) return false;
        if (b->broadcasting && b->broadcast_query == record->query) return false;
    }
    if (b->broadcasting) {
        if (!b->broadcast_query || (b->next_query && b->broadcast_query >= b->next_query)) return false;
        for (size_t i = 0; i < 48; ++i) if (b->q3->requests[i].id == b->broadcast_query) return false;
        for (size_t i = 0; i < b->q3->broadcast_count; ++i) if (b->q3->broadcasts[i].query == b->broadcast_query) return false;
    }
    for (size_t i = 0; i < b->q3->broadcast_count; ++i) {
        uint64_t id = b->q3->broadcasts[i].query;
        if (!id || (b->next_query && id >= b->next_query)) return false;
        for (size_t j = 0; j < i; ++j) if (b->q3->broadcasts[j].query == id) return false;
    }
    for (int32_t s = 0; s < 4; ++s) {
        const browser_q3_source *list = &b->q3->sources[s];
        uint64_t previous_order = 0;
        if (list->count > 8192 || list->reset_generation > list->generation) return false;
        for (uint32_t i = 0; i < list->count; ++i) {
            qa_server_entry entry;
            if (!service_address_valid(&list->addresses[i]) ||
                !qa_server_browser_q3_entry(b, &list->addresses[i], &entry) ||
                !(entry.sources & qa_browser_q3_source_bit(s))) return false;
            uint64_t order = 0;
            for (uint32_t j = 0; j < b->capacity; ++j) if (b->records[j].occupied &&
                b->records[j].entry.protocol.kind == QA_NET_Q3_68 &&
                qa_net_address_equal(&b->records[j].entry.address, &list->addresses[i], true)) { order = b->records[j].q3_order; break; }
            if (order <= previous_order) return false;
            previous_order = order;
            for (uint32_t j = 0; j < i; ++j)
                if (qa_net_address_equal(&list->addresses[j], &list->addresses[i], true)) return false;
        }
        uint32_t actual = 0;
        for (uint32_t i = 0; i < b->capacity; ++i)
            if (b->records[i].occupied && b->records[i].entry.protocol.kind == QA_NET_Q3_68 &&
                (b->records[i].entry.sources & qa_browser_q3_source_bit(s))) ++actual;
        if (actual != list->count) return false;
    }
    for (size_t i = 0; i < 48; ++i) {
        const browser_q3_request *q = &b->q3->requests[i];
        if (!q->id) continue;
        const qa_browser_q3_result *v = &q->result;
        if ((b->next_query && q->id >= b->next_query) || v->kind > QA_BROWSER_Q3_STATUS ||
            (v->address.kind != QA_NET_IPV4 && v->address.kind != QA_NET_IPV6) || !v->address.port || !service_address_valid(&v->address)) return false;
        for (size_t j = 0; j < i; ++j) if (b->q3->requests[j].id == q->id) return false;
        for (size_t j = 0; j < b->q3->broadcast_count; ++j) if (b->q3->broadcasts[j].query == q->id) return false;
        if (v->completed) {
            const qa_server_entry *entry = &v->entry;
            if (v->completed_ns < v->sent_ns || entry->updated_ns != v->completed_ns ||
                entry->ping_ns != v->completed_ns - v->sent_ns || !entry->has_ping || !entry->available || entry->pending || entry->timed_out ||
                entry->protocol.kind != QA_NET_Q3_68 || entry->protocol.revision || entry->protocol.flags ||
                !qa_net_address_equal(&v->address, &entry->address, true) || !entry->sources || (entry->sources & ~31u) ||
                entry->players > 1024 || entry->maximum_players > 1024 ||
                !memchr(entry->name, 0, sizeof(entry->name)) || !memchr(entry->map, 0, sizeof(entry->map)) ||
                !memchr(entry->rules, 0, sizeof(entry->rules)) || !memchr(v->status, 0, sizeof(v->status)) ||
                v->response_kind > QA_BROWSER_Q3_STATUS || v->player_count > 1024 || v->names_size > sizeof(v->names) ||
                (v->response_kind == QA_BROWSER_Q3_INFO && (v->player_count || v->names_size)) ||
                (v->response_kind == QA_BROWSER_Q3_STATUS && v->player_count != entry->players)) return false;
            uint32_t offset = 0;
            for (uint32_t j = 0; j < v->player_count; ++j) {
                if (v->players[j].name != offset || offset >= v->names_size) return false;
                const char *end = memchr(v->names + offset, 0, v->names_size - offset);
                if (!end) return false;
                offset = (uint32_t)(end - v->names) + 1;
            }
            if (offset != v->names_size) return false;
        } else if (v->completed_ns) return false;
    }
    return true;
}
bool qa_browser_q3_save(qa_net_writer *w, const qa_server_browser *b)
{
    const browser_q3 *q = b->q3;
    if (!qa_net_write_u64(w, q->next_entry_order)) return false;
    if (!qa_net_write_u64(w, q->status_count)) return false;
    for (size_t i = 0; i < q->status_count; ++i)
        if (!q3_save_address(w,&q->status_queue[i].address) || !service_save_protocol(w,q->status_queue[i].protocol)) return false;
    if (!qa_net_write_u64(w, q->broadcast_count)) return false;
    for (size_t i = 0; i < q->broadcast_count; ++i)
        if (!qa_net_write_u64(w, q->broadcasts[i].query) || !qa_net_write_u64(w, q->broadcasts[i].sent_ns)) return false;
    if (!qa_net_write_i32(w, q->master_source) ||
        !qa_net_write_u8(w, q->master_received)) return false;
    for (size_t s = 0; s < 4; ++s) {
        const browser_q3_source *list = &q->sources[s];
        if (!qa_net_write_u32(w, list->count) || !qa_net_write_u64(w, list->generation) ||
            !qa_net_write_u64(w, list->reset_generation)) return false;
        for (uint32_t i = 0; i < list->count; ++i) if (!q3_save_address(w, &list->addresses[i])) return false;
    }
    for (size_t i = 0; i < 48; ++i) {
        const browser_q3_request *request = &q->requests[i];
        if (!qa_net_write_u64(w, request->id)) return false;
        if (!request->id) continue;
        const qa_browser_q3_result *v = &request->result;
        if (!q3_save_address(w, &v->address) || !qa_net_write_u8(w, v->kind) ||
            !qa_net_write_u64(w, v->sent_ns) || !qa_net_write_u8(w, v->completed)) return false;
        if (v->completed) {
            if (!qa_net_write_u64(w, v->completed_ns) || !qa_net_write_u8(w, v->response_kind) ||
                !entry_write(w, &v->entry) || !service_save_text(w, v->status, sizeof(v->status)) ||
                !qa_net_write_u32(w, v->player_count)) return false;
            for (uint32_t j = 0; j < v->player_count; ++j) if (!qa_net_write_i32(w, v->players[j].score) ||
                !qa_net_write_i32(w, v->players[j].ping) || !qa_net_write_string(w, v->names + v->players[j].name)) return false;
        }
    }
    return true;
}
bool qa_browser_q3_restore(qa_net_reader *r, qa_server_browser *b)
{
    browser_q3 *q = b->q3;
    memset(q, 0, sizeof(*q));
    q->next_entry_order = qa_net_read_u64(r);
    uint64_t status_count = qa_net_read_u64(r);
    if (r->failed || status_count > SIZE_MAX / sizeof(browser_status_query) || status_count > qa_net_reader_remaining(r) / 19)
        return qa_net_reader_fail(r, "Source status queue exceeds encoded extent");
    q->status_count = (size_t)status_count;
    if (status_count) {
        q->status_queue = calloc((size_t)status_count, sizeof(*q->status_queue));
        if (!q->status_queue) { qa_error_set(r->error, QA_ERROR_MEMORY, 0, "Importing Q3 status queue"); r->failed = true; return false; }
    }
    for (size_t i = 0; i < q->status_count; ++i)
        if (!q3_restore_address(r,&q->status_queue[i].address) || !service_restore_protocol(r,&q->status_queue[i].protocol)) return false;
    uint64_t count = qa_net_read_u64(r);
    if (r->failed || count > SIZE_MAX / sizeof(browser_q3_broadcast) || count > qa_net_reader_remaining(r) / 16)
        return qa_net_reader_fail(r, "Q3 broadcast continuation exceeds encoded extent");
    q->broadcast_count = (size_t)count;
    if (count) {
        q->broadcasts = calloc((size_t)count, sizeof(*q->broadcasts));
        if (!q->broadcasts) { qa_error_set(r->error, QA_ERROR_MEMORY, 0, "Importing Q3 broadcast queries"); r->failed = true; return false; }
    }
    for (size_t i = 0; i < q->broadcast_count; ++i) {
        q->broadcasts[i].query = qa_net_read_u64(r); q->broadcasts[i].sent_ns = qa_net_read_u64(r);
    }
    q->master_source = qa_net_read_i32(r); q->master_received = q3_save_bool(r);
    for (size_t s = 0; s < 4; ++s) {
        browser_q3_source *list = &q->sources[s];
        list->count = qa_net_read_u32(r); list->generation = qa_net_read_u64(r); list->reset_generation = qa_net_read_u64(r);
        if (list->count > 8192) return qa_net_reader_fail(r, "Q3 source list exceeds retained capacity");
        for (uint32_t i = 0; i < list->count; ++i) if (!q3_restore_address(r, &list->addresses[i])) return false;
    }
    for (size_t i = 0; i < 48; ++i) {
        browser_q3_request *request = &q->requests[i];
        request->id = qa_net_read_u64(r); if (!request->id) continue;
        qa_browser_q3_result *v = &request->result;
        if (!q3_restore_address(r, &v->address)) return false;
        v->kind = (qa_browser_q3_request_kind)qa_net_read_u8(r); v->sent_ns = qa_net_read_u64(r); v->completed = q3_save_bool(r);
        if (v->completed) {
            v->completed_ns = qa_net_read_u64(r);
            v->response_kind = (qa_browser_q3_request_kind)qa_net_read_u8(r);
            if (!entry_read(r, &v->entry) || !qa_net_read_string(r, v->status, sizeof(v->status))) return false;
            v->player_count = qa_net_read_u32(r);
            if (v->player_count > 1024) return qa_net_reader_fail(r, "Q3 response player inventory exceeds source capacity");
            for (uint32_t j = 0; j < v->player_count; ++j) {
                qa_browser_q3_player *player = &v->players[j];
                player->score = qa_net_read_i32(r); player->ping = qa_net_read_i32(r);
                if (v->names_size == sizeof(v->names)) return qa_net_reader_fail(r, "Q3 response names exceed source payload");
                player->name = (uint16_t)v->names_size;
                if (!qa_net_read_string(r, v->names + v->names_size, sizeof(v->names) - v->names_size)) return false;
                v->names_size += (uint32_t)strlen(v->names + v->names_size) + 1;
            }
        }
    }
    return !r->failed;
}
