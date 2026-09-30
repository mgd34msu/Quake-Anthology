#ifndef QA_NETWORK_Q1_RUNTIME_H
#define QA_NETWORK_Q1_RUNTIME_H
#include "qa/network_runtime.h"
#include "qa/network_q1_session.h"
#include "qa/network_q1_channel.h"

typedef struct qa_network_nq_server_policy {
    size_t message_bytes, fragment_bytes, queued_bytes;
} qa_network_nq_server_policy;
typedef struct qa_network_nq_server_hooks {
    void *context;
    /* Stage1 emits actual SERVERINFO/SETVIEW, stage2 retained signon/baselines,
     * stage3 performs source spawn and emits its complete source state. This
     * adapter appends the original SIGNON service only after source success. */
    bool (*signon)(void *, qa_net_client_id, uint8_t stage, qa_q1_emit_fn, void *, qa_error *);
    bool (*begin)(void *, qa_net_client_id, qa_error *);
    bool (*command)(void *, qa_net_client_id, const char *, qa_error *);
    /* The source consumer binds the actual controlled actor/seat and invokes
     * qa_network_accept with this sequence. No source slot is inferred here. */
    bool (*input)(void *, qa_net_client_id, const qa_q1_command *, uint64_t sequence, qa_error *);
    /* Record retirement for the next safe point; never detach inside a hook. */
    bool (*drop)(void *, qa_net_client_id, const char *, qa_error *);
} qa_network_nq_server_hooks;
typedef struct qa_network_nq_server_state {
    uint64_t input_sequence;
    size_t queued_bytes, queued_messages;
    uint8_t stage;
    bool started, retiring;
} qa_network_nq_server_state;
/* Original single-seat NQ15 server. Native channel and FIFO transfer into the
 * shared runtime; its transport remains the sole receive owner. Start only
 * after the real application has reserved the source client. */
bool qa_network_attach_nq_server(qa_network_runtime *, const qa_net_connect *,
    const qa_network_nq_server_policy *, const qa_network_nq_server_hooks *,
    uint64_t now_ns, qa_net_client_id *, qa_error *);
bool qa_network_nq_server_start(qa_network_runtime *, qa_net_client_id, qa_error *);
bool qa_network_nq_server_reliable(qa_network_runtime *, qa_net_client_id, qa_bytes, qa_error *);
/* Complete source TIME/clientdata/entity/events bytes; sent only after begin. */
bool qa_network_nq_server_frame(qa_network_runtime *, qa_net_client_id, qa_bytes, qa_error *);
bool qa_network_nq_server_drop(qa_network_runtime *, qa_net_client_id, const char *, qa_error *);
bool qa_network_nq_server_state_read(qa_network_runtime *, qa_net_client_id,
    qa_network_nq_server_state *, qa_error *);
const qa_q1_peer *qa_network_nq_server_view(qa_network_runtime *, qa_net_client_id);
bool qa_network_nq_server_policy_read(qa_network_runtime *, qa_net_client_id,
    qa_network_nq_server_policy *, qa_error *);
#endif
