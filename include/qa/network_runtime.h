#ifndef QA_NETWORK_RUNTIME_H
#define QA_NETWORK_RUNTIME_H

#include "qa/network_unified.h"
#include "qa/network_q3.h"
#include "qa/network_q1.h"

#define QA_NETWORK_COMMAND_BACKUP 128u
#define QA_NETWORK_MAX_SEATS 8u
/* Storage covers the native KEX remote roster. Other wire dialects retain
 * their four-seat limit; local connection admission remains one seat. */
uint32_t qa_network_protocol_seat_capacity(qa_net_protocol_id protocol);

typedef struct qa_network_runtime qa_network_runtime;
bool qa_network_udp_policy_read(const qa_network_runtime *, qa_net_udp_policy *, bool *present, qa_error *);
typedef struct qa_unified_input qa_unified_input;
typedef struct qa_network_command {
    qa_net_client_id client;
    qa_net_seat_id seat;
    qa_actor_id actor;
    uint64_t epoch;
    qa_movement_command movement;
    bool has_arsenal;
    qa_unified_arsenal arsenal;
} qa_network_command;
/* One recovered QuakeWorld packet retains one source sequence. The movement
 * owner receives all raw commands together and owns recursive msec splitting. */
typedef struct qa_network_command_group {
    qa_net_client_id client;
    qa_net_seat_id seat;
    qa_actor_id actor;
    uint64_t epoch;
    qa_movement_kind movement;
    const qa_movement_command *commands;
    size_t count;
} qa_network_command_group;
/* Original Q3 words remain intact until the actual source input owner adapts
 * them. movement authenticates the separately selected control provider. */
typedef struct qa_network_q3_source_command {
    qa_net_client_id client;
    qa_net_seat_id seat;
    qa_actor_id actor;
    uint64_t epoch, sequence;
    qa_movement_kind movement;
    qa_q3_usercmd command;
} qa_network_q3_source_command;
/* Literal NetQuake words and physical source identity are independent of the
 * selected movement kind used to authenticate the canonical controlled seat. */
typedef struct qa_network_nq_source_command {
    qa_net_client_id client;
    qa_net_seat_id seat;
    qa_actor_id actor;
    uint64_t epoch, sequence;
    qa_actor_owner source_owner;
    uint32_t source_slot;
    qa_movement_kind movement;
    qa_q1_command command;
} qa_network_nq_source_command;
/* A snapshot borrows the admitted producer's complete owner checkpoint. The
 * runtime stores command history only, never another actor world/inventory. */
typedef struct qa_network_snapshot {
    qa_net_seat_id seat;
    qa_actor_id actor;
    qa_movement_kind movement;
    uint64_t epoch, sequence, acknowledged_command, server_time_ns;
    const void *owner_checkpoint;
} qa_network_snapshot;
typedef struct qa_network_hooks {
    void *context;
    qa_net_admit_fn admit;
    bool (*controlled)(void *, qa_net_client_id, qa_net_seat_id,
                       qa_actor_id, qa_movement_kind, qa_bytes arsenal, qa_error *);
    bool (*command)(void *, const qa_network_command *, qa_error *);
    /* Restore every prediction-owned component together before replay. replay
     * must use the selected movement kernel and prediction effects policy. */
    bool (*restore)(void *, const qa_network_snapshot *, qa_error *);
    bool (*replay)(void *, const qa_network_command *, qa_error *);
    void (*disconnected)(void *, qa_net_client_id, const char *reason);
    bool (*connectionless)(void *, qa_network_runtime *, const qa_net_datagram *, qa_error *);
    /* Must authenticate retained identity before changing its endpoint. */
    bool (*reconnect)(void *, const qa_net_client *, const qa_net_address *, qa_bytes proof, qa_error *);
    bool (*commands)(void *, const qa_network_command_group *, qa_error *);
    bool (*q3_source_command)(void *, const qa_network_q3_source_command *, qa_error *);
    bool (*nq_source_command)(void *, const qa_network_nq_source_command *, qa_error *);
    bool (*unified_input)(void *,qa_net_client_id,qa_net_seat_id,qa_actor_id,uint64_t epoch,
        const qa_unified_input *,qa_error *);
} qa_network_hooks;
/* One adapter per connection. Source adapters own dialect histories, not
 * seats/world/clocks. receive must authenticate packets before invoking runtime
 * command/snapshot/received. All callbacks run on the runtime owner thread.
 * No callback may attach/detach/travel/pump/destroy the runtime. */
typedef struct qa_network_peer_ops {
    bool (*receive)(void *, qa_network_runtime *, qa_net_client_id,
                    const qa_net_datagram *, qa_error *);
    bool (*flush)(void *, qa_network_runtime *, qa_net_client_id, uint64_t now_ns, qa_error *);
    bool (*command)(void *, const qa_network_command *, qa_error *);
    /* Queue signon/travel through source reliability, retire stale wire deltas.
     * Failure after restart faults this connection and disconnects it. */
    bool (*restart)(void *, uint64_t epoch, const qa_sha256_digest *, qa_error *);
    bool (*rebind)(void *, const qa_net_address *, qa_error *);
    void (*close)(void *);
    /* Pure held-decoder predicate. The sole receiver stops polling and
     * flushing until its owner resumes the source at an idle safe point. */
    bool (*receive_pending)(const void *);
} qa_network_peer_ops;
typedef struct qa_network_options {
    uint64_t owner, timeout_ns;
    uint32_t clients, packets_per_pump;
    qa_network_hooks hooks;
} qa_network_options;
/* Transport transfers on successful create; peers transfer on successful
 * attach. The runtime is the sole receiver and closes all owned resources. */
bool qa_network_create(qa_net_transport *, const qa_network_options *, qa_network_runtime **, qa_error *);
void qa_network_destroy(qa_network_runtime *);
const qa_net_connections *qa_network_connections(const qa_network_runtime *);
bool qa_network_callbacks_idle(const qa_network_runtime *);
bool qa_network_attach(qa_network_runtime *, const qa_net_connect *,
                        const qa_network_peer_ops *, void *peer, uint64_t now_ns,
                        qa_net_client_id *, qa_error *);
bool qa_network_detach(qa_network_runtime *, qa_net_client_id, const char *, qa_error *);
/* Removes an actual cold canonical row whose protocol peer never transferred. */
bool qa_network_discard_incomplete(qa_network_runtime *,qa_net_client_id,qa_error *);
bool qa_network_connection_incomplete(const qa_network_runtime *,qa_net_client_id);
bool qa_network_pump(qa_network_runtime *, uint64_t now_ns, qa_error *);
bool qa_network_send(qa_network_runtime *, qa_net_client_id, qa_bytes, qa_error *);
/* Connectionless services share this transport; they never open a second
 * receive owner. The address is not a connection admission. */
bool qa_network_send_address(qa_network_runtime *, const qa_net_address *, qa_bytes, qa_error *);
bool qa_network_received(qa_network_runtime *, qa_net_client_id, uint64_t now_ns, qa_error *);
bool qa_network_phase(qa_network_runtime *, qa_net_client_id, qa_net_phase, qa_error *);
uint64_t qa_network_epoch(const qa_network_runtime *, qa_net_client_id);
/* submit records a local command and invokes the dialect's encoder. accept
 * invokes gameplay once for an authenticated remote command. Duplicate source
 * sequences are ignored. Rejection never advances the accepted sequence. */
bool qa_network_submit(qa_network_runtime *, const qa_network_command *, qa_error *);
bool qa_network_accept(qa_network_runtime *, const qa_network_command *, qa_error *);
/* Validate and admit the complete QW group once. Its accepted source sequence
 * advances only after the command owner has accepted every retained command. */
bool qa_network_accept_commands(qa_network_runtime *, const qa_network_command_group *, qa_error *);
bool qa_network_accept_q3_source_command(qa_network_runtime *,
    const qa_network_q3_source_command *, qa_error *);
bool qa_network_accept_nq_source_command(qa_network_runtime *,
    const qa_network_nq_source_command *, qa_error *);
/* The genuine Anthology payload keeps binary64 movement and arsenal bytes.
 * Sequence zero is a valid first input; success alone advances its receipt. */
bool qa_network_accept_unified_input(qa_network_runtime *,qa_net_client_id,qa_net_seat_id,
    qa_actor_id,uint64_t epoch,const qa_unified_input *,qa_error *);
bool qa_network_snapshot_apply(qa_network_runtime *, qa_net_client_id,
                                const qa_network_snapshot *, qa_error *);
bool qa_network_restart(qa_network_runtime *, qa_net_client_id,
                         const qa_sha256_digest *, qa_error *);
bool qa_network_reconnect(qa_network_runtime *, qa_net_client_id,
                           const qa_net_address *, qa_bytes proof, uint64_t now_ns, qa_error *);
#endif
