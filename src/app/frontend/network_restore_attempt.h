#ifndef QA_FRONTEND_NETWORK_RESTORE_ATTEMPT_H
#define QA_FRONTEND_NETWORK_RESTORE_ATTEMPT_H
#include "network_restore.h"

/* Initial UI graph import has no decoded gamestate or CGAME Init tuple.
 * Only the real detached candidate and physical CLIENT prefix admit it. */
bool frontend_network_client_restore_attempt_read(const qa_frontend *,
    frontend_network_client_attempt *, bool *present, qa_error *);
bool frontend_network_client_restore_attempt_current(const qa_frontend *,
    const frontend_network_client_attempt *);
bool frontend_network_client_restore_attempt_services(qa_frontend *,
    const frontend_network_client_attempt *, frontend_network_initial_services_binding *,
    void *lease_context,
    bool (*entered)(void *, const frontend_network_client_attempt *, qa_error *),
    qa_q3_host_client_services *, qa_error *);
bool frontend_network_client_restore_initial_adopt(qa_frontend *,
    const frontend_network_client_attempt *, frontend_remote_q3_initial *,
    frontend_remote_q3_modules *, qa_error *);
bool frontend_network_client_restore_initial_finish(qa_frontend *, qa_error *);
bool frontend_network_client_restore_initial_completed_current(const qa_frontend *,
    const frontend_remote_q3_initial *, const frontend_network_client_attempt *);
#endif
