#ifndef QA_APPLICATION_NATIVE_Q2_PRIVATE_STATE_H
#define QA_APPLICATION_NATIVE_Q2_PRIVATE_STATE_H
#include "qa/native_host.h"
struct application_native_q2;
struct application_q2_private_state;
bool application_q2_private_qualified(struct application_native_q2 *, qa_error *);
bool application_q2_private_capture(struct application_native_q2 *, qa_buffer *, qa_error *);
bool application_q2_private_prepare(struct application_native_q2 *, qa_bytes,
    struct application_q2_private_state **, qa_error *);
bool application_q2_private_client_matches(const struct application_q2_private_state *,
    uint32_t slot, bool present, bool connected, bool spawned, qa_error *);
bool application_q2_private_apply(struct application_native_q2 *,
    struct application_q2_private_state *, qa_error *);
void application_q2_private_free(struct application_q2_private_state *);
#endif
