#ifndef QA_FRONTEND_NETWORK_UNIFIED_SAVE_H
#define QA_FRONTEND_NETWORK_UNIFIED_SAVE_H

#include "network_unified.h"

bool frontend_network_unified_checkpoint(const frontend_network_unified *, qa_buffer *, qa_error *);
bool frontend_network_unified_restore_prepare(const frontend_network_unified_options *, uint64_t connection_owner,
    qa_bytes, frontend_network_unified **, qa_error *);
bool frontend_network_unified_restore_hooks(frontend_network_unified *, qa_network_runtime *,
    const qa_net_client *, qa_unified_session_hooks *, qa_error *);
bool frontend_network_unified_restore_client_service(frontend_network_unified *,
    struct frontend_network_unified_client_service *, qa_error *);
/* Binds only the authentic lower graph. CLIENT dictionaries and presentation
 * children still restore before final finish grants the imported marker. */
bool frontend_network_unified_restore_lower(frontend_network_unified *, qa_network_runtime *, qa_error *);
/* Borrows the one restored replica prefix while the genuine presentation
 * dictionaries and children are still being imported. */
bool frontend_network_unified_restore_client(frontend_network_unified *, qa_network_runtime *,
    qa_net_client_id *, frontend_remote_unified **, bool *present, qa_error *);
bool frontend_network_unified_restore_finish(frontend_network_unified *, qa_network_runtime *, qa_error *);
bool frontend_network_unified_restore_dispose(frontend_network_unified **, qa_error *);

#endif
