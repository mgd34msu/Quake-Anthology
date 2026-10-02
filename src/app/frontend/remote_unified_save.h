#ifndef QA_FRONTEND_REMOTE_UNIFIED_SAVE_H
#define QA_FRONTEND_REMOTE_UNIFIED_SAVE_H
#include "remote_unified.h"

bool frontend_remote_unified_checkpoint(const frontend_remote_unified *,
    qa_application_content_graph *, qa_buffer *, qa_error *);
/* The actual CLIENT service and generic connection table already exist.
 * Imports private identities and retained recipes before presentation children
 * claim their graph references. Returned partial owners require checked disposal. */
bool frontend_remote_unified_restore_prefix(qa_frontend *, const frontend_remote_unified_options *,
    const qa_net_client *, qa_application_content_graph *, qa_bytes,
    frontend_remote_unified **, qa_error *);
/* Presentation children and the genuine lower session restore first. */
bool frontend_remote_unified_restore_bind(frontend_remote_unified *, qa_unified_session *, qa_error *);
bool frontend_remote_unified_restore_pending(const frontend_remote_unified *);
bool frontend_remote_unified_checkpoint_current(const frontend_remote_unified *, qa_error *);
bool frontend_remote_unified_qualified(const frontend_remote_unified *, qa_network_runtime *,
    const qa_net_client *, qa_error *);
bool frontend_remote_unified_restore_dispose(frontend_remote_unified **, qa_error *);

#endif
