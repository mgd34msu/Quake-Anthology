#ifndef QA_APPLICATION_NATIVE_Q2_DELIVERY_PRIVATE_H
#define QA_APPLICATION_NATIVE_Q2_DELIVERY_PRIVATE_H

#include "internal.h"

struct application_q2_audience_scratch;
bool application_native_q2_delivery_create(qa_application *, size_t actors, qa_error *);
void application_native_q2_delivery_destroy(qa_application *);

/* Capture runs under the real executing GAME or canonical combat caller.
 * Recipients borrow load-sized scratch until copied into the event stream. */
bool application_native_q2_delivery_capture(application_provider *, const qa_builtin_q2_multicast *,
    qa_vec3 line_end, qa_application_q2_audience *, qa_error *);
struct application_native_q2;
struct qa_q2_server_record;
bool application_native_q2_message_capture(struct application_native_q2 *,
    const qa_native_host_message *, qa_application_q2_protocol_delivery *, qa_error *);
bool application_emit_q2_protocol(application_provider *,
    const qa_application_protocol_event *, const qa_application_q2_protocol_delivery *,
    const qa_native_host_message *, const struct qa_q2_server_record *, qa_error *);
void application_native_q2_delivery_dispose(qa_application_q2_audience *);
bool application_native_q2_delivery_retain(qa_application *,
    const qa_application_q2_audience *, qa_application_q2_audience *, qa_error *);

#endif
