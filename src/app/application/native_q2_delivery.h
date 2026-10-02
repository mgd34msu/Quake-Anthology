#ifndef QA_APPLICATION_NATIVE_Q2_DELIVERY_PRIVATE_H
#define QA_APPLICATION_NATIVE_Q2_DELIVERY_PRIVATE_H

#include "internal.h"

/* Capture runs under the real executing GAME or canonical combat caller.
 * Temporary recipient storage is released by the caller after arena copy. */
bool application_native_q2_delivery_capture(application_provider *, qa_vec3,
    qa_application_q2_audience *, qa_error *);
struct application_native_q2;
bool application_native_q2_message_capture(struct application_native_q2 *,
    const qa_native_host_message *, qa_application_q2_protocol_delivery *, qa_error *);
bool application_emit_q2_protocol(application_provider *,
    const qa_application_protocol_event *, const qa_application_q2_protocol_delivery *, qa_error *);
void application_native_q2_delivery_dispose(qa_application_q2_audience *);
bool application_native_q2_delivery_retain(qa_application *,
    const qa_application_q2_audience *, qa_application_q2_audience *, qa_error *);
bool application_emit_q2_particles(application_provider *, const qa_builtin_event *,
    qa_vec3 multicast_origin, qa_error *);

#endif
