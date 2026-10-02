#ifndef QA_APPLICATION_NETWORK_UNIFIED_SAVE_H
#define QA_APPLICATION_NETWORK_UNIFIED_SAVE_H

#include "network_unified.h"

bool application_unified_server_checkpoint(const application_unified_server *, qa_buffer *, qa_error *);
/* Creates only retained Source continuation against the supplied real graph.
 * The runtime imports QAUS with these hooks, then restore_bind qualifies that
 * exact installed peer. Neither step admits a player or executes GAME. */
bool application_unified_server_restore(qa_bytes, qa_application *, qa_network_runtime *,
    const qa_net_client *, application_unified_server **, qa_error *);
bool application_unified_server_restore_bind(application_unified_server *, qa_unified_session *, qa_error *);
/* Quiet disposal is confined to a decoded owner whose callbacks have never
 * acquired published Source custody. */
bool application_unified_server_restore_dispose(application_unified_server **, qa_error *);

#endif
