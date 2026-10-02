#include "internal.h"
#include "qa/network_services_save.h"
#include "qa/server_browser_favorites.h"
#include "../service_save_fields.h"
#include <stdlib.h>
#include <string.h>

static bool address_write(qa_net_writer *writer, const qa_net_address *address) {
    if (!qa_net_write_u8(writer, (uint8_t)address->kind) || !qa_net_write_u16(writer, address->port)) return false;
    switch (address->kind) {
    case QA_NET_IPV4: return qa_net_write_data(writer, address->host.ipv4, 4);
    case QA_NET_IPV6: return qa_net_write_data(writer, address->host.ipv6.bytes, 16) && qa_net_write_u32(writer, address->host.ipv6.scope);
    case QA_NET_IPX: return qa_net_write_u32(writer, address->host.ipx.network) && qa_net_write_data(writer, address->host.ipx.node, 6);
    case QA_NET_LOOPBACK: return qa_net_write_string(writer, address->host.loopback);
    }
    return qa_net_writer_fail(writer, "Unsupported stored server address");
}
static bool address_read(qa_net_reader *reader, qa_net_address *address) {
    memset(address, 0, sizeof(*address)); address->kind = (qa_net_address_kind)qa_net_read_u8(reader);
    address->port = qa_net_read_u16(reader);
    switch (address->kind) {
    case QA_NET_IPV4: return qa_net_read_data(reader, address->host.ipv4, 4);
    case QA_NET_IPV6:
        if (!qa_net_read_data(reader, address->host.ipv6.bytes, 16)) return false;
        address->host.ipv6.scope = qa_net_read_u32(reader); return !reader->failed;
    case QA_NET_IPX:
        address->host.ipx.network = qa_net_read_u32(reader);
        return qa_net_read_data(reader, address->host.ipx.node, 6);
    case QA_NET_LOOPBACK: return qa_net_read_string(reader, address->host.loopback, sizeof(address->host.loopback));
    }
    return qa_net_reader_fail(reader, "Unsupported stored server address");
}
bool qa_server_browser_save(const qa_server_browser *browser, qa_buffer *out, qa_error *error) {
    if (!browser || !out) return qa_browser_fail(error, "Missing browser persistence output");
    size_t capacity = 12 + (size_t)browser->capacity * 160;
    uint8_t *data = malloc(capacity);
    if (!data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating server list encoding"); return false; }
    uint32_t count = 0;
    for (uint32_t i = 0; i < browser->capacity; ++i)
        if (browser->records[i].occupied && (browser->records[i].entry.sources & (QA_SERVER_FAVORITE | QA_SERVER_DIRECT))) ++count;
    qa_net_writer writer; qa_net_writer_init(&writer, data, capacity, error);
    qa_net_write_data(&writer, "QASB", 4); qa_net_write_u32(&writer, 1); qa_net_write_u32(&writer, count);
    for (uint32_t i = 0; i < browser->capacity; ++i) {
        const qa_server_entry *entry = &browser->records[i].entry;
        if (!browser->records[i].occupied || !(entry->sources & (QA_SERVER_FAVORITE | QA_SERVER_DIRECT))) continue;
        qa_net_write_u32(&writer, entry->sources & (QA_SERVER_FAVORITE | QA_SERVER_DIRECT));
        qa_net_write_u32(&writer, (uint32_t)entry->protocol.kind);
        qa_net_write_u32(&writer, entry->protocol.revision); qa_net_write_u32(&writer, entry->protocol.flags);
        address_write(&writer, &entry->address);
    }
    if (writer.failed) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&writer)}; return true;
}
static bool restore_sources(qa_server_browser *browser, qa_bytes bytes, uint32_t source_mask, qa_error *error) {
    if (!browser || browser->callback || bytes.size < 12 || !bytes.data || memcmp(bytes.data, "QASB", 4))
        return qa_browser_fail(error, "Invalid stored server list");
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error); reader.bit = 32;
    if (qa_net_read_u32(&reader) != 1) return qa_browser_fail(error, "Unsupported server list version");
    uint32_t count = qa_net_read_u32(&reader);
    if (count > browser->capacity) return qa_browser_fail(error, "Stored server list exceeds capacity");
    browser_record *records = calloc(browser->capacity, sizeof(*records));
    if (!records) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating stored server candidate"); return false; }
    memcpy(records, browser->records, browser->capacity * sizeof(*records));
    for (uint32_t i = 0; i < browser->capacity; ++i) {
        records[i].entry.sources &= ~source_mask;
        if (!records[i].entry.sources) memset(&records[i], 0, sizeof(records[i]));
    }
    browser_q3 *q3 = malloc(sizeof(*q3));
    if (!q3) { free(records); qa_error_set(error, QA_ERROR_MEMORY, 0, "Preparing stored Q3 memberships"); return false; }
    *q3 = *browser->q3;
    qa_server_browser candidate = *browser; candidate.records = records; candidate.q3 = q3; candidate.hooks.changed = NULL;
    /* The candidate's favorite memberships must follow the same remove owner. */
    if (candidate.q3->sources[3].generation == UINT64_MAX) {
        free(q3); free(records); return qa_browser_fail(error, "Stored Q3 favorite generation exhausted");
    }
    candidate.q3->sources[3].count = 0;
    ++candidate.q3->sources[3].generation;
    bool ok = true;
    for (uint32_t i = 0; ok && i < count; ++i) {
        uint32_t sources = qa_net_read_u32(&reader);
        qa_net_protocol_id protocol;
        protocol.kind = (qa_net_protocol)qa_net_read_u32(&reader);
        protocol.revision = qa_net_read_u32(&reader); protocol.flags = qa_net_read_u32(&reader);
        qa_net_address address;
        ok = !reader.failed && address_read(&reader, &address) && sources &&
            !(sources & ~(QA_SERVER_FAVORITE | QA_SERVER_DIRECT)) && qa_net_protocol_valid(protocol, error);
        sources &= source_mask;
        if (ok && sources) ok = qa_server_browser_add(&candidate, &address, protocol, sources, error);
    }
    if (ok) ok = qa_net_reader_finish(&reader);
    if (!ok) { free(q3); free(records); return false; }
    for (uint32_t i = 0; i < browser->capacity; ++i) {
        if (browser->records[i].q3_response.data != records[i].q3_response.data)
            qa_buffer_free(&browser->records[i].q3_response);
        if (browser->records[i].status_response.data != records[i].status_response.data)
            qa_buffer_free(&browser->records[i].status_response);
    }
    free(browser->records); free(browser->q3); browser->records = records; browser->q3 = q3; return true;
}
bool qa_server_browser_restore(qa_server_browser *browser, qa_bytes bytes, qa_error *error) {
    return restore_sources(browser, bytes, QA_SERVER_FAVORITE | QA_SERVER_DIRECT, error);
}
bool qa_server_browser_restore_favorites(qa_server_browser *browser, qa_bytes bytes, qa_error *error) {
    return restore_sources(browser, bytes, QA_SERVER_FAVORITE, error);
}

static bool browser_checkpoint_valid(const qa_server_browser *b)
{
    if (!b || b->callback || !qa_http_callbacks_idle(b->http) ||
        b->master_body.size > 1048576 || (b->master_body.size && !b->master_body.data) ||
        (!b->http_master && b->master_body.size) ||
        !b->capacity || b->capacity > 16384 || !b->records || !qa_browser_q3_valid(b) ||
        !service_address_valid(&b->broadcast) || !service_address_valid(&b->master) ||
        !qa_net_protocol_valid(b->broadcast_protocol, NULL) || !qa_net_protocol_valid(b->master_protocol, NULL) ||
        (b->broadcasting && (!b->broadcast_timeout || !b->broadcast_query)) ||
        (b->master_pending && !b->master_timeout)) return false;
    for (uint32_t i = 0; i < b->capacity; ++i) {
        const browser_record *r = &b->records[i]; const qa_server_entry *e = &r->entry;
        if (!qa_browser_status_response_valid(r) || (e->has_ping && !e->available) ||
            (!e->has_ping && e->ping_ns)) return false;
        if (!r->occupied) continue;
        if (!e->sources || (e->sources & ~31u) || !service_address_valid(&e->address) ||
            !qa_net_protocol_valid(e->protocol, NULL) || !memchr(e->name, 0, sizeof(e->name)) ||
            !memchr(e->map, 0, sizeof(e->map)) || !memchr(e->rules, 0, sizeof(e->rules)) ||
            (e->pending && (!r->query || !r->timeout_ns))) return false;
        for (uint32_t j = 0; j < i; ++j) {
            const qa_server_entry *other = &b->records[j].entry;
            if (!b->records[j].occupied) continue;
            if (qa_net_address_equal(&e->address, &other->address, true) &&
                ((e->protocol.kind == other->protocol.kind && e->protocol.revision == other->protocol.revision &&
                  e->protocol.flags == other->protocol.flags) || (e->pending && other->pending))) return false;
        }
    }
    return true;
}
bool qa_server_browser_checkpoint(const qa_server_browser *b, qa_buffer *out, qa_error *error)
{
    if (!out || !browser_checkpoint_valid(b) || !qa_browser_http_valid(b, error))
        return qa_browser_fail(error, "Browser continuation requires idle callbacks and valid service ownership");
    size_t fixed = 4194304 + (size_t)16384 * (12000 + 16384 + 65535) + 1048576 + 65536;
    if (b->q3->broadcast_count > (SIZE_MAX - fixed) / 16 ||
        b->q3->status_count > (SIZE_MAX - fixed - b->q3->broadcast_count * 16) / 160)
        return qa_browser_fail(error, "Q3 broadcasts exceed checkpoint allocation extent");
    size_t response_bytes = 0;
    for (uint32_t i = 0; i < b->capacity; ++i)
        response_bytes += b->records[i].q3_response.size + b->records[i].status_response.size;
    size_t capacity = 4194304 + b->q3->broadcast_count * 16 + b->q3->status_count * 160 + b->capacity + qa_server_browser_count(b) * 12000 + response_bytes +
        b->master_body.size + (b->master_url ? strlen(b->master_url) + 1 : 0);
    uint8_t *data = malloc(capacity);
    if (!data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Encoding browser continuation"); return false; }
    qa_net_writer w; qa_net_writer_init(&w, data, capacity, error);
    bool ok = qa_net_write_u32(&w, UINT32_C(0x42534151)) && qa_net_write_u32(&w, 5) &&
        qa_net_write_u32(&w, b->capacity) && qa_net_write_u64(&w, b->next_query) &&
        q3_save_address(&w, &b->broadcast) && q3_save_address(&w, &b->master) &&
        service_save_protocol(&w, b->broadcast_protocol) && service_save_protocol(&w, b->master_protocol) &&
        qa_net_write_u64(&w, b->broadcast_sent) && qa_net_write_u64(&w, b->broadcast_timeout) &&
        qa_net_write_u64(&w, b->master_sent) && qa_net_write_u64(&w, b->master_timeout) &&
        qa_net_write_u64(&w, b->broadcast_query) && qa_net_write_u8(&w, b->broadcasting) && qa_net_write_u8(&w, b->master_pending) &&
        qa_net_write_u64(&w, b->http_master) && qa_net_write_u32(&w, (uint32_t)b->master_body.size) &&
        qa_net_write_data(&w, b->master_body.data, b->master_body.size);
    if (ok && b->http_master) ok = qa_net_write_string(&w, b->master_url);
    for (uint32_t i = 0; ok && i < b->capacity; ++i) {
        const browser_record *r = &b->records[i]; const qa_server_entry *e = &r->entry;
        ok = qa_net_write_u8(&w, r->occupied); if (!ok || !r->occupied) continue;
        ok = q3_save_address(&w, &e->address) && service_save_protocol(&w, e->protocol) &&
            qa_net_write_u32(&w, e->sources) && qa_net_write_u32(&w, e->players) && qa_net_write_u32(&w, e->maximum_players) &&
            qa_net_write_u64(&w, e->updated_ns) && qa_net_write_u64(&w, e->ping_ns) &&
            qa_net_write_u8(&w, e->available) && qa_net_write_u8(&w, e->pending) && qa_net_write_u8(&w, e->timed_out) &&
            qa_net_write_u8(&w, e->has_ping) &&
            service_save_text(&w, e->name, sizeof(e->name)) && service_save_text(&w, e->map, sizeof(e->map)) &&
            service_save_text(&w, e->rules, sizeof(e->rules)) && qa_net_write_u64(&w, r->query) &&
            qa_net_write_u64(&w, r->sent_ns) && qa_net_write_u64(&w, r->timeout_ns) && qa_net_write_u64(&w, r->q3_order) &&
            qa_net_write_u32(&w, (uint32_t)r->q3_response.size) && qa_net_write_data(&w, r->q3_response.data, r->q3_response.size) &&
            qa_net_write_u32(&w, (uint32_t)r->status_response.size) && qa_net_write_data(&w, r->status_response.data, r->status_response.size);
    }
    if (ok) ok = qa_browser_q3_save(&w, b);
    if (!ok || w.failed) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&w)}; return true;
}
static bool browser_restore_checkpoint(qa_bytes bytes, qa_http *http, uint32_t capacity,
    const qa_browser_hooks *hooks, bool attach_http, qa_server_browser **out, qa_error *error)
{
    if (!out || *out || bytes.size > SIZE_MAX / 8 || (bytes.size && !bytes.data))
        return qa_browser_fail(error, "Invalid browser continuation output or extent");
    qa_net_reader r; qa_net_reader_init(&r, bytes, error);
    if (qa_net_read_u32(&r) != UINT32_C(0x42534151) || qa_net_read_u32(&r) != 5 || qa_net_read_u32(&r) != capacity)
        return qa_browser_fail(error, "Browser continuation schema or capacity differs");
    qa_server_browser *b = NULL;
    if (!qa_server_browser_create(http, capacity, hooks, &b, error)) return false;
    b->next_query = qa_net_read_u64(&r);
    bool ok = q3_restore_address(&r, &b->broadcast) && q3_restore_address(&r, &b->master) &&
        service_restore_protocol(&r, &b->broadcast_protocol) && service_restore_protocol(&r, &b->master_protocol);
    b->broadcast_sent = qa_net_read_u64(&r); b->broadcast_timeout = qa_net_read_u64(&r);
    b->master_sent = qa_net_read_u64(&r); b->master_timeout = qa_net_read_u64(&r); b->broadcast_query = qa_net_read_u64(&r);
    b->broadcasting = q3_save_bool(&r); b->master_pending = q3_save_bool(&r);
    b->http_master = qa_net_read_u64(&r);
    uint32_t body_size = qa_net_read_u32(&r);
    if (!r.failed && body_size <= 1048576 && body_size <= qa_net_reader_remaining(&r)) {
        if (body_size) {
            b->master_body.data = malloc((size_t)body_size + 1);
            if (!b->master_body.data) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "Restoring HTTP master prefix"); ok = false;
            } else {
                b->master_body.size = body_size;
                ok = ok && qa_net_read_data(&r, b->master_body.data, body_size);
                b->master_body.data[body_size] = 0;
            }
        }
    } else ok = false;
    if (ok && b->http_master) ok = service_restore_text(&r, &b->master_url, 65535);
    for (uint32_t i = 0; ok && !r.failed && i < capacity; ++i) {
        browser_record *record = &b->records[i]; qa_server_entry *e = &record->entry;
        record->occupied = q3_save_bool(&r); if (!record->occupied) continue;
        ok = q3_restore_address(&r, &e->address) && service_restore_protocol(&r, &e->protocol);
        e->sources = qa_net_read_u32(&r); e->players = qa_net_read_u32(&r); e->maximum_players = qa_net_read_u32(&r);
        e->updated_ns = qa_net_read_u64(&r); e->ping_ns = qa_net_read_u64(&r);
        e->available = q3_save_bool(&r); e->pending = q3_save_bool(&r); e->timed_out = q3_save_bool(&r);
        e->has_ping = q3_save_bool(&r);
        ok = ok && qa_net_read_string(&r, e->name, sizeof(e->name)) && qa_net_read_string(&r, e->map, sizeof(e->map)) &&
            qa_net_read_string(&r, e->rules, sizeof(e->rules));
        record->query = qa_net_read_u64(&r); record->sent_ns = qa_net_read_u64(&r); record->timeout_ns = qa_net_read_u64(&r);
        record->q3_order = qa_net_read_u64(&r);
        uint32_t response_size = qa_net_read_u32(&r);
        if (r.failed || response_size > 16384 || response_size > qa_net_reader_remaining(&r)) {
            ok = qa_net_reader_fail(&r, "Retained Q3 discovery datagram exceeds encoded extent"); break;
        }
        if (response_size) {
            record->q3_response.data = malloc(response_size);
            if (!record->q3_response.data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Importing retained Q3 response"); ok = false; break; }
            record->q3_response.size = response_size;
            ok = ok && qa_net_read_data(&r, record->q3_response.data, response_size);
        }
        response_size = qa_net_read_u32(&r);
        if (r.failed || response_size > 65535 || response_size > qa_net_reader_remaining(&r)) {
            ok = qa_net_reader_fail(&r, "Retained native discovery datagram exceeds encoded extent"); break;
        }
        if (response_size) {
            record->status_response.data = malloc(response_size);
            if (!record->status_response.data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Importing retained native response"); ok = false; break; }
            record->status_response.size = response_size;
            ok = ok && qa_net_read_data(&r, record->status_response.data, response_size);
        }
    }
    if (ok) ok = qa_browser_q3_restore(&r, b);
    if (!ok || !qa_net_reader_finish(&r) || !browser_checkpoint_valid(b) ||
        (attach_http && !qa_browser_restore_http(b, error))) {
        qa_server_browser_destroy(b); return qa_browser_fail(error, "Invalid browser continuation fields");
    }
    *out = b; return true;
}
bool qa_server_browser_restore_checkpoint(qa_bytes bytes,qa_http *http,uint32_t capacity,
    const qa_browser_hooks *hooks,qa_server_browser **out,qa_error *error)
{
    return browser_restore_checkpoint(bytes,http,capacity,hooks,true,out,error);
}
bool qa_server_browser_restore_checkpoint_staged(qa_bytes bytes,qa_http *http,uint32_t capacity,
    const qa_browser_hooks *hooks,qa_server_browser **out,qa_error *error)
{
    return browser_restore_checkpoint(bytes,http,capacity,hooks,false,out,error);
}
bool qa_server_browser_restore_checkpoint_finish(qa_server_browser *browser,qa_error *error)
{
    return browser && browser_checkpoint_valid(browser) && qa_browser_restore_http(browser,error);
}
bool qa_server_browser_http_handoff_ready(const qa_server_browser *active,
    const qa_server_browser *candidate, qa_error *error)
{
    if (!active || !candidate || active == candidate || !browser_checkpoint_valid(active) ||
        !browser_checkpoint_valid(candidate) || !qa_browser_http_valid(active, error) || !qa_browser_http_valid(candidate, error))
        return qa_browser_fail(error, "HTTP master handoff requires idle genuine consumers");
    if (!active->http_master && !candidate->http_master) return true;
    return (active->http_master == candidate->http_master &&
        active->master_protocol.kind == candidate->master_protocol.kind &&
        active->master_protocol.revision == candidate->master_protocol.revision &&
        active->master_protocol.flags == candidate->master_protocol.flags &&
        active->master_sent == candidate->master_sent && active->master_url && candidate->master_url &&
        !strcmp(active->master_url, candidate->master_url) &&
        active->master_body.size == candidate->master_body.size &&
        (!active->master_body.size || !memcmp(active->master_body.data, candidate->master_body.data, active->master_body.size))) ||
        qa_browser_fail(error, "Live HTTP master advanced beyond its saved consumer body and request cut");
}
