#ifndef QA_BROWSER_INTERNAL_H
#define QA_BROWSER_INTERNAL_H
#include "qa/server_browser.h"

typedef struct browser_record {
    qa_server_entry entry;
    uint64_t query, sent_ns, timeout_ns;
    bool occupied;
} browser_record;
struct qa_server_browser {
    qa_http *http;
    qa_browser_hooks hooks;
    browser_record *records;
    uint32_t capacity;
    uint64_t next_query;
    qa_net_address broadcast, master;
    qa_net_protocol_id broadcast_protocol, master_protocol;
    uint64_t broadcast_sent, broadcast_timeout, master_sent, master_timeout, broadcast_query;
    bool broadcasting, master_pending, callback;
    qa_http_request_id http_master;
    qa_buffer master_body;
};
bool qa_browser_fail(qa_error *, const char *);
bool qa_browser_query_encode(qa_net_protocol_id, uint64_t challenge, qa_net_writer *);
bool qa_browser_status_decode(qa_bytes, qa_net_protocol_id, qa_server_entry *, uint64_t *challenge, qa_error *);
bool qa_browser_master_decode(qa_bytes, qa_net_protocol_id, qa_net_address *, size_t, size_t *, bool *complete, qa_error *);
#endif
