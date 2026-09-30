#ifndef QA_NETWORK_RUNTIME_INTERNAL_H
#define QA_NETWORK_RUNTIME_INTERNAL_H
#include "qa/network_runtime.h"

typedef struct qa_network_history_entry {
    qa_network_command command;
    qa_buffer arsenal;
    bool valid;
} qa_network_history_entry;
typedef struct qa_network_seat {
    qa_net_seat_id id;
    uint64_t submitted, accepted, acknowledged, snapshot;
    bool has_submitted, has_accepted, has_snapshot, prediction_fault, applying;
    qa_network_history_entry history[QA_NETWORK_COMMAND_BACKUP];
} qa_network_seat;
typedef struct qa_network_peer {
    qa_net_client_id id;
    qa_network_peer_ops ops;
    void *state;
    qa_network_seat *seats;
    size_t seat_count;
    uint64_t epoch;
    bool occupied;
} qa_network_peer;
struct qa_network_runtime {
    qa_net_transport *transport;
    qa_net_connections *connections;
    qa_network_options options;
    qa_network_peer *peers;
    uint64_t now_ns;
    bool pumping, callback;
};
qa_network_peer *qa_network_peer_get(qa_network_runtime *, qa_net_client_id, qa_error *);
void qa_network_history_clear(qa_network_peer *);
bool qa_network_fail(qa_error *, const char *);
bool qa_network_admission(void *, const qa_net_connect *, qa_error *);
struct qa_network_checkpoint_refs;
bool qa_network_q3_checkpoint_peer(const qa_network_peer *, uint32_t *, qa_buffer *, qa_error *);
bool qa_network_q3_restore_peer(qa_network_runtime *, const qa_net_client *, uint32_t, qa_bytes,
    const struct qa_network_checkpoint_refs *, qa_network_peer *, qa_error *);
#endif
