#ifndef QA_NETWORK_Q1_SESSION_SAVE_H
#define QA_NETWORK_Q1_SESSION_SAVE_H
#include "qa/network_q1_session.h"
#include "qa/network_q1_download_save.h"

/* Native negotiation state only. NQ borrows the exact admitted candidate seat
 * identity, and QW signon binds actual candidate host callbacks without calling
 * them. Installed QW file downloads retain their actual immutable resource
 * identity and offset; other source download callbacks remain unsupported.
 * Outputs must be empty; no signon command or
 * source setup is replayed. Precache output owns every native copied name. */
bool qa_nq_signon_checkpoint(const qa_nq_signon *, qa_buffer *, qa_error *);
bool qa_nq_signon_restore_checkpoint(qa_bytes, const qa_nq_signon *candidate_identity,
    qa_nq_signon *, qa_error *);
bool qa_qw_signon_checkpoint(const qa_qw_signon *, qa_buffer *, qa_error *);
bool qa_qw_signon_restore_checkpoint(qa_bytes, const qa_qw_signon_host *, bool donor_wide,
    const qa_qw_download_admission *,
    qa_qw_signon **, qa_error *);
bool qa_qw_precache_checkpoint(const qa_qw_precache *, qa_buffer *, qa_error *);
bool qa_qw_precache_restore_checkpoint(qa_bytes, qa_net_protocol_id,
    qa_qw_precache **, qa_error *);
#endif
