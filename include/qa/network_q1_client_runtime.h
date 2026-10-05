#ifndef QA_NETWORK_Q1_CLIENT_RUNTIME_H
#define QA_NETWORK_Q1_CLIENT_RUNTIME_H
#include "qa/network_runtime.h"
#include "qa/network_q1_channel.h"
#include "qa/network_q1_nq.h"
#include "qa/network_q1_qw.h"
#include "qa/network_q1_session.h"

typedef struct qa_network_q1_client_policy {
    size_t message_bytes, fragment_bytes, queued_bytes, service_limit, pending_commands;
    uint16_t qport;
    uint32_t bytes_per_second;
    qa_nq_options nq_options;
    qa_nq_signon nq_identity;
} qa_network_q1_client_policy;
typedef struct qa_network_q1_client_hooks {
    void *context;
    /* Called at the enclosing runtime idle boundary, once per retained service.
     * Arguments borrow the complete owned batch through the end callback. */
    bool (*nq)(void *, qa_net_client_id, qa_net_protocol_id decoded_protocol,
        const qa_nq_message *, uint64_t received_ns, qa_error *);
    bool (*qw)(void *, qa_net_client_id, qa_net_protocol_id decoded_protocol,
        const qa_qw_service *, uint64_t received_ns, qa_error *);
    /* Actual source media preparation supplies the map's checksum. Name lists
     * contain the original ordered wire precaches, without an invented index0. */
    bool (*qw_game_state)(void *, qa_net_client_id, const char *const *, size_t,
        const char *const *, size_t, uint32_t *map_checksum, qa_error *);
    bool (*qw_skins)(void *, qa_net_client_id, bool *ready, qa_error *);
    bool (*end)(void *, qa_net_client_id, uint64_t received_ns, qa_error *);
    bool (*command_nq)(void *, const qa_network_command *, qa_q1_command *, qa_error *);
    bool (*command_qw)(void *, const qa_network_command *, uint64_t now_ns, qa_qw_command *, qa_error *);
    /* The actual camera owner supplies its pending spectator teleport after
     * command_qw. This native coordinate service enters the reliable FIFO. */
    bool (*qw_teleport)(void *, qa_net_client_id, qa_vec3 *, bool *present, qa_error *);
    bool (*qw_loss)(void *, qa_net_client_id, uint32_t outgoing_sequence, uint8_t *, qa_error *);
    bool (*sent)(void *, qa_net_client_id, uint32_t sequence, const qa_q1_command *,
        const qa_qw_command *, uint64_t sent_ns, qa_error *);
    bool (*acknowledged)(void *, qa_net_client_id, uint32_t sequence, uint64_t received_ns, qa_error *);
    bool (*drop)(void *, qa_net_client_id, const char *, qa_error *);
    /* Complete accepted native service bytes, after their real Source callbacks.
     * Recording failures are handled by the recording owner independently. */
    bool (*batch)(void *, qa_net_client_id, qa_net_protocol_id, qa_bytes, qa_bytes wire_prefix,
        uint32_t sequence, uint32_t acknowledged, uint64_t received_ns, qa_error *);
} qa_network_q1_client_hooks;
typedef struct qa_network_q1_client_state {
    qa_net_protocol_id protocol, admitted_protocol, before_protocol;
    uint32_t moves, last_frame;
    size_t services, next_service, queued_bytes, pending_commands;
    uint8_t signon;
    bool received, active, retiring, has_delta, waiting_skins;
} qa_network_q1_client_state;

/* The actual accepted connection supplies its protocol, endpoint and single
 * seat. This owner transfers to the sole runtime receiver on successful attach. */
bool qa_network_attach_q1_client(qa_network_runtime *, const qa_net_connect *,
    const qa_network_q1_client_policy *, const qa_network_q1_client_hooks *,
    uint64_t now_ns, qa_net_client_id *, qa_error *);
/* Same retained CLIENT, attached to its real local seat without UDP traffic. */
bool qa_network_attach_q1_demo(qa_network_runtime *, const qa_net_connect *,
    const qa_network_q1_client_policy *, const qa_network_q1_client_hooks *,
    uint64_t now_ns, qa_net_client_id *, qa_error *);
bool qa_network_q1_demo_packet(qa_network_runtime *, qa_net_client_id, qa_bytes,
    uint64_t received_ns, qa_error *);
bool qa_network_q1_demo_sequences(qa_network_runtime *, qa_net_client_id,
    uint32_t outgoing, uint32_t incoming, qa_error *);
bool qa_network_q1_demo_command(qa_network_runtime *, qa_net_client_id,
    const qa_qw_command *, uint64_t sent_ns, qa_error *);
bool qa_network_q1_client_sequences(qa_network_runtime *, qa_net_client_id,
    uint32_t *outgoing, uint32_t *incoming, qa_error *);
bool qa_network_q1_client_start(qa_network_runtime *, qa_net_client_id, qa_error *);
bool qa_network_q1_client_disconnect(qa_network_runtime *, qa_net_client_id,
    const char *reason, qa_error *);
bool qa_network_q1_client_continue(qa_network_runtime *, qa_net_client_id, qa_error *);
/* Completes the genuine pending skin/download preparation; no signon replay. */
bool qa_network_q1_client_skins_ready(qa_network_runtime *, qa_net_client_id, qa_error *);
bool qa_network_q1_client_receive_pending(qa_network_runtime *, qa_net_client_id);
bool qa_network_q1_client_command(qa_network_runtime *, qa_net_client_id, const char *, qa_error *);
bool qa_network_q1_client_move_nq(qa_network_runtime *, qa_net_client_id, const qa_q1_command *, qa_error *);
bool qa_network_q1_client_move_qw(qa_network_runtime *, qa_net_client_id, const qa_qw_command *, qa_error *);
/* The genuine Source owns its input ordinal and native sent/ack history.
 * Qualifies the admitted full actor/seat without generic snapshot replay. */
bool qa_network_q1_client_submit(qa_network_runtime *, const qa_network_command *, qa_error *);
bool qa_network_q1_client_state_read(qa_network_runtime *, qa_net_client_id,
    qa_network_q1_client_state *, qa_error *);
const qa_nq_decoder *qa_network_q1_client_nq_decoder(qa_network_runtime *, qa_net_client_id);
const qa_qw_decoder *qa_network_q1_client_qw_decoder(qa_network_runtime *, qa_net_client_id);
#endif
