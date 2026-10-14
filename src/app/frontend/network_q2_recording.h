#ifndef QA_FRONTEND_NETWORK_Q2_RECORDING_H
#define QA_FRONTEND_NETWORK_Q2_RECORDING_H

#include "demo_service.h"
#include "qa/application_network_q2.h"

typedef struct qa_frontend qa_frontend;
typedef struct frontend_network_q2_recording frontend_network_q2_recording;

/* The returned-frame Network owner keeps the actual publisher alive. Both
 * recording feeds observe its completed Source without admitting a viewer. */
bool frontend_network_q2_recording_create(qa_frontend *, qa_application_network_q2 *,
    frontend_network_q2_recording **, qa_error *);
/* Publish the preceding Source before replacing it at a map boundary. Existing
 * files stay attached and receive the new Source's original signon. */
bool frontend_network_q2_recording_rebind(frontend_network_q2_recording *,
    qa_application_network_q2 *, qa_error *);
bool frontend_network_q2_recording_source(frontend_network_q2_recording *,
    frontend_demo_action, qa_fs_root *, frontend_demo_record_source *, qa_error *);
bool frontend_network_q2_recording_publish(frontend_network_q2_recording *, qa_error *);
bool frontend_network_q2_recording_idle(const frontend_network_q2_recording *);
/* The demo service detaches/releases its feeds before the publisher closes. */
bool frontend_network_q2_recording_destroy(frontend_network_q2_recording **, qa_error *);

#endif
