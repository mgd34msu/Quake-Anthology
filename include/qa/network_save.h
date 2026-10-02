#ifndef QA_NETWORK_SAVE_H
#define QA_NETWORK_SAVE_H
#include "qa/network_q3_runtime.h"
#include "qa/network_q3_save.h"
#include "qa/network_q1_runtime.h"
#include "qa/network_qw_runtime.h"
#include "qa/network_q1_client_runtime.h"
#include "qa/network_q2_session_save.h"
#include "qa/network_unified_save.h"
#include "qa/network_local.h"

/* Connection records replace the process-local owner namespace with the
 * admitted candidate owner, preserving slot generations and source seat IDs.
 * The candidate admission callback validates every installed connection. */
bool qa_net_connections_checkpoint(const qa_net_connections *, qa_net_writer *);
bool qa_net_connections_restore(qa_net_reader *, uint64_t owner, uint32_t capacity,
    qa_net_admit_fn, void *, qa_net_connections **);

typedef enum qa_network_source_kind {
    QA_NETWORK_SOURCE_Q3_CLIENT = 1, QA_NETWORK_SOURCE_Q3_SERVER,
    QA_NETWORK_SOURCE_NQ_SERVER, QA_NETWORK_SOURCE_QW_SERVER,
    QA_NETWORK_SOURCE_Q2_SERVER, QA_NETWORK_SOURCE_Q2_CLIENT, QA_NETWORK_SOURCE_UNIFIED,
    QA_NETWORK_SOURCE_Q1_CLIENT, QA_NETWORK_SOURCE_LOCAL
} qa_network_source_kind;
typedef struct qa_network_checkpoint_refs {
    void *context;
    bool (*source)(void *, const qa_net_client *, qa_network_source_kind,
        qa_q3_client_hooks *, qa_q3_server_hooks *, qa_error *);
    bool (*save_actor)(void *, qa_actor_id, qa_saved_actor_id *, qa_error *);
    bool (*restore_actor)(void *, qa_saved_actor_id, qa_actor_id *, qa_error *);
    bool (*source_nq)(void *, const qa_net_client *, qa_network_nq_server_policy *,
        qa_network_nq_server_hooks *, qa_error *);
    bool (*source_qw)(void *, const qa_net_client *, qa_network_qw_server_policy *,
        qa_network_qw_server_hooks *, qa_qw_download_admission *, qa_error *);
    bool (*client_q3_policy)(void *, const qa_net_client *, qa_network_q3_client_policy *, qa_error *);
    qa_network_q2_checkpoint_refs q2;
    bool (*source_unified)(void *, const qa_net_client *, qa_unified_session_hooks *, qa_error *);
    bool (*source_q1_client)(void *,const qa_net_client *,qa_network_q1_client_policy *,
        qa_network_q1_client_hooks *,qa_error *);
    bool (*source_local)(void *,const qa_net_client *,qa_network_local_hooks *,qa_error *);
} qa_network_checkpoint_refs;

/* Transport is an explicitly prepared candidate binding. Source callback
 * descriptors must borrow the candidate, never the active application.
 * Unsupported installed dialect owners fail capture; no absent record is
 * fabricated. Transport transfers only after successful restore. */
bool qa_network_connections_checkpoint(const qa_network_runtime *, const qa_network_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_network_connections_restore(qa_bytes, qa_net_transport *, const qa_network_options *,
    const qa_network_checkpoint_refs *, qa_network_runtime **, qa_error *);
/* Generic prediction command history is a distinct owner from a source
 * CGAME's complete native player prediction continuation. Both are required
 * by an installed remote prediction consumer. Restore calls no replay hook. */
bool qa_network_prediction_checkpoint(const qa_network_runtime *, const qa_network_checkpoint_refs *,
    qa_buffer *, qa_error *);
bool qa_network_prediction_restore(qa_network_runtime *, const qa_network_checkpoint_refs *,
    qa_bytes, qa_error *);
/* No-fail final exchange after both owners have passed their idle/identity
 * admission. Keeps the live local socket endpoint and its sole receive owner. */
void qa_network_transport_exchange(qa_network_runtime *, qa_network_runtime *);
/* Wrappers which already moved their actual native socket retain their
 * installed transport containers while publishing the same Source custody. */
void qa_network_transport_publish_retained(qa_network_runtime *,qa_network_runtime *);
/* Qualify actual restored Source callbacks before the final aggregate exchange.
 * Pure decoded channels remain unbound until that successful publication. */
bool qa_network_source_publication_ready(const qa_network_runtime *,qa_error *);
const qa_net_address *qa_network_local_address(const qa_network_runtime *);
/* Read the actual accepted source-command counter for one admitted seat.
 * Idle and readonly; it neither submits a command nor changes history. */
bool qa_network_accepted_sequence(const qa_network_runtime *, qa_net_client_id,
    qa_net_seat_id, bool *present, uint64_t *sequence, qa_error *);
#endif
