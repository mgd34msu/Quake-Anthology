#ifndef QA_APPLICATION_NATIVE_Q1_SPECTATOR_H
#define QA_APPLICATION_NATIVE_Q1_SPECTATOR_H

#include "internal.h"

bool application_native_q1_spectator_begin(application_provider *, qa_actor_id, qa_error *);
bool application_native_q1_spectator_postthink(application_provider *, qa_actor_id,
    const qa_q1_input *, qa_error *);
bool application_native_q1_spectator_track(application_provider *, qa_actor_id,
    bool target_supplied, int32_t client_slot, qa_error *);
bool application_native_q1_spectator_disconnect(application_provider *, qa_actor_id, qa_error *);
bool application_native_q1_client_disconnect(application_provider *, qa_actor_id, qa_error *);

#endif
