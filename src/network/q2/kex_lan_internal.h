#ifndef QA_KEX_LAN_INTERNAL_H
#define QA_KEX_LAN_INTERNAL_H

#include "qa/network_q2_kex.h"

struct attributes {
    qa_kex_attribute *data;
    size_t count;
};
struct player {
    uint64_t id;
    struct attributes attributes;
};
struct peer {
    qa_kex_lan *owner;
    qa_net_address address;
    qa_kex_channel *channel;
    uint64_t players[8];
    size_t count;
};
struct queued {
    struct queued *next;
    qa_net_address address;
    uint64_t received;
    size_t size;
    qa_net_poll_kind kind;
    uint8_t bytes[];
};
struct qa_kex_lan {
    qa_net_transport *transport;
    qa_net_address local_address;
    qa_kex_lan_options options;
    char name[1025];
    struct peer *peers[256];
    size_t peer_count;
    struct player players[255];
    size_t player_count;
    struct attributes attributes;
    uint64_t next_id, clock, retry_at;
    bool joined, retried, entered;
    uint8_t local_first;
    uint64_t local_ids[8];
    struct queued *head, *tail, *borrowed;
    size_t queued_count;
    bool dropped;
    qa_net_address dropped_from;
};

bool qa_kex_lan_valid(const qa_kex_lan *);
qa_net_send_result qa_kex_lan_emit(void *, qa_bytes, qa_error *);
bool qa_kex_lan_dispatch(qa_kex_lan *, const qa_net_datagram *, qa_error *);
void qa_kex_lan_destroy_detached(qa_kex_lan *);

#endif
