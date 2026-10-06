#ifndef QA_FRONTEND_NATIVE_Q2_MESSAGES_H
#define QA_FRONTEND_NATIVE_Q2_MESSAGES_H
#include "qa/frontend.h"
#include "qa/application_native_q2_delivery.h"
/* Consumes the retained original GAME messages before the sole queue clear. */
bool frontend_native_q2_messages(qa_frontend *, qa_error *);
bool frontend_native_q2_muzzle_sound(qa_frontend *, const qa_application_protocol_event *,
    const qa_application_q2_audience *, const qa_builtin_event *, bool monster,
    qa_q2_edition, qa_error *);
#endif
