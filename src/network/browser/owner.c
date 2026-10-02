#include "internal.h"
#include "qa/http_save.h"
#include "qa/network_q2.h"
#include <stdlib.h>
#include <string.h>

bool qa_browser_fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_FORMAT, 0, "%s", message); return false;
}
static bool protocol_equal(qa_net_protocol_id a, qa_net_protocol_id b) {
    return a.kind == b.kind && a.revision == b.revision && a.flags == b.flags;
}
browser_record *qa_browser_find(qa_server_browser *browser, const qa_net_address *address, qa_net_protocol_id protocol) {
    for (uint32_t i = 0; i < browser->capacity; ++i)
        if (browser->records[i].occupied && protocol_equal(browser->records[i].entry.protocol, protocol) &&
            qa_net_address_equal(&browser->records[i].entry.address, address, true)) return &browser->records[i];
    return NULL;
}
void qa_browser_changed(qa_server_browser *browser, const qa_server_entry *entry) {
    if (browser->hooks.changed) {
        bool previous = browser->callback; browser->callback = true;
        browser->hooks.changed(browser->hooks.context, entry); browser->callback = previous;
    }
}
bool qa_server_browser_create(qa_http *http, uint32_t capacity, const qa_browser_hooks *hooks,
                               qa_server_browser **out, qa_error *error) {
    if (!http || !capacity || capacity > 16384 || !hooks || !hooks->send || !out)
        return qa_browser_fail(error, "Invalid browser owner options");
    qa_server_browser *browser = calloc(1, sizeof(*browser));
    if (!browser) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating browser"); return false; }
    browser->records = calloc(capacity, sizeof(*browser->records));
    if (!browser->records) { free(browser); qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating browser entries"); return false; }
    browser->q3 = calloc(1, sizeof(*browser->q3));
    if (!browser->q3) { free(browser->records); free(browser); qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating Q3 browser continuation"); return false; }
    browser->q3->master_source = -1;
    browser->q3->next_entry_order = 1;
    browser->http = http; browser->capacity = capacity; browser->hooks = *hooks; browser->next_query = 1;
    *out = browser; return true;
}
void qa_server_browser_cancel_master(qa_server_browser *browser) {
    if (!browser || browser->callback) return;
    qa_http_cancel(browser->http, browser->http_master); browser->http_master = 0;
    browser->master_pending = false; qa_buffer_free(&browser->master_body);
    free(browser->master_url); browser->master_url = NULL;
}
void qa_server_browser_destroy(qa_server_browser *browser) {
    if (!browser || browser->callback) return;
    qa_server_browser_cancel_master(browser);
    for (uint32_t i = 0; i < browser->capacity; ++i) {
        qa_buffer_free(&browser->records[i].q3_response);
        qa_buffer_free(&browser->records[i].status_response);
    }
    free(browser->q3->broadcasts); free(browser->q3->status_queue); free(browser->q3); free(browser->records); free(browser);
}
bool qa_server_browser_add(qa_server_browser *browser, const qa_net_address *address,
                            qa_net_protocol_id protocol, uint32_t sources, qa_error *error) {
    if (!browser || browser->callback || !address || !sources || (sources & ~31u) ||
        !qa_net_protocol_valid(protocol, error)) return qa_browser_fail(error, "Invalid browser entry");
    browser_record *record = qa_browser_find(browser, address, protocol);
    bool fresh = !record;
    if (!record) {
        for (uint32_t i = 0; i < browser->capacity; ++i) if (!browser->records[i].occupied) { record = &browser->records[i]; break; }
        if (!record) return qa_browser_fail(error, "Server browser capacity exhausted");
        if (protocol.kind == QA_NET_Q3_68 && !browser->q3->next_entry_order)
            return qa_browser_fail(error, "Q3 entry insertion order exhausted");
        *record = (browser_record){.occupied = true, .entry = {.address = *address, .protocol = protocol}};
        if (protocol.kind == QA_NET_Q3_68) record->q3_order = browser->q3->next_entry_order++;
    }
    if (protocol.kind == QA_NET_Q3_68 && !qa_browser_q3_membership(browser, address,
        record->entry.sources, record->entry.sources | sources, error)) {
        if (fresh) memset(record, 0, sizeof(*record));
        return false;
    }
    record->entry.sources |= sources; qa_browser_changed(browser, &record->entry); return true;
}
bool qa_server_browser_remove_source(qa_server_browser *browser, const qa_net_address *address,
                                      qa_net_protocol_id protocol, uint32_t source, qa_error *error) {
    if (!browser || browser->callback || !address) return qa_browser_fail(error, "Invalid source removal");
    browser_record *record = qa_browser_find(browser, address, protocol); if (!record) return true;
    if (protocol.kind == QA_NET_Q3_68 && !qa_browser_q3_membership(browser, address,
        record->entry.sources, record->entry.sources & ~source, error)) return false;
    record->entry.sources &= ~source;
    qa_server_entry previous = record->entry;
    if (!record->entry.sources) {
        qa_buffer_free(&record->q3_response); qa_buffer_free(&record->status_response);
        memset(record, 0, sizeof(*record));
    }
    qa_browser_changed(browser, &previous); return true;
}
bool qa_server_browser_query(qa_server_browser *browser, const qa_net_address *address,
                              qa_net_protocol_id protocol, bool broadcast, uint64_t now, uint64_t timeout, qa_error *error) {
    if (!browser || browser->callback || !address || !timeout || !browser->next_query ||
        (broadcast && !browser->hooks.local) ||
        !qa_net_protocol_valid(protocol, error)) return qa_browser_fail(error, "Invalid browser query");
    /* Challenge-less native dialects have one pending query per endpoint. */
    if (!broadcast) for (uint32_t i = 0; i < browser->capacity; ++i) {
        if (browser->records[i].occupied && browser->records[i].entry.pending &&
            qa_net_address_equal(&browser->records[i].entry.address, address, true)) {
            if (protocol.kind == QA_NET_Q3_68 && browser->records[i].entry.protocol.kind == QA_NET_Q3_68)
                browser->records[i].entry.pending = false;
            else return qa_browser_fail(error, "Endpoint already has a pending discovery query");
        }
    }
    uint64_t query = browser->next_query++;
    uint8_t bytes[256]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    if (!qa_browser_query_encode(protocol, query, &writer)) return false;
    if (!broadcast && !(protocol.kind == QA_NET_Q3_68 && qa_browser_find(browser, address, protocol)) &&
        !qa_server_browser_add(browser, address, protocol, QA_SERVER_DIRECT, error)) return false;
    browser->callback = true;
    bool ok = browser->hooks.send(browser->hooks.context, address, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
    browser->callback = false; if (!ok) return false;
    if (broadcast) {
        browser->broadcast = *address; browser->broadcast_protocol = protocol;
        browser->broadcast_sent = now; browser->broadcast_timeout = timeout;
        browser->broadcast_query = query; browser->broadcasting = true;
    } else {
        browser_record *record = qa_browser_find(browser, address, protocol);
        record->entry.pending = true; record->entry.timed_out = false;
        record->query = query; record->sent_ns = now; record->timeout_ns = timeout;
    }
    return true;
}
void qa_server_browser_expire(qa_server_browser *browser, uint64_t now) {
    if (!browser || browser->callback) return;
    for (uint32_t i = 0; i < browser->capacity; ++i) {
        browser_record *record = &browser->records[i];
        if (record->occupied && record->entry.pending && now >= record->sent_ns &&
            now - record->sent_ns >= record->timeout_ns) {
            record->entry.pending = false; record->entry.timed_out = true; qa_browser_changed(browser, &record->entry);
        }
    }
    if (browser->broadcasting && now >= browser->broadcast_sent && now - browser->broadcast_sent >= browser->broadcast_timeout)
        browser->broadcasting = false;
    if (browser->master_pending && now >= browser->master_sent && now - browser->master_sent >= browser->master_timeout)
        browser->master_pending = false;
    qa_browser_q3_expire(browser, now);
}
bool qa_server_browser_receive(qa_server_browser *browser, const qa_net_datagram *packet,
                                bool *recognized, qa_error *error) {
    if (!browser || browser->callback || !packet || !recognized) return qa_browser_fail(error, "Invalid discovery delivery");
    *recognized = false; if (packet->kind != QA_NET_POLL_PACKET) return true;
    if (browser->master_pending && qa_net_address_equal(&browser->master, &packet->from, true)) {
        qa_net_address addresses[256]; size_t count; bool complete; qa_error ignored = {0};
        if (qa_browser_master_decode(packet->payload, browser->master_protocol, addresses, 256, &count, &complete, &ignored)) {
            uint32_t source = browser->master_protocol.kind == QA_NET_Q3_68 && browser->q3->master_source >= 0 ?
                qa_browser_q3_source_bit(browser->q3->master_source) : QA_SERVER_MASTER;
            bool malformed = false;
            for (size_t i = 0; i < count; ++i) {
                if (!addresses[i].port) {
                    if (browser->master_protocol.kind == QA_NET_Q3_68 && browser->q3->master_source >= 0) { malformed = true; break; }
                    continue;
                }
                if (browser->master_protocol.kind == QA_NET_Q3_68 && browser->q3->master_source >= 0 &&
                    browser->q3->sources[browser->q3->master_source].count >=
                    (browser->q3->master_source == 2 ? 8192u : 128u)) break;
                if (!qa_server_browser_add(browser, &addresses[i], browser->master_protocol, source, error)) return false;
                if (!qa_browser_status_enqueue(browser, &addresses[i], browser->master_protocol, error)) return false;
            }
            if (!malformed) {
                browser->q3->master_received = true;
                if (complete) browser->master_pending = false;
            }
            *recognized = true; return true;
        }
    }
    if (!qa_browser_q3_receive(browser, packet, recognized, error)) return false;
    if (*recognized) return true;
    browser_record *target = NULL;
    for (uint32_t i = 0; i < browser->capacity; ++i)
        if (browser->records[i].occupied && browser->records[i].entry.pending &&
            qa_net_address_equal(&browser->records[i].entry.address, &packet->from, true)) { target = &browser->records[i]; break; }
    qa_net_protocol_id protocol;
    uint64_t sent, timeout, query;
    if (target) { protocol = target->entry.protocol; sent = target->sent_ns; timeout = target->timeout_ns; query = target->query; }
    else if (browser->broadcasting) {
        protocol = browser->broadcast_protocol; sent = browser->broadcast_sent;
        timeout = browser->broadcast_timeout; query = browser->broadcast_query;
    } else return true;
    if (packet->received_ns < sent || packet->received_ns - sent >= timeout) return true;
    qa_server_entry decoded; uint64_t challenge; qa_error ignored = {0};
    if (!qa_browser_status_decode(packet->payload, protocol, &decoded, &challenge, &ignored)) return true;
    if (protocol.kind == QA_NET_Q3_68 && challenge != query) return true;
    if (!target) {
        browser->callback = true;
        bool local = browser->hooks.local && browser->hooks.local(browser->hooks.context, &packet->from);
        browser->callback = false;
        if (!local) return true;
        if (!qa_server_browser_add(browser, &packet->from, protocol, QA_SERVER_LAN, error)) return false;
        target = qa_browser_find(browser, &packet->from, protocol);
    }
    decoded.address = target->entry.address; decoded.sources = target->entry.sources;
    decoded.updated_ns = packet->received_ns; decoded.ping_ns = packet->received_ns - sent;
    if (protocol.kind == QA_NET_Q3_68 && !qa_browser_q3_store_response(target, packet->payload, error)) return false;
    if (protocol.kind != QA_NET_Q3_68 && !qa_browser_status_store_response(target, packet->payload, error)) return false;
    decoded.available = true; decoded.has_ping = true; target->entry = decoded;
    qa_browser_changed(browser, &target->entry); *recognized = true; return true;
}
bool qa_server_browser_master_udp(qa_server_browser *browser, const qa_net_address *address,
                                   qa_net_protocol_id protocol, uint64_t now, uint64_t timeout, qa_error *error) {
    if (!browser || browser->callback || !address || !timeout || !qa_net_protocol_valid(protocol, error))
        return qa_browser_fail(error, "Invalid master query");
    const char *text = protocol.kind == QA_NET_Q3_68 ? "getservers 68 empty full" :
        (protocol.kind == QA_NET_Q2_34 || protocol.kind == QA_NET_R1Q2_35 || protocol.kind == QA_NET_Q2PRO_36 ||
         protocol.kind == QA_NET_Q2REPRO_1038 || protocol.kind == QA_NET_Q2PRIVATE_4038) ? "query" : NULL;
    if (!text) return qa_browser_fail(error, "Selected native master uses HTTP or LAN discovery");
    uint8_t bytes[128]; qa_net_writer writer; qa_net_writer_init(&writer, bytes, sizeof(bytes), error);
    if (!qa_q2_oob_write(&writer, text)) return false;
    browser->callback = true;
    bool ok = browser->hooks.send(browser->hooks.context, address, (qa_bytes){bytes, qa_net_writer_size(&writer)}, error);
    browser->callback = false; if (!ok) return false;
    qa_server_browser_cancel_master(browser);
    browser->master = *address; browser->master_protocol = protocol;
    browser->q3->master_source = -1; browser->q3->master_received = false;
    browser->master_sent = now; browser->master_timeout = timeout; browser->master_pending = true; return true;
}
static bool http_headers(void *context, qa_http_request_id id, const qa_http_response *response, qa_error *error) {
    (void)context; (void)id;
    return (response->status >= 300 && response->status < 400) || response->status == 200 ||
        qa_browser_fail(error, "HTTP master list request failed");
}
static bool http_body(void *context, qa_http_request_id id, const qa_http_response *response, qa_bytes bytes, qa_error *error) {
    qa_server_browser *browser = context; (void)id;
    if (response->status != 200) return true;
    if (bytes.size > 1048576 - browser->master_body.size || (bytes.size && !bytes.data))
        return qa_browser_fail(error, "HTTP master list exceeds 1 MiB");
    size_t size = browser->master_body.size + bytes.size;
    uint8_t *data = realloc(browser->master_body.data, size + 1);
    if (!data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining master list"); return false; }
    if (bytes.size) memcpy(data + browser->master_body.size, bytes.data, bytes.size);
    data[size] = 0; browser->master_body = (qa_buffer){data, size}; return true;
}
static void http_complete(void *context, qa_http_request_id id, const qa_http_response *response, const qa_error *failure) {
    qa_server_browser *browser = context; (void)id; browser->http_master = 0;
    qa_error error = {0}; if (failure) error = *failure;
    if (error.code == QA_OK && response->status != 200) qa_browser_fail(&error, "HTTP master did not complete");
    if (error.code == QA_OK && browser->master_body.size) {
        char *cursor = (char *)browser->master_body.data;
        char *end = cursor + browser->master_body.size;
        while (cursor < end && error.code == QA_OK) {
            char *start = cursor;
            while (cursor < end && *cursor && *cursor != '\n' && *cursor != '\r') ++cursor;
            if (cursor < end && !*cursor) { qa_browser_fail(&error, "Master list contains a NUL byte"); break; }
            char *stop = cursor;
            while (start < stop && (*start == ' ' || *start == '\t')) ++start;
            while (stop > start && (stop[-1] == ' ' || stop[-1] == '\t')) --stop;
            if (cursor < end) ++cursor;
            *stop = 0;
            if (start < stop && *start != '#') {
                qa_net_address address;
                uint16_t port = browser->master_protocol.kind == QA_NET_Q3_68 ? 27960 :
                    browser->master_protocol.kind <= QA_NET_RMQ999 ? 26000 :
                    browser->master_protocol.kind <= QA_NET_QW29 ? 27500 : 27910;
                if ((size_t)(stop-start)>255) { qa_browser_fail(&error,"Master list address exceeds its admitted extent"); break; }
                if (!qa_net_address_resolve(start, port, 0, &address, &error) ||
                    !qa_server_browser_add(browser, &address, browser->master_protocol, QA_SERVER_MASTER, &error) ||
                    !qa_browser_status_enqueue(browser, &address, browser->master_protocol, &error)) break;
            }
        }
    }
    qa_buffer_free(&browser->master_body);
    free(browser->master_url); browser->master_url = NULL;
    if (browser->hooks.master_complete) {
        browser->callback = true;
        browser->hooks.master_complete(browser->hooks.context, error.code == QA_OK ? NULL : &error);
        browser->callback = false;
    }
}
bool qa_browser_restore_http(qa_server_browser *browser, qa_error *error)
{
    if (!browser->http_master) return true;
    if (!qa_browser_http_valid(browser, error)) return false;
    qa_http_callbacks callbacks = {browser, http_headers, http_body, http_complete};
    return qa_http_restore_callbacks(browser->http, browser->http_master, &callbacks, error);
}
bool qa_browser_http_valid(const qa_server_browser *browser, qa_error *error)
{
    if (!browser->http_master) return !browser->master_url && !browser->master_body.size;
    qa_http_continuation_view view;
    if (!browser->master_url || !qa_http_continuation_read(browser->http, browser->http_master, &view, error)) return false;
    uint64_t entity_bytes = view.response.bytes - view.response_body_start;
    return (!strcmp(view.url, browser->master_url) && !strcmp(view.method, "GET") &&
        !view.request_body.size && !view.header_count && view.timeout_ms == 15000 &&
        view.connect_timeout_ms == 5000 && view.maximum_redirects == 5 &&
        view.maximum_response_bytes == 1048576 && !view.canceled && view.failure.code == QA_OK &&
        browser->master_body.size == (view.response.status == 200 ? entity_bytes : 0)) ||
        qa_browser_fail(error, "HTTP master consumer differs from its actual recipe and received prefix");
}
bool qa_server_browser_master_http(qa_server_browser *browser, const char *url,
                                    qa_net_protocol_id protocol, uint64_t now, qa_error *error) {
    if (!browser || browser->callback || !url || strlen(url) > 65535 || !qa_net_protocol_valid(protocol, error))
        return qa_browser_fail(error, "Invalid HTTP master query");
    qa_server_browser_cancel_master(browser);
    browser->master_url = malloc(strlen(url) + 1);
    if (!browser->master_url) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining HTTP master address"); return false; }
    strcpy(browser->master_url, url);
    browser->master_protocol = protocol; browser->master_sent = now;
    browser->q3->master_source = -1; browser->q3->master_received = false;
    qa_http_request request = {.url = url, .method = "GET", .maximum_response_bytes = 1048576,
        .timeout_ms = 15000, .connect_timeout_ms = 5000, .maximum_redirects = 5,
        .callbacks = {browser, http_headers, http_body, http_complete}};
    if (qa_http_submit(browser->http, &request, &browser->http_master, error)) return true;
    free(browser->master_url); browser->master_url = NULL; return false;
}
size_t qa_server_browser_count(const qa_server_browser *browser) {
    size_t count = 0;
    if (browser) for (uint32_t i = 0; i < browser->capacity; ++i) if (browser->records[i].occupied) ++count;
    return count;
}
bool qa_server_browser_at(const qa_server_browser *browser, size_t index, qa_server_entry *out) {
    if (!browser || !out || index >= browser->capacity || !browser->records[index].occupied) return false;
    *out = browser->records[index].entry; return true;
}
static bool contains(const char *text, const char *needle) {
    return !needle || !*needle || strstr(text, needle) != NULL;
}
static int compare(const qa_server_entry *a, const qa_server_entry *b, qa_browser_sort sort) {
    if (sort == QA_BROWSER_PING) {
        if (a->available != b->available) return a->available ? -1 : 1;
        if (a->ping_ns != b->ping_ns) return a->ping_ns < b->ping_ns ? -1 : 1;
    } else if (sort == QA_BROWSER_PLAYERS && a->players != b->players) return a->players < b->players ? -1 : 1;
    int value = strcmp(sort == QA_BROWSER_MAP ? a->map : a->name, sort == QA_BROWSER_MAP ? b->map : b->name);
    if (value) return value;
    char left[256], right[256];
    if (qa_net_address_format(&a->address, left, sizeof(left), NULL) && qa_net_address_format(&b->address, right, sizeof(right), NULL))
        return strcmp(left, right);
    return 0;
}
bool qa_server_browser_list(const qa_server_browser *browser, const qa_browser_filter *filter,
                             uint32_t *indices, size_t capacity, size_t *count, qa_error *error) {
    if (!browser || !filter || !count || (capacity && !indices) || filter->sort > QA_BROWSER_MAP)
        return qa_browser_fail(error, "Invalid browser list request");
    uint32_t *all = malloc(browser->capacity * sizeof(*all));
    if (!all) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating browser ordering"); return false; }
    size_t size = 0;
    for (uint32_t i = 0; i < browser->capacity; ++i) {
        const qa_server_entry *entry = &browser->records[i].entry;
        if (!browser->records[i].occupied || (filter->favorites_only && !(entry->sources & QA_SERVER_FAVORITE)) ||
            (filter->hide_empty && !entry->players) || (filter->hide_full && entry->maximum_players && entry->players >= entry->maximum_players) ||
            (!contains(entry->name, filter->text) && !contains(entry->map, filter->text))) continue;
        size_t position = size;
        while (position) {
            int order = compare(entry, &browser->records[all[position - 1]].entry, filter->sort);
            if (filter->descending) order = -order;
            if (order >= 0) break;
            all[position] = all[position - 1]; --position;
        }
        all[position] = i; ++size;
    }
    size_t copied = size < capacity ? size : capacity;
    if (copied) memcpy(indices, all, copied * sizeof(*indices));
    free(all); *count = size; return true;
}
