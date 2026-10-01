#ifndef QA_APPLICATION_GUEST_Q2_CONTROL_H
#define QA_APPLICATION_GUEST_Q2_CONTROL_H

#include "client_outputs.h"

struct application_native_q2;
struct application_q2_control;

/* The declaration admits the capability; activation owns actual source hooks. */
bool application_q2_control_prepare(struct application_native_q2 *, qa_error *);
bool application_q2_control_activate(struct application_native_q2 *, qa_error *);
bool application_q2_control_suspend(struct application_native_q2 *, qa_error *);
bool application_q2_control_close(struct application_native_q2 *, qa_error *);
bool application_q2_control_body_admitted(const struct application_native_q2 *);

/* Copies live publications only while this exact original client can consume them. */
bool application_q2_control_outputs(struct application_native_q2 *, qa_actor_id,
    application_client_outputs *, qa_error *);
bool application_q2_control_crouched(struct application_native_q2 *, qa_actor_id,
    bool *, qa_error *);

#endif
