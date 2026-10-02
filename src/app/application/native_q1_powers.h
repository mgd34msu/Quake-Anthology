#ifndef QA_APPLICATION_NATIVE_Q1_POWERS_H
#define QA_APPLICATION_NATIVE_Q1_POWERS_H

#include "internal.h"

bool application_native_q1_powerup(void *, qa_actor_id, qa_q1_power,
    double source_expiry, qa_error *);
bool application_native_q1_set_gravity(void *, qa_actor_id, float scale, qa_error *);

#endif
