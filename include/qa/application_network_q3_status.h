#ifndef QA_APPLICATION_NETWORK_Q3_STATUS_H
#define QA_APPLICATION_NETWORK_Q3_STATUS_H

#include "qa/application_q3_factory.h"
#include "qa/q3_host.h"

typedef struct qa_application_network_q3_cgame_host {
    qa_application_q3_remote_source source;
    qa_q3_host *host;
    qa_q3_host_client_context context;
} qa_application_network_q3_cgame_host;

/* Borrows the actual original or acquired CGAME executor host. A compiled
 * CGAME has no such host and reports present=false. Child service namespaces
 * come from the actual host, independently of its physical CLIENT parent.
 * This observes no status permission and invokes no Cvar_Update or source. */
bool qa_application_network_q3_cgame_host_read(qa_application *,
    const qa_application_q3_remote_source *, qa_application_network_q3_cgame_host *,
    bool *present, qa_error *);
bool qa_application_network_q3_cgame_host_current(qa_application *,
    const qa_application_network_q3_cgame_host *);

#endif
