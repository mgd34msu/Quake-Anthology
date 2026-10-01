#ifndef QA_APPLICATION_NATIVE_Q3_REMOTE_LIFECYCLE_H
#define QA_APPLICATION_NATIVE_Q3_REMOTE_LIFECYCLE_H
#include "qa/application_q3_factory.h"

/* The actual frontend owner consumes modules, service, media and resource
 * leases before returning success. Failure retains its cleanup continuation. */
typedef struct qa_native_q3_remote_retirement {
    qa_application_q3_remote_source previous;
    void *context;
    bool (*retire)(void *, const qa_application_q3_remote_source *, qa_error *);
} qa_native_q3_remote_retirement;
bool qa_native_q3_remote_client_clear(qa_application *, const qa_native_q3_remote_retirement *,
    qa_application_q3_remote_source *, qa_error *);
bool qa_native_q3_remote_client_rebind(qa_application *, const qa_application_q3_remote_binding *,
    qa_application_q3_remote_source *, qa_error *);
#endif
