#ifndef QA_FRONTEND_NETWORK_NQ_PRIVATE_H
#define QA_FRONTEND_NETWORK_NQ_PRIVATE_H
#include "network_nq.h"
#include "../../network/event_receipts.h"

enum { NQ_CLIENTS = 64, NQ_MESSAGE = 8000, NQ_DATAGRAM = 1024, NQ_PENDING = 32, NQ_PINGS = 16 };
typedef struct nq_frontend_peer {
    struct frontend_nq_host *host;
    qa_net_client_id client;
    qa_net_seat_id seat;
    qa_q1_entity *baselines;
    size_t baseline_count;
    qa_q1_command latest;
    uint64_t input_sequence, tick_sequence, entered_ns, admission_order;
    uint64_t protocol_generation;
    uint64_t protocol_cursor;
    qa_event_receipts event_receipts;
    double pings[NQ_PINGS];
    uint8_t ping_count;
    uint32_t source_slot;
    uint8_t impulse;
    bool occupied, retiring, command_present;
    char reason[256];
} nq_frontend_peer;
typedef struct nq_pending_control {
    qa_net_address address;
    uint64_t received_ns;
    size_t size;
    uint8_t bytes[1024];
} nq_pending_control;
typedef struct nq_status_cache {
    char *name;
    int32_t frags;
    float source_frags;
    uint8_t colors;
    bool present;
} nq_status_cache;
struct frontend_nq_host {
    qa_frontend *frontend;
    qa_network_runtime *runtime;
    uint64_t composition;
    qa_actor_owner owner;
    uint64_t generation, submillisecond_ns, published_source_time_ns, next_admission_order;
    nq_frontend_peer peers[NQ_CLIENTS];
    nq_pending_control pending[NQ_PENDING];
    size_t pending_count;
    unsigned busy;
    bool previous_pause;
    char reply_address[128];
    nq_status_cache board[256];
    char *published_names[256];
    char *styles[64];
};
bool frontend_nq_source_hooks(frontend_nq_host *, const qa_net_client *,
    qa_network_nq_server_policy *, qa_network_nq_server_hooks *, qa_error *);
bool frontend_nq_qualified(const frontend_nq_host *, bool complete_clock, qa_error *);
#endif
