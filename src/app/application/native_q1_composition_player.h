#ifndef QA_APPLICATION_NATIVE_Q1_COMPOSITION_PLAYER_H
#define QA_APPLICATION_NATIVE_Q1_COMPOSITION_PLAYER_H

#include "internal.h"

bool application_native_q1_rogue_state(void *, qa_actor_owner, qa_actor_id,
    qa_actor_id *, qa_error *);
bool application_native_q1_rogue_state_current(void *, qa_actor_owner, qa_actor_id,
    qa_actor_id, qa_error *);
bool application_native_q1_rogue_number_read(void *, qa_actor_owner, qa_actor_id,
    qa_actor_id, uint32_t, double *, qa_error *);
bool application_native_q1_rogue_number_write(void *, qa_actor_owner, qa_actor_id,
    qa_actor_id, uint32_t, double, qa_error *);

#endif
