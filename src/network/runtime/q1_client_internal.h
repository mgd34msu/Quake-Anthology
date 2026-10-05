#ifndef QA_NETWORK_RUNTIME_Q1_CLIENT_INTERNAL_H
#define QA_NETWORK_RUNTIME_Q1_CLIENT_INTERNAL_H
#include "internal.h"
#include "qa/network_q1_client_runtime.h"
#include "qa/network_q1_decoder_save.h"
#include "qa/network_q1_peer_save.h"
#include "qa/network_q1_session_save.h"
#include "qa/network_q1_history_save.h"
#include "q1_client_retirement.h"

typedef struct q1_client_pending {
    struct q1_client_pending *next;
    qa_buffer bytes;
} q1_client_pending;
typedef struct q1_client_record {
    qa_net_protocol_id protocol;
    union { qa_nq_message nq; qa_qw_service qw; } service;
    const char **names;
} q1_client_record;
typedef struct q1_client_move {
    union { qa_q1_command nq; qa_qw_command qw; } command;
} q1_client_move;
typedef struct q1_runtime_client {
    qa_network_runtime *runtime;
    qa_net_client_id id;
    qa_net_protocol_id protocol, admitted_protocol, before_protocol;
    qa_q1_peer native;
    qa_network_q1_client_policy policy;
    qa_network_q1_client_hooks hooks;
    qa_nq_decoder *nq;
    qa_qw_decoder *qw;
    qa_qw_precache *precache;
    qa_nq_signon signon;
    char *name, *parameters;
    qa_qw_history history;
    q1_client_retirement retirement;
    q1_client_pending *first, *last;
    size_t queued_bytes;
    q1_client_move *commands;
    size_t command_count;
    qa_buffer payload, before_decoder;
    q1_client_record *records;
    size_t record_count, cursor;
    uint64_t received_ns;
    uint32_t sequence, acknowledged, moves, last_frame;
    uint8_t demo_wire_prefix[8];
    bool demo_wire_prefix_present;
    bool held, has_delta, active, retiring, busy, started, waiting_skins, demo;
} q1_runtime_client;

extern const qa_network_peer_ops qa_network_q1_client_ops;
q1_runtime_client *q1_client_get(qa_network_runtime *, qa_net_client_id, qa_error *);
bool q1_client_create(qa_network_runtime *, const qa_net_connect *,
    const qa_network_q1_client_policy *, const qa_network_q1_client_hooks *, q1_runtime_client **, qa_error *);
bool q1_client_decode_batch(q1_runtime_client *, qa_bytes, uint32_t, uint32_t, uint64_t, qa_error *);
bool q1_client_queue(q1_runtime_client *, qa_bytes, qa_error *);
void q1_client_batch_clear(q1_runtime_client *);
void q1_client_close(void *);
bool qa_network_q1_client_peer(const qa_network_peer *);
bool qa_network_q1_client_retirement_pending(const qa_network_peer *);
void qa_network_q1_client_transport_rebind(qa_network_peer *, qa_net_transport *);
bool qa_network_q1_client_checkpoint_peer(const qa_network_peer *, qa_buffer *, qa_error *);
bool qa_network_q1_client_restore_peer(qa_network_runtime *, const qa_net_client *, qa_bytes,
    const qa_network_q1_client_policy *, const qa_network_q1_client_hooks *, qa_network_peer *, qa_error *);
#endif
