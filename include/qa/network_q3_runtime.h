#ifndef QA_NETWORK_Q3_RUNTIME_H
#define QA_NETWORK_Q3_RUNTIME_H
#include "qa/network_runtime.h"
#include "qa/network_q3.h"

/* Client hooks retain the source lifecycle and prediction owner. The shared
 * runtime remains the only transport receiver. Borrowed views expire on the
 * next receive, history mutation or detach. Source cgame consumes complete
 * snapshots and source usercmd history for its own prediction replay. */
typedef struct qa_network_q3_client_policy {
    void *context;
    /* Runtime supplies its actual connection and clock. The admitted source
     * supplies client cvars and initialization/download readiness. */
    bool (*settings)(void *, const qa_net_client *, qa_q3_client_readiness *,
        qa_q3_client_send *, qa_error *);
} qa_network_q3_client_policy;
bool qa_network_attach_q3_client(qa_network_runtime *, const qa_net_connect *,
    qa_q3_product, int32_t challenge, uint16_t qport, const qa_q3_client_hooks *,
    const qa_network_q3_client_policy *, uint64_t now_ns, qa_net_client_id *, qa_error *);
const qa_q3_client_peer *qa_network_q3_client_view(qa_network_runtime *, qa_net_client_id);
/* Pure observation of the generation-qualified native connection lifetime. */
bool qa_network_q3_client_live(qa_network_runtime *, qa_net_client_id);
bool qa_network_q3_client_receive_pending(qa_network_runtime *, qa_net_client_id);
/* Runs the actual unread server-message cursor outside pump/callback entry.
 * Source clear, private construction and download services occur here. */
bool qa_network_q3_client_continue(qa_network_runtime *, qa_net_client_id, qa_error *);
/* Genuine decoded connection counters for external CGAME Init. The executed
 * reliable cursor is independent of the gamestate's received-command cursor. */
typedef struct qa_network_q3_client_init {
    int32_t server_message, last_executed_server_command, client_number;
} qa_network_q3_client_init;
bool qa_network_q3_client_init_read(qa_network_runtime *, qa_net_client_id,
    qa_network_q3_client_init *, qa_error *);
bool qa_network_q3_client_init_current(qa_network_runtime *, qa_net_client_id,
    const qa_network_q3_client_init *);
/* Matches the chosen, genuinely retained snapshot's commandTime against the
 * actual owned usercmd ring. Unmatched history never invents an acknowledgement. */
bool qa_network_q3_client_acknowledged_usercmd(qa_network_runtime *, qa_net_client_id,
    const qa_q3_snapshot *, bool *has_sequence, uint64_t *sequence,
    bool *history_unavailable, qa_error *);
bool qa_network_q3_client_command(qa_network_runtime *, qa_net_client_id, const char *, qa_error *);
bool qa_network_q3_client_usercmd(qa_network_runtime *, qa_net_client_id, const qa_q3_usercmd *, qa_error *);
bool qa_network_q3_client_execute(qa_network_runtime *, qa_net_client_id, int32_t, qa_error *);
/* Explicit source send, including EOF acknowledgements. Uses the same actual
 * client policy and native sequence/channel owner as scheduled sends. */
bool qa_network_q3_client_send(qa_network_runtime *, qa_net_client_id, int32_t real_time, qa_error *);
bool qa_network_q3_client_disconnect(qa_network_runtime *, qa_net_client_id, int32_t real_time, qa_error *);
/* Authenticated server closure latches native retirement; safe source/seat
 * cleanup belongs to the enclosing frontend after receive callbacks return. */
bool qa_network_q3_client_retire(qa_network_runtime *, qa_net_client_id, qa_error *);

/* Source callbacks borrow their application owner. The adapter binds the
 * original channel/reliable/snapshot owner to the shared client and seat.
 * drop must record retirement for the next safe point, never detach inline.
 * resend_gamestate must call qa_network_q3_gamestate with the current source
 * state. enter_world/think retain source command semantics and selected roles. */
bool qa_network_attach_q3_server(qa_network_runtime *, const qa_net_connect *,
    qa_q3_product, int32_t challenge, uint16_t qport, const qa_q3_server_hooks *,
    uint64_t now_ns, qa_net_client_id *, qa_error *);
bool qa_network_q3_gamestate(qa_network_runtime *, qa_net_client_id,
    const qa_q3_gamestate *, const qa_q3_server_rate *, qa_error *);
/* Seed genuine source baselines at accepted connect without sending or
 * promoting either native/shared phase. Caller supplies actual GAME state. */
bool qa_network_q3_seed_baselines(qa_network_runtime *, qa_net_client_id,
    const qa_q3_gamestate *, qa_error *);
bool qa_network_q3_snapshot(qa_network_runtime *, qa_net_client_id,
    const qa_q3_snapshot *, const qa_q3_server_rate *, const qa_q3_download *,
    size_t download_count, qa_error *);
bool qa_network_q3_snapshot_write(qa_network_runtime *, qa_net_client_id,
    const qa_q3_snapshot *, const qa_q3_server_rate *, qa_q3_server_download_write_fn, void *, qa_error *);
bool qa_network_q3_command(qa_network_runtime *, qa_net_client_id, const char *, qa_error *);
bool qa_network_q3_configstring(qa_network_runtime *, qa_net_client_id,
    unsigned index, const char *, qa_error *);
bool qa_network_q3_pure(qa_network_runtime *, qa_net_client_id,
    const qa_q3_pure_server *, const qa_q3_tokens *, qa_q3_pure_result *, qa_error *);
bool qa_network_q3_reset_pure(qa_network_runtime *, qa_net_client_id, qa_error *);
/* Copies the actual original peer state. No borrowed peer pointer escapes. */
bool qa_network_q3_state(qa_network_runtime *, qa_net_client_id, qa_q3_server_state *, qa_error *);
const qa_q3_server_peer *qa_network_q3_server_view(qa_network_runtime *, qa_net_client_id);
/* After the owning source has reconnected and begun this exact retained
 * client for a same-map source round. Requires an idle runtime. Unlike travel,
 * retains channel/reliable/pure/last-usercmd and shared command epoch/history.
 * Makes the next source snapshot full and immediately due. */
bool qa_network_q3_round_activate(qa_network_runtime *, qa_net_client_id, qa_error *);
/* Native retained-frame disconnect delivery at the runtime safe point.
 * Does not remove the shared client or execute source disconnect callbacks. */
bool qa_network_q3_disconnect(qa_network_runtime *, qa_net_client_id,
    const qa_q3_server_rate *, uint8_t server_flags, const char *reason, qa_error *);
/* Only after source admission authenticates reconnect and shared endpoint
 * rebinding succeeds. Recreates wire sequence/XOR state, retaining the shared
 * client and seat. Caller must restart/signon before admitting commands. */
bool qa_network_q3_reconnect_channel(qa_network_runtime *, qa_net_client_id,
    int32_t challenge, uint16_t qport, qa_error *);
#endif
