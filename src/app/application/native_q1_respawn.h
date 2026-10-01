#ifndef QA_APPLICATION_NATIVE_Q1_RESPAWN_H
#define QA_APPLICATION_NATIVE_Q1_RESPAWN_H
#include "internal.h"
#include "qa/game_q1_travel.h"

bool application_native_q1_travel_new(application_provider *, qa_actor_id,
    qa_q1_travel_state **, qa_error *);
bool application_native_q1_travel_capture(application_provider *, qa_actor_id,
    qa_q1_travel_state **, qa_error *);
bool application_native_q1_travel_admit(application_provider *, qa_actor_id,
    qa_q1_travel_state *, qa_error *);
bool application_native_q1_request_respawn(void *, qa_actor_id, qa_error *);
bool application_native_q1_respawn_new(application_provider *, qa_actor_id, qa_error *);
bool application_native_q1_suicide(void *, qa_actor_id, qa_error *);
#endif
