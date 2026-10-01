#ifndef QA_NETWORK_Q1_CHANNEL_H
#define QA_NETWORK_Q1_CHANNEL_H

#include "qa/network_q1.h"

#define QA_NQ_FLAG_DATA UINT32_C(0x00010000)
#define QA_NQ_FLAG_ACK UINT32_C(0x00020000)
#define QA_NQ_FLAG_EOM UINT32_C(0x00080000)
#define QA_NQ_FLAG_UNRELIABLE UINT32_C(0x00100000)
#define QA_NQ_FLAG_CONTROL UINT32_C(0x80000000)

typedef struct qa_q1_delivery {
    bool present, reliable;
    uint32_t sequence, acknowledged, dropped;
    qa_bytes payload;
} qa_q1_delivery;
typedef struct qa_nq_channel qa_nq_channel;
typedef struct qa_qw_channel qa_qw_channel;
typedef enum qa_q1_channel_side { QA_Q1_CHANNEL_CLIENT, QA_Q1_CHANNEL_SERVER } qa_q1_channel_side;
/* Packet outputs are borrowed until the next operation producing a packet.
 * Reliable NQ delivery storage lasts until the next receive. Other delivery
 * payloads borrow input bytes. Time values are monotonic nanoseconds. */
bool qa_nq_channel_create(size_t message_bytes, size_t fragment_bytes, qa_nq_channel **, qa_error *);
void qa_nq_channel_destroy(qa_nq_channel *);
bool qa_nq_channel_ready(const qa_nq_channel *);
bool qa_nq_channel_queue(qa_nq_channel *, qa_bytes, qa_error *);
bool qa_nq_channel_next(qa_nq_channel *, uint64_t, bool *present, qa_bytes *, qa_error *);
bool qa_nq_channel_unreliable(qa_nq_channel *, qa_bytes, qa_bytes *, qa_error *);
bool qa_nq_channel_receive(qa_nq_channel *, qa_bytes, uint64_t, qa_q1_delivery *, qa_bytes *reply, qa_error *);
bool qa_qw_channel_create(qa_q1_channel_side, uint16_t qport, size_t message_bytes,
                           uint32_t bytes_per_second, qa_qw_channel **, qa_error *);
void qa_qw_channel_destroy(qa_qw_channel *);
bool qa_qw_channel_queue(qa_qw_channel *, qa_bytes, qa_error *);
bool qa_qw_channel_pending(const qa_qw_channel *);
bool qa_qw_channel_rate(qa_qw_channel *, uint32_t bytes_per_second, qa_error *);
bool qa_qw_channel_can_send(const qa_qw_channel *, uint64_t);
bool qa_qw_channel_transmit(qa_qw_channel *, qa_bytes, uint64_t, bool server_paused, qa_bytes *, qa_error *);
bool qa_qw_channel_receive(qa_qw_channel *, qa_bytes, uint64_t, qa_q1_delivery *, qa_error *);
typedef struct qa_qw_channel_stats {
    uint32_t incoming_sequence, outgoing_sequence;
    uint64_t last_received_ns;
    double frame_latency, frame_interval_ms;
    uint32_t bytes_per_second;
} qa_qw_channel_stats;
qa_qw_channel_stats qa_qw_channel_get_stats(const qa_qw_channel *);

typedef enum qa_q1_peer_kind { QA_Q1_PEER_NETQUAKE, QA_Q1_PEER_QUAKEWORLD } qa_q1_peer_kind;
/* Peer borrows its transport and channel. Each belongs to a single caller. */
typedef struct qa_q1_peer {
    qa_net_transport *transport;
    qa_net_address remote;
    qa_q1_peer_kind kind;
    union { qa_nq_channel *nq; qa_qw_channel *qw; } channel;
    /* A failed ACK send does not discard an already accepted delivery. */
    bool reply_send_failed;
} qa_q1_peer;
bool qa_q1_peer_send(qa_q1_peer *, qa_bytes, qa_error *);
bool qa_q1_peer_receive(qa_q1_peer *, const qa_net_address *, qa_bytes, uint64_t,
                         qa_q1_delivery *, qa_error *);

typedef enum qa_nq_control_kind {
    QA_NQ_CONNECT_REQUEST = 1, QA_NQ_SERVER_INFO_REQUEST = 2,
    QA_NQ_PLAYER_INFO_REQUEST = 3, QA_NQ_RULE_INFO_REQUEST = 4,
    QA_NQ_ACCEPT = 0x81, QA_NQ_REJECT = 0x82, QA_NQ_SERVER_INFO = 0x83,
    QA_NQ_PLAYER_INFO = 0x84, QA_NQ_RULE_INFO = 0x85
} qa_nq_control_kind;
typedef struct qa_qw_rule { const char *name, *value; } qa_qw_rule;
typedef struct qa_nq_control {
    qa_nq_control_kind kind;
    union {
        struct { const char *game; uint8_t version; } request;
        uint8_t player;
        const char *previous_rule;
        int32_t port;
        const char *reason;
        struct { const char *address, *name, *map; uint8_t players, max_players, version; } server;
        struct { uint8_t player; const char *name; int32_t colors, frags, seconds; const char *address; } player_info;
        struct { bool present; qa_qw_rule rule; } rule_info;
    } data;
} qa_nq_control;
/* Decode strings borrow the input packet. */
bool qa_nq_control_decode(qa_bytes, qa_nq_control *, qa_error *);
bool qa_nq_control_encode(const qa_nq_control *, qa_net_writer *);
typedef enum qa_q1_connect_decision { QA_Q1_CONNECT_ACCEPT, QA_Q1_CONNECT_REJECT, QA_Q1_CONNECT_IGNORE } qa_q1_connect_decision;
typedef struct qa_q1_connect_result {
    qa_q1_connect_decision decision;
    uint16_t port;
    const char *reason;
} qa_q1_connect_result;
typedef struct qa_nq_connection_host {
    void *context;
    bool (*server_info)(void *, qa_nq_control *, qa_error *);
    bool (*player_info)(void *, uint8_t, bool *present, qa_nq_control *, qa_error *);
    bool (*next_rule)(void *, const char *, bool *present, qa_qw_rule *, qa_error *);
    bool (*connect)(void *, const qa_net_address *, uint64_t, qa_q1_connect_result *, qa_error *);
} qa_nq_connection_host;
bool qa_nq_control_answer(qa_bytes, const qa_net_address *, uint64_t,
                           const qa_nq_connection_host *, bool *present, qa_net_writer *, qa_error *);

bool qa_qw_oob_encode(const char *, bool nul_terminated, qa_net_writer *);
bool qa_qw_oob_decode(qa_bytes, qa_bytes *text, qa_error *);
typedef struct qa_qw_info { qa_qw_rule *rules; size_t count; char *storage; } qa_qw_info;
bool qa_qw_info_parse(const char *, qa_qw_info *, qa_error *);
const char *qa_qw_info_get(const qa_qw_info *, const char *key);
void qa_qw_info_free(qa_qw_info *);
typedef uint32_t (*qa_qw_random_fn)(void *);
typedef struct qa_qw_challenges qa_qw_challenges;
bool qa_qw_challenges_create(size_t capacity, qa_qw_random_fn, void *, qa_qw_challenges **, qa_error *);
void qa_qw_challenges_destroy(qa_qw_challenges *);
bool qa_qw_challenge_issue(qa_qw_challenges *, const qa_net_address *, uint64_t, uint32_t *, qa_error *);
bool qa_qw_challenge_validate(const qa_qw_challenges *, const qa_net_address *, uint32_t);
typedef struct qa_qw_connect_request {
    qa_net_address from;
    uint16_t qport;
    uint32_t challenge;
    const char *userinfo;
    bool spectator, donor_wide;
} qa_qw_connect_request;
typedef bool (*qa_qw_reply_fn)(void *, qa_bytes, qa_error *);
typedef bool (*qa_qw_text_fn)(void *, const char *, qa_error *);
/* Callbacks complete synchronously and borrow arguments only for that call.
 * execute_admin streams output through write; the server chunks it on wire. */
typedef struct qa_qw_connection_host {
    void *context;
    const char *password, *spectator_password, *rcon_password;
    bool high_characters;
    bool (*blocked)(void *, const qa_net_address *);
    bool (*connect)(void *, const qa_qw_connect_request *, uint64_t, qa_q1_connect_result *, qa_error *);
    bool (*status)(void *, const char **, qa_error *);
    bool (*log)(void *, int32_t sequence, const char **, qa_error *);
    bool (*execute_admin)(void *, const char *command, qa_qw_text_fn write, void *, qa_error *);
} qa_qw_connection_host;
bool qa_qw_connectionless_receive(const qa_qw_connection_host *, qa_qw_challenges *,
                                   qa_bytes, const qa_net_address *, uint64_t,
                                   qa_qw_reply_fn, void *, qa_error *);

typedef enum qa_q1_connect_phase {
    QA_Q1_CONNECT_WAITING, QA_Q1_CONNECT_CHALLENGE, QA_Q1_CONNECT_REQUESTING,
    QA_Q1_CONNECT_CONNECTED, QA_Q1_CONNECT_REJECTED
} qa_q1_connect_phase;
typedef struct qa_q1_connect_state {
    qa_q1_connect_phase phase;
    unsigned attempts;
    uint16_t port;
    int32_t challenge;
    const char *reason;
} qa_q1_connect_state;
typedef struct qa_nq_connect_client qa_nq_connect_client;
typedef struct qa_qw_connect_client qa_qw_connect_client;
bool qa_nq_connect_create(qa_nq_connect_client **, qa_error *);
void qa_nq_connect_destroy(qa_nq_connect_client *);
qa_q1_connect_state qa_nq_connect_state(const qa_nq_connect_client *);
bool qa_nq_connect_next(qa_nq_connect_client *, uint64_t, bool *, qa_net_writer *, qa_error *);
bool qa_nq_connect_receive(qa_nq_connect_client *, qa_bytes, qa_error *);
bool qa_qw_connect_create(uint16_t qport, const char *userinfo, qa_qw_connect_client **, qa_error *);
void qa_qw_connect_destroy(qa_qw_connect_client *);
qa_q1_connect_state qa_qw_connect_state(const qa_qw_connect_client *);
bool qa_qw_connect_next(qa_qw_connect_client *, uint64_t, bool *, qa_net_writer *, qa_error *);
bool qa_qw_connect_receive(qa_qw_connect_client *, qa_bytes, qa_error *);
bool qa_qw_heartbeat(uint32_t sequence, uint32_t active_clients, qa_net_writer *);
bool qa_qw_shutdown(qa_net_writer *);
typedef struct qa_qw_master_heartbeat { uint64_t previous_ns; uint32_t sequence; bool sent; } qa_qw_master_heartbeat;
bool qa_qw_master_next(qa_qw_master_heartbeat *, uint64_t, uint32_t active_clients,
                        bool force, bool *present, qa_net_writer *, qa_error *);

typedef struct qa_q1_discovery_player { int32_t score, ping; const char *name; } qa_q1_discovery_player;
typedef struct qa_q1_discovery {
    qa_net_protocol_id protocol;
    const char *name, *map, *source_address;
    uint32_t players, max_players;
    qa_qw_info rules;
    qa_q1_discovery_player *player_details;
    size_t player_count;
    char *storage;
} qa_q1_discovery;
bool qa_nq_discovery_query(qa_net_writer *);
bool qa_qw_discovery_query(qa_net_writer *);
/* Donor NQ has no master protocol; QW master lists use the HTTP service. */
bool qa_q1_discovery_master_query(bool quakeworld, qa_net_writer *);
bool qa_q1_discovery_read(qa_bytes, bool quakeworld, qa_q1_discovery *, qa_error *);
void qa_q1_discovery_free(qa_q1_discovery *);

#endif
