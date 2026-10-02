#ifndef QA_SERVER_BROWSER_Q3_H
#define QA_SERVER_BROWSER_Q3_H
#include "qa/server_browser.h"

typedef uint64_t qa_browser_q3_request_id;
typedef enum qa_browser_q3_request_kind { QA_BROWSER_Q3_INFO, QA_BROWSER_Q3_STATUS } qa_browser_q3_request_kind;
typedef struct qa_browser_q3_player { int32_t score, ping; uint16_t name; } qa_browser_q3_player;
typedef struct qa_browser_q3_result {
    qa_net_address address;
    qa_browser_q3_request_kind kind, response_kind;
    uint64_t sent_ns, completed_ns;
    bool completed;
    qa_server_entry entry;
    /* Source status rules followed by canonical score/ping/name rows. */
    char status[8192];
    uint32_t player_count, names_size;
    qa_browser_q3_player players[1024];
    char names[16384];
} qa_browser_q3_result;
typedef struct qa_browser_q3_list {
    const qa_net_address *addresses;
    uint32_t count;
    uint64_t generation, reset_generation;
    bool pending;
} qa_browser_q3_list;
typedef struct qa_browser_q3_cached_entry {
    qa_server_entry entry;
    /* Borrowed immutable datagram that produced the complete status. */
    qa_bytes response;
    uint64_t insertion_order;
} qa_browser_q3_cached_entry;

/* Retained requests have no automatic timeout. Release retires the exact
 * challenge/result; subsequent datagrams cannot fill a reused request. */
bool qa_server_browser_q3_request(qa_server_browser *, const qa_net_address *,
    qa_browser_q3_request_kind, uint64_t now_ns, qa_browser_q3_request_id *, qa_error *);
bool qa_server_browser_q3_result(const qa_server_browser *, qa_browser_q3_request_id,
    const qa_browser_q3_result **, qa_error *);
bool qa_server_browser_q3_release(qa_server_browser *, qa_browser_q3_request_id, qa_error *);
bool qa_server_browser_q3_list_read(const qa_server_browser *, int32_t source,
    qa_browser_q3_list *, qa_error *);
bool qa_server_browser_q3_clear(qa_server_browser *, int32_t source, qa_error *);
bool qa_server_browser_q3_scan(qa_server_browser *, uint64_t now_ns, qa_error *);
bool qa_server_browser_q3_master(qa_server_browser *, int32_t source,
    const qa_net_address *, int32_t protocol, const char *const *keywords,
    size_t keyword_count, uint64_t now_ns, qa_error *);
bool qa_server_browser_q3_pump(qa_server_browser *, uint64_t now_ns, qa_error *);
bool qa_server_browser_q3_entry(const qa_server_browser *, const qa_net_address *, qa_server_entry *);
bool qa_server_browser_q3_cached_entry_read(const qa_server_browser *, const qa_net_address *,
    qa_browser_q3_cached_entry *, qa_error *);
bool qa_server_browser_q3_cached_entry_validate(const qa_browser_q3_cached_entry *, qa_error *);
bool qa_server_browser_q3_restore_entry(qa_server_browser *, const qa_browser_q3_cached_entry *, qa_error *);
#endif
