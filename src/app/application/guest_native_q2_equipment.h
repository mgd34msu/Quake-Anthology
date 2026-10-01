#ifndef QA_APPLICATION_GUEST_NATIVE_Q2_EQUIPMENT_H
#define QA_APPLICATION_GUEST_NATIVE_Q2_EQUIPMENT_H

#include "qa/application_native_q2_equipment.h"
struct application_provider;
bool application_q2_guest_equipment_read(struct application_provider *, qa_actor_id,
    qa_application_native_q2_equipment_view *, qa_error *);

#endif
