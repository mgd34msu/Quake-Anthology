#ifndef QA_NETWORK_Q3_RUNTIME_H
#define QA_NETWORK_Q3_RUNTIME_H
#include "qa/network_runtime.h"
#include "qa/network_q3.h"

/* Client hooks retain the source lifecycle and prediction owner. The shared
 * runtime remains the only transport receiver. Borrowed views expire on the
 * next receive, history mutation or detach. Source cgame consumes complete
 * snapshots and source usercmd history for its own prediction replay. */
bool qa_network_attach_q3_client(qa_network_runtime *, const qa_net_connect *,
    qa_q3_product, int32_t challenge, uint16_t qport, const qa_q3_client_hooks *,
    uint64_t now_ns, qa_net_client_id *, qa_error *);
const qa_q3_client_peer *qa_network_q3_client_view(qa_network_runtime *, qa_net_client_id);
bool qa_network_q3_client_command(qa_network_runtime *, qa_net_client_id, const char *, qa_error *);
bool qa_network_q3_client_usercmd(qa_network_runtime *, qa_net_client_id, const qa_q3_usercmd *, qa_error *);
bool qa_network_q3_client_execute(qa_network_runtime *, qa_net_client_id, int32_t, qa_error *);
bool qa_network_q3_client_disconnect(qa_network_runtime *, qa_net_client_id, int32_t real_time, qa_error *);

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
bool qa_network_q3_snapshot(qa_network_runtime *, qa_net_client_id,
    const qa_q3_snapshot *, const qa_q3_server_rate *, const qa_q3_download *,
    size_t download_count, qa_error *);
bool qa_network_q3_command(qa_network_runtime *, qa_net_client_id, const char *, qa_error *);
bool qa_network_q3_configstring(qa_network_runtime *, qa_net_client_id,
    unsigned index, const char *, qa_error *);
bool qa_network_q3_pure(qa_network_runtime *, qa_net_client_id,
    const qa_q3_pure_server *, const qa_q3_tokens *, qa_q3_pure_result *, qa_error *);
/* Copies the actual original peer state. No borrowed peer pointer escapes. */
bool qa_network_q3_state(qa_network_runtime *, qa_net_client_id, qa_q3_server_state *, qa_error *);
/* Only after source admission authenticates reconnect and shared endpoint
 * rebinding succeeds. Recreates wire sequence/XOR state, retaining the shared
 * client and seat. Caller must restart/signon before admitting commands. */
bool qa_network_q3_reconnect_channel(qa_network_runtime *, qa_net_client_id,
    int32_t challenge, uint16_t qport, qa_error *);
#endif
