#ifndef QA_APPLICATION_UNIFIED_Q2_EVENTS_H
#define QA_APPLICATION_UNIFIED_Q2_EVENTS_H

#include "internal.h"
#include "qa/network_q2_messages.h"

bool application_unified_q2_protocol_event(application_provider *,
    const qa_application_protocol_event *, const qa_application_q2_protocol_delivery *, qa_error *);


#endif
