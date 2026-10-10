#ifndef QA_APPLICATION_NATIVE_Q2_PROTOCOL_RESOURCES_H
#define QA_APPLICATION_NATIVE_Q2_PROTOCOL_RESOURCES_H
#include "guest_native_q2_private.h"
typedef struct application_native_q2_protocol_resources {
    qa_application_protocol_resource_reference *rows;
    size_t count, capacity;
    qa_application_protocol_reference *references;
    size_t reference_count, reference_capacity;
} application_native_q2_protocol_resources;
bool application_native_q2_protocol_resources_capture(struct application_native_q2 *,
    qa_bytes, const qa_application_protocol_reference *, size_t reference_count,
    const qa_native_host_message *, application_native_q2_protocol_resources *, qa_error *);
#endif
