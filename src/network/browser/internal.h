#ifndef QA_BROWSER_INTERNAL_H
#define QA_BROWSER_INTERNAL_H
#include "qa/server_browser.h"
#include "qa/server_browser_q3.h"

typedef struct browser_q3_request {
    qa_browser_q3_request_id id;
    qa_browser_q3_result result;
} browser_q3_request;
typedef struct browser_q3_source {
    qa_net_address addresses[8192];
    uint32_t count;
    uint64_t generation, reset_generation;
} browser_q3_source;
typedef struct browser_q3_broadcast { uint64_t query, sent_ns; } browser_q3_broadcast;
typedef struct browser_status_query { qa_net_address address; qa_net_protocol_id protocol; } browser_status_query;
typedef struct browser_q3 {
    uint64_t next_entry_order;
    browser_q3_source sources[4];
    browser_q3_request requests[48];
    browser_q3_broadcast *broadcasts;
    size_t broadcast_count;
    browser_status_query *status_queue;
    size_t status_count;
    bool master_received;
    int32_t master_source;
} browser_q3;

typedef struct browser_record {
    qa_server_entry entry;
    qa_buffer q3_response;
    qa_buffer status_response;
    uint64_t q3_order;
    uint64_t query, sent_ns, timeout_ns;
    bool occupied;
} browser_record;
struct qa_server_browser {
    qa_http *http;
    qa_browser_hooks hooks;
    browser_record *records;
    browser_q3 *q3;
    uint32_t capacity;
    uint64_t next_query;
    qa_net_address broadcast, master;
    qa_net_protocol_id broadcast_protocol, master_protocol;
    uint64_t broadcast_sent, broadcast_timeout, master_sent, master_timeout, broadcast_query;
    bool broadcasting, master_pending, callback;
    qa_http_request_id http_master;
    qa_buffer master_body;
    char *master_url;
};
bool qa_browser_fail(qa_error *, const char *);
browser_record *qa_browser_find(qa_server_browser *, const qa_net_address *, qa_net_protocol_id);
void qa_browser_changed(qa_server_browser *, const qa_server_entry *);
bool qa_browser_restore_http(qa_server_browser *, qa_error *);
bool qa_browser_http_valid(const qa_server_browser *, qa_error *);
bool qa_browser_query_encode(qa_net_protocol_id, uint64_t challenge, qa_net_writer *);
bool qa_browser_status_decode(qa_bytes, qa_net_protocol_id, qa_server_entry *, uint64_t *challenge, qa_error *);
bool qa_browser_status_store_response(browser_record *, qa_bytes, qa_error *);
bool qa_browser_status_response_valid(const browser_record *);
bool qa_browser_master_decode(qa_bytes, qa_net_protocol_id, qa_net_address *, size_t, size_t *, bool *complete, qa_error *);
uint32_t qa_browser_q3_source_bit(int32_t);
bool qa_browser_q3_membership(qa_server_browser *, const qa_net_address *, uint32_t before, uint32_t after, qa_error *);
bool qa_browser_q3_receive(qa_server_browser *, const qa_net_datagram *, bool *, qa_error *);
bool qa_browser_q3_decode(qa_bytes, qa_browser_q3_result *, uint64_t *, qa_error *);
bool qa_browser_q3_store_response(browser_record *, qa_bytes, qa_error *);
bool qa_browser_q3_response_valid(const browser_record *);
bool qa_browser_q3_valid(const qa_server_browser *);
bool qa_browser_q3_save(qa_net_writer *, const qa_server_browser *);
bool qa_browser_q3_restore(qa_net_reader *, qa_server_browser *);
void qa_browser_q3_expire(qa_server_browser *, uint64_t);
bool qa_browser_q3_enqueue(qa_server_browser *, const qa_net_address *, qa_error *);
bool qa_browser_status_enqueue(qa_server_browser *, const qa_net_address *, qa_net_protocol_id, qa_error *);
#endif
