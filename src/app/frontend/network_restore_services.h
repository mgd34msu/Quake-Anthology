#ifndef QA_FRONTEND_NETWORK_RESTORE_SERVICES_H
#define QA_FRONTEND_NETWORK_RESTORE_SERVICES_H
#include "network_restore.h"

typedef struct frontend_network_restore_services_binding {
    qa_frontend *frontend;
    qa_application *application;
    const void *network;
    frontend_network_client_domain domain;
    void *context;
    bool (*entered)(void *, const frontend_network_client_domain *, qa_error *);
} frontend_network_restore_services_binding;
/* The imported per-role lease retains this binding through checked teardown.
 * It begins in the real detached graph, then follows ordinary publication. */
bool frontend_network_client_restore_services(qa_frontend *,
    const frontend_network_client_domain *, frontend_network_restore_services_binding *,
    void *lease_context,
    bool (*entered)(void *, const frontend_network_client_domain *, qa_error *),
    qa_q3_host_client_services *, qa_error *);
#endif
