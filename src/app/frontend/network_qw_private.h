#ifndef QA_FRONTEND_NETWORK_QW_PRIVATE_H
#define QA_FRONTEND_NETWORK_QW_PRIVATE_H
#include "network_qw.h"
#include "save_private.h"
#include "qa/network_q1_connection_save.h"
enum { QW_CLIENTS = 32, QW_PENDING = 32, QW_MESSAGE = 1450, QW_SIGNON = 1400, QW_ACTIONS = 256 };
typedef struct qw_source_action {
    struct qw_source_action *next;
    qa_net_client_id client;
    qa_net_seat_id seat;
    qa_actor_id actor;
    uint64_t epoch;
    uint64_t received_ns;
    char *text;
} qw_source_action;
typedef struct qw_frontend_peer {
    struct frontend_qw_host *host;
    qa_net_client_id client;
    qa_net_seat_id seat;
    uint64_t input_sequence, connected_ns, command_time_ns, last_received_ns;
    uint16_t qport, stat_mask;
    uint32_t rate;
    double stats[16];
    qa_qw_command command;
    float frags;
    char *userinfo;
    struct { uint32_t sequence; uint64_t sent_ns; double ping_ms; bool present; } pings[64];
    int32_t message_level;
    bool occupied, retiring, spectator, begun;
    char reason[256];
} qw_frontend_peer;
typedef struct qw_pending_control {
    qa_net_address address;
    uint64_t time_ns;
    size_t size;
    uint8_t bytes[QW_MESSAGE];
    qa_buffer reply;
} qw_pending_control;
struct frontend_qw_host {
    qa_frontend *frontend;
    qa_network_runtime *runtime;
    qa_server_admin *admin;
    qa_actor_owner owner;
    uint64_t generation, event_cursor, reliable_cursor, event_generation, reliable_generation, published_time_ns;
    uint64_t action_time_ns;
    qa_actor_id action_actor;
    uint32_t random, checksum, player_model, nail_model, supernail_model, active_limit;
    int32_t server_count;
    qa_sha256_digest composition;
    qa_qw_challenges *challenges;
    qw_frontend_peer peers[QW_CLIENTS];
    qw_pending_control pending[QW_PENDING];
    size_t pending_count;
    uint32_t drop_recipients[QW_CLIENTS];
    qa_net_client_id drop_clients[QW_CLIENTS][QW_CLIENTS];
    qa_qw_source_entity baselines[512];
    size_t baseline_count;
    char *models[255], *sounds[255], *styles[64];
    size_t model_count, sound_count;
    qa_buffer *signon;
    qa_bytes *signon_views;
    size_t signon_count;
    qw_source_action *first_action, *last_action;
    size_t action_count;
    qa_buffer status;
    bool previous_pause, action_active;
    unsigned busy;
};
qa_network_qw_server_hooks frontend_qw_peer_hooks(qw_frontend_peer *);
#endif
