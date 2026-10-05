#ifndef QA_FRONTEND_NETWORK_RESTORE_H
#define QA_FRONTEND_NETWORK_RESTORE_H
#include "network_presentation.h"
#include "remote_q3_initial.h"

/* Normal services rebuilt against the live native socket, without saved
 * connection, browser or prediction caches. */
bool frontend_network_create_detached(qa_frontend *,const qa_frontend *,qa_error *);
bool frontend_network_rebuild_ready(const qa_frontend *,const qa_frontend *,qa_error *);

/* Candidate-only data binding. It exposes the genuinely imported native
 * connection, physical CLIENT and pending content without claiming Init. */
bool frontend_network_client_restore_domain_read(const qa_frontend *,
    frontend_network_client_domain *, qa_error *);
bool frontend_network_client_restore_domain_current(const qa_frontend *,
    const frontend_network_client_domain *);
/* Adopt the imported parent before child restoration can fail. Completion
 * remains private until the actual media owners and lower cut qualify. */
bool frontend_network_client_restore_adopt(qa_frontend *,
    const frontend_network_client_domain *, frontend_remote_q3 *, qa_error *);
bool frontend_network_client_restore_finish(qa_frontend *, qa_error *);

/* Every retained initial parent is represented, including partial creation.
 * These observations run only under the genuine resource inventory fence. */
bool frontend_network_initial_metadata_read(const qa_frontend *,
    frontend_network_client_attempt *, frontend_remote_q3_initial **,
    frontend_remote_q3_modules **, bool *present, qa_error *);
bool frontend_network_initial_metadata_current(const qa_frontend *,
    const frontend_network_client_attempt *, const frontend_remote_q3_initial *,
    const frontend_remote_q3_modules *);
#endif
