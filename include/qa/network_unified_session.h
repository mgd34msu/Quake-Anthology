#ifndef QA_NETWORK_UNIFIED_SESSION_H
#define QA_NETWORK_UNIFIED_SESSION_H

#include "qa/network_runtime.h"
#include "qa/strings.h"
#include "qa/network_unified_frame_pool.h"

typedef struct qa_unified_input_batch {
    uint32_t epoch;
    qa_usercmd commands[64];
    size_t count;
    qa_buffer providers[64], weapons[64];
    size_t provider_capacity[64], weapon_capacity[64];
    bool borrowed;
} qa_unified_input_batch;
bool qa_unified_inputs_read(const qa_unified_document *, qa_unified_input_batch *, qa_error *);
bool qa_unified_inputs_document(uint32_t epoch, const qa_usercmd *, size_t,
    qa_unified_document **, qa_error *);
void qa_unified_inputs_free(qa_unified_input_batch *);
const qa_unified_input_batch *qa_unified_document_inputs(const qa_unified_document *);

/* Server receipts identify the admitted physical Source player. Client
 * receipts identify its admitted replica actor and providers from the real
 * prepared frame; they become available with the first ACTIVE frame. */
typedef struct qa_unified_session_player {
    qa_actor_id actor;
    qa_net_seat_id seat;
    qa_ruleset_id movement;
    qa_bytes arsenal;
    qa_actor_owner source_owner;
    uint32_t source_slot;
} qa_unified_session_player;
typedef struct qa_unified_session_commit {
    bool applied;
    int64_t acknowledged_input;
    qa_unified_document *reply;
    qa_unified_document *followups[8];
    size_t followup_count;
} qa_unified_session_commit;

typedef struct qa_unified_held {
    qa_unified_frame_lease *lease;
    qa_event_lease *event_lease;
    struct qa_unified_held *next;
    qa_unified_document *document;
    qa_unified_document_kind kind;
    qa_buffer wire;
    size_t bytes;
    uint32_t sequence, required;
    qa_unified_session_commit commit;
    uint32_t response_first, response_last;
    bool source_finished, responses_queued;
} qa_unified_held;
/* Successful Source callbacks transfer distinct immutable reply/followup
 * documents. The lower owner retains them until the entire reliable response
 * batch is queued and the delivery's phase transition completes. */
typedef struct qa_unified_session_hooks {
    qa_strings *strings;
    void *context;
    bool (*player)(void *, qa_net_client_id, qa_unified_session_player *, qa_error *);
    bool (*events_decode)(void *, qa_bytes, qa_unified_held **, bool *ready, qa_error *);
    bool (*control)(void *, qa_network_runtime *, qa_net_client_id, uint32_t epoch,
        const qa_unified_document *, qa_unified_session_commit *, qa_error *);
    bool (*input)(void *, qa_network_runtime *, qa_net_client_id,
        const qa_unified_input_batch *, qa_error *);
    bool (*prepare)(void *, qa_net_client_id, const qa_unified_document *, bool *ready, qa_error *);
    bool (*frame)(void *, qa_network_runtime *, qa_net_client_id,
        const qa_unified_document *, qa_unified_session_commit *, qa_error *);
    bool (*restart)(void *, qa_network_runtime *, qa_net_client_id, uint32_t epoch,
        const uint64_t *, qa_unified_document **offer, qa_error *);
    bool (*source_ready)(void *, qa_network_runtime *, qa_net_client_id, uint32_t wire_epoch, qa_error *);
    void (*closed)(void *, qa_net_client_id);
} qa_unified_session_hooks;
typedef struct qa_unified_session qa_unified_session;
const qa_unified_limits *qa_unified_session_limits(const qa_unified_session *);
/* Borrows the sole generic runtime and transfers the peer/channel on attach.
 * Returned control borrows until runtime detach/destroy. One real local seat
 * belongs to each production Anthology peer, independently of wire actors. */
bool qa_unified_session_attach(qa_network_runtime *, const qa_net_connect *, bool server,
    qa_unified_token, const qa_unified_limits *, const qa_unified_session_hooks *,
    uint64_t now_ns, qa_net_client_id *, qa_unified_session **borrowed_control, qa_error *);
/* Resolves only the true installed production peerops/state outside pumping
 * and runtime callbacks, while the borrowed owner remains attached. */
bool qa_unified_session_find(qa_network_runtime *, qa_net_client_id,
    qa_unified_session **borrowed_control, qa_error *);
/* Held decoded documents stay owned until actual preparation and publication
 * finish. Process only outside runtime pumping and callbacks. */
bool qa_unified_session_process(qa_unified_session *, bool *waiting, qa_error *);
/* Queues only replies from already completed Source controls before replacing
 * their epoch. Unfinished controls remain held; retiring peers need no offer.
 * ready is false while the reliable queue lacks space for the retained batch. */
bool qa_unified_session_restart_prepare(qa_unified_session *, bool *ready,
    bool *retiring, qa_error *);
bool qa_unified_session_offer_ready(const qa_unified_session *, const qa_unified_document *,
    bool *ready, qa_error *);
/* Checks a real immutable outgoing control without consuming its reliable
 * sequence. Queue pressure or allocation failure leaves ready false. */
bool qa_unified_session_control_ready(const qa_unified_session *, const qa_unified_document *,
    bool *ready, qa_error *);
/* Admission runs inside the actual generic restart callback. It joins the
 * canonical table's seat storage to the true pending offer and its generation;
 * CLIENT offers must be the exact unfinished receive head being committed. */
bool qa_unified_session_restart_admit(const qa_unified_session *, const qa_network_runtime *,
    const qa_net_connect *, const qa_unified_document *actual_offer, qa_error *);
/* A local timeout/close may retire the exact unfinished CLIENT offer after
 * its Source published a newer recipe but before the lower epoch committed.
 * This proof is available only inside that actual disconnect callback. */
bool qa_unified_session_client_disconnect_pending(const qa_unified_session *, qa_network_runtime *,
    qa_net_client_id, uint32_t callback_epoch, const qa_unified_document *disconnect,
    const qa_unified_document *actual_offer, qa_error *);
bool qa_unified_session_control(qa_unified_session *, const qa_unified_document *, qa_error *);
/* Native compiled Source commands preserve their received activation and
 * lexical tokens; the server resolves that actual provider instance. */
typedef struct qa_module_console_call {
    const char *instance;
    uint64_t publication, map_revision;
    const char *const *arguments;
    size_t argument_count;
} qa_module_console_call;
bool qa_unified_session_source_command(qa_unified_session *, const qa_module_console_call *, qa_error *);
bool qa_unified_session_frame(qa_unified_session *, const qa_unified_document *, qa_error *);
bool qa_unified_session_input(qa_unified_session *, const qa_usercmd *, qa_error *);
/* Retains a real local close request, including from the actual Source
 * callback. Source retirement and the signed reply run at the next returned
 * process boundary; retries preserve the original reason. */
bool qa_unified_session_close(qa_unified_session *, const char *reason, qa_error *);
bool qa_unified_session_flush(qa_unified_session *, uint64_t now_ns, qa_error *);
bool qa_unified_session_idle(const qa_unified_session *);
bool qa_unified_session_disconnected(const qa_unified_session *);
bool qa_unified_session_retiring(const qa_unified_session *);
/* True only after real admission/first-frame phase commit on the published
 * physical transport. A Source-finished but unqueued reply is not active. */
bool qa_unified_session_active(const qa_unified_session *);
uint32_t qa_unified_session_epoch(const qa_unified_session *);
int64_t qa_unified_session_acknowledged(const qa_unified_session *);

typedef enum qa_unified_handshake_kind {
    QA_UNIFIED_HELLO, QA_UNIFIED_CHALLENGE, QA_UNIFIED_CONNECT
} qa_unified_handshake_kind;
typedef struct qa_unified_handshake {
    qa_unified_handshake_kind kind;
    qa_unified_token nonce, token;
} qa_unified_handshake;
bool qa_unified_handshake_read(qa_bytes, qa_unified_handshake *, qa_error *);
bool qa_unified_handshake_write(const qa_unified_handshake *, qa_buffer *, qa_error *);
bool qa_unified_token_random(qa_unified_token *, qa_error *);

#endif
