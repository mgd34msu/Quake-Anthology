#ifndef QA_FRONTEND_NATIVE_Q2_MESSAGES_H
#define QA_FRONTEND_NATIVE_Q2_MESSAGES_H
#include "qa/frontend.h"
#include "qa/application_native_q2_delivery.h"
#include "qa/unified_frame_events.h"
/* Consumes the retained original GAME messages before the sole queue clear. */
bool frontend_native_q2_messages(qa_frontend *, qa_error *);
void frontend_native_q2_print_event(qa_frontend *, qa_actor_owner,
    const qa_unified_q2_protocol_event *, qa_actor_id);
bool frontend_native_q2_muzzle_sound(qa_frontend *, const qa_application_protocol_event *,
    const qa_application_q2_audience *, const qa_builtin_event *, bool monster,
    qa_q2_edition, qa_error *);
#endif
