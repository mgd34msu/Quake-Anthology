#ifndef QA_APPLICATION_NATIVE_Q1_COMPOSITION_BIRTH_H
#define QA_APPLICATION_NATIVE_Q1_COMPOSITION_BIRTH_H

#include "internal.h"

bool application_native_q1_composition_birth(application_provider *, qa_actor_id,
    bool first_admission, qa_error *);
bool application_native_q1_ctf_status(application_provider *, qa_actor_id, qa_error *);
bool application_native_q1_ctf_prethink(application_provider *, qa_actor_id,
    const qa_q1_input *, qa_error *);
bool application_native_q1_ctf_impulse(application_provider *, qa_actor_id,
    const qa_q1_input *, bool *handled, qa_error *);
bool application_native_q1_source_impulse(qa_application *, qa_actor_id,
    const qa_q1_input *, bool *consumed, qa_error *);

#endif
