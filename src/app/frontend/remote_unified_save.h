#ifndef QA_FRONTEND_REMOTE_UNIFIED_SAVE_H
#define QA_FRONTEND_REMOTE_UNIFIED_SAVE_H
#include "remote_unified.h"

bool frontend_remote_unified_restore_pending(const frontend_remote_unified *);
bool frontend_remote_unified_checkpoint_current(const frontend_remote_unified *, qa_error *);
bool frontend_remote_unified_qualified(const frontend_remote_unified *, qa_network_runtime *,
    const qa_net_client *, qa_error *);
bool frontend_remote_unified_restore_dispose(frontend_remote_unified **, qa_error *);

#endif
