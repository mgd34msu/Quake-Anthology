#ifndef QA_APPLICATION_UNIFIED_Q2_EVENTS_H
#define QA_APPLICATION_UNIFIED_Q2_EVENTS_H

#include "internal.h"
#include "unified_output_json.h"
#include "qa/network_q2_messages.h"

bool application_unified_q2_protocol_event(application_provider *,
    const qa_application_protocol_event *, const qa_application_q2_protocol_delivery *, qa_error *);

bool application_unified_q2_temporary_json(application_unified_json *,
    const qa_q2_temp_entity *, bool rerelease, const qa_actor_id actors[7], qa_error *);

#endif
