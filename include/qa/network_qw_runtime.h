#ifndef QA_NETWORK_QW_RUNTIME_H
#define QA_NETWORK_QW_RUNTIME_H
#include "qa/network_runtime.h"
#include "qa/network_q1_session.h"
#include "qa/network_q1_qw.h"
#include "qa/network_qw_source.h"
#include "qa/network_q1_channel.h"
#include "qa/network_q1_download_save.h"

typedef struct qa_network_qw_server_policy {
    uint16_t qport;
    uint32_t bytes_per_second;
    size_t message_bytes, queued_messages;
} qa_network_qw_server_policy;
typedef struct qa_network_qw_server_hooks {
    void *context;
    qa_qw_signon_host signon;
    void (*print)(void *, const char *);
    bool (*paused)(void *, qa_net_client_id, bool *, qa_error *);
    /* The source owner submits one raw group. It binds the canonical remote
     * seat and calls qa_network_accept_commands once for the complete packet. */
    bool (*input)(void *, qa_net_client_id, const qa_qw_command *, size_t,
        uint32_t source_sequence, qa_error *);
    bool (*command)(void *, qa_net_client_id, const char *, qa_error *);
    /* Installed hosts retain the authenticated text with its complete source
     * admission, then invoke server_command after the real source EndFrame. */
    bool (*defer_command)(void *, qa_net_client_id, const char *, qa_error *);
    bool (*receipt)(void *, qa_net_client_id, uint32_t acknowledged,
        uint32_t next_outgoing, uint64_t received_ns, qa_error *);
    bool (*blocked)(void *, const qa_net_address *);
    /* Record retirement only. Detachment waits for the runtime safe point. */
    bool (*drop)(void *, qa_net_client_id, const char *, qa_error *);
} qa_network_qw_server_hooks;
typedef struct qa_network_qw_server_state {
    uint32_t input_sequence, outgoing_sequence, choked;
    uint8_t loss;
    uint16_t qport;
    size_t queued_messages, queued_bytes;
    bool active, retiring, reply;
    /* Peer-lifetime FIFO batch serials; appends share the queued batch serial.
     * Only the native reliable-toggle ACK advances acknowledged. */
    uint64_t reliable_queued, reliable_inflight, reliable_acknowledged;
} qa_network_qw_server_state;

/* Original QW28 owns one source seat per connection and uses the runtime's
 * sole transport. Source reservation and real signon callbacks are required. */
bool qa_network_attach_qw_server(qa_network_runtime *, const qa_net_connect *,
    const qa_network_qw_server_policy *, const qa_network_qw_server_hooks *,
    uint64_t now_ns, qa_net_client_id *, qa_error *);
bool qa_network_qw_server_reliable(qa_network_runtime *, qa_net_client_id, qa_bytes, qa_error *);
bool qa_network_qw_server_command(qa_network_runtime *, qa_net_client_id,
    const char *, qa_error *);
/* Actual immutable source baselines replace the entire baseline/history owner. */
bool qa_network_qw_server_baselines(qa_network_runtime *, qa_net_client_id,
    const qa_qw_source_entity *, size_t, qa_error *);
/* Services and entities are observed from the actual source frame. This owner
 * chooses the requested retained wire delta and records only transmitted data. */
bool qa_network_qw_server_frame(qa_network_runtime *, qa_net_client_id,
    qa_bytes services, const qa_qw_source_frame *, qa_error *);
bool qa_network_qw_server_drop(qa_network_runtime *, qa_net_client_id, const char *, qa_error *);
bool qa_network_qw_server_state_read(qa_network_runtime *, qa_net_client_id,
    qa_network_qw_server_state *, qa_error *);
bool qa_network_qw_server_rate(qa_network_runtime *, qa_net_client_id, uint32_t, qa_error *);
const qa_q1_peer *qa_network_qw_server_view(qa_network_runtime *, qa_net_client_id);

#endif
