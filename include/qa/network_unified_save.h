#ifndef QA_NETWORK_UNIFIED_SAVE_H
#define QA_NETWORK_UNIFIED_SAVE_H
#include "qa/network_unified_session.h"

bool qa_unified_channel_idle(const qa_unified_channel *);
bool qa_unified_channel_checkpoint(const qa_unified_channel *, qa_buffer *, qa_error *);
bool qa_unified_channel_restore(qa_bytes, qa_unified_channel **, qa_error *);
bool qa_unified_channel_descriptor(const qa_unified_channel *, qa_unified_token *, qa_unified_limits *, qa_error *);
typedef struct qa_unified_progress {
    uint64_t next_reliable, next_frame;
    uint32_t reliable_received, reliable_acknowledged, frame_received, newest_frame;
    uint64_t time_ceiling;
} qa_unified_progress;
bool qa_unified_channel_progress_read(const qa_unified_channel *, qa_unified_progress *, qa_error *);
bool qa_unified_session_checkpoint(const qa_unified_session *, qa_buffer *, qa_error *);
/* The sole runtime codec installs these genuine ops/state on the actual
 * already-restored connection, after the candidate Source supplied its hooks.
 * No attach, callback, transport effect or actor creation occurs here. */
bool qa_unified_session_restore(qa_bytes, qa_network_runtime *, const qa_net_client *,
    const qa_unified_session_hooks *, qa_unified_session **, qa_network_peer_ops *, qa_error *);
bool qa_unified_session_qualified(const qa_unified_session *, const qa_net_client *, qa_error *);
bool qa_unified_session_source_ready(const qa_unified_session *, qa_error *);
/* Final no-fail Source publication/retirement after genuine aggregate
 * admission. Historical restoration never grants callback ownership. */
void qa_unified_session_source_publish(qa_unified_session *);
void qa_unified_session_source_retire(qa_unified_session *);
/* A detached candidate or the exact retired lower owner has no physical
 * Source callback custody. This permits quiet bridge disposal while idle. */
bool qa_unified_session_source_retired(const qa_unified_session *);
uint32_t qa_unified_session_required(const qa_unified_session *);
/* Qualifies one genuinely queued control against the retained unacked bytes
 * or the actual cumulative acknowledgement. It never queues or sends. */
bool qa_unified_session_control_receipt(const qa_unified_session *, uint32_t,
    const qa_unified_document *, qa_error *);
/* Joins the actual readonly CLIENT continuation to its retained receive head,
 * including Source publication that returned a failure before lower commit. */
bool qa_unified_session_client_receipt(const qa_unified_session *, uint32_t epoch,
    bool admitted, bool retired, const qa_unified_document *pending_offer,
    const qa_unified_document *published_frame, const qa_unified_document *pending_frame, qa_error *);
#endif
