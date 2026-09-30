#ifndef QA_SERVER_BROWSER_H
#define QA_SERVER_BROWSER_H
#include "qa/network.h"
#include "qa/http.h"

typedef struct qa_server_browser qa_server_browser;
enum qa_server_source {
    QA_SERVER_LAN = 1u, QA_SERVER_MASTER = 2u, QA_SERVER_FAVORITE = 4u, QA_SERVER_DIRECT = 8u
};
typedef struct qa_server_entry {
    qa_net_address address;
    qa_net_protocol_id protocol;
    uint32_t sources, players, maximum_players;
    uint64_t updated_ns, ping_ns;
    bool available, pending, timed_out;
    char name[1025], map[256], rules[8193];
} qa_server_entry;
typedef struct qa_browser_hooks {
    void *context;
    bool (*send)(void *, const qa_net_address *, qa_bytes, qa_error *);
    /* Required for broadcast discovery. Interface/subnet authority belongs to
     * the transport owner; private address ranges alone do not prove locality. */
    bool (*local)(void *, const qa_net_address *);
    void (*changed)(void *, const qa_server_entry *);
    void (*master_complete)(void *, const qa_error *);
} qa_browser_hooks;
/* Borrows HTTP. The application routes connectionless datagrams here through
 * its one receive owner. Selected protocol remains explicit for every entry. */
bool qa_server_browser_create(qa_http *, uint32_t capacity, const qa_browser_hooks *,
                               qa_server_browser **, qa_error *);
void qa_server_browser_destroy(qa_server_browser *);
bool qa_server_browser_add(qa_server_browser *, const qa_net_address *, qa_net_protocol_id,
                            uint32_t sources, qa_error *);
bool qa_server_browser_remove_source(qa_server_browser *, const qa_net_address *,
                                      qa_net_protocol_id, uint32_t source, qa_error *);
bool qa_server_browser_query(qa_server_browser *, const qa_net_address *, qa_net_protocol_id,
                              bool broadcast, uint64_t now_ns, uint64_t timeout_ns, qa_error *);
bool qa_server_browser_receive(qa_server_browser *, const qa_net_datagram *, bool *recognized, qa_error *);
void qa_server_browser_expire(qa_server_browser *, uint64_t now_ns);
bool qa_server_browser_master_udp(qa_server_browser *, const qa_net_address *, qa_net_protocol_id,
                                   uint64_t now_ns, uint64_t timeout_ns, qa_error *);
bool qa_server_browser_master_http(qa_server_browser *, const char *url, qa_net_protocol_id,
                                    uint64_t now_ns, qa_error *);
void qa_server_browser_cancel_master(qa_server_browser *);
size_t qa_server_browser_count(const qa_server_browser *);
bool qa_server_browser_at(const qa_server_browser *, size_t, qa_server_entry *);
/* Filter/sort indices refer to entry storage until add/remove. Returns required
 * count even when capacity is smaller. stable ordering uses address bytes. */
typedef enum qa_browser_sort { QA_BROWSER_NAME, QA_BROWSER_PING, QA_BROWSER_PLAYERS, QA_BROWSER_MAP } qa_browser_sort;
typedef struct qa_browser_filter { const char *text; bool hide_empty, hide_full, favorites_only; qa_browser_sort sort; bool descending; } qa_browser_filter;
bool qa_server_browser_list(const qa_server_browser *, const qa_browser_filter *,
                             uint32_t *indices, size_t capacity, size_t *count, qa_error *);
/* Persistent favorite/direct identities use explicit endian-aware encoding. */
bool qa_server_browser_save(const qa_server_browser *, qa_buffer *, qa_error *);
bool qa_server_browser_restore(qa_server_browser *, qa_bytes, qa_error *);
#endif
