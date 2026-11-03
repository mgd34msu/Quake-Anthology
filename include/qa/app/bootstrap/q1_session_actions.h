/* Port of src/app/bootstrap/q1-session-actions.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q1_SESSION_ACTIONS_H
#define QA_APP_BOOTSTRAP_Q1_SESSION_ACTIONS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
#include "qa/contracts/identity.h"
typedef struct qa_app_seat_actor { qa_actor_id actor; qa_seat_id seat; } qa_app_seat_actor;
/* Returns "q1", "q2" or NULL (TS null). */
const char *qa_app_source_level_completion(const qa_app_sim_presentation_event *source);
/* Writes matching seats into out (capacity seat_count); returns count. */
size_t qa_app_q1_session_departures(const qa_app_sim_presentation_event *events, size_t event_count,
    const qa_app_seat_actor *seats, size_t seat_count, qa_seat_id *out);
#endif
