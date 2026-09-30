#ifndef QA_APPLICATION_MATCH_INTENTS_H
#define QA_APPLICATION_MATCH_INTENTS_H
#include "native_maps.h"

typedef struct application_match_intents application_match_intents;
application_match_intents *application_match_intents_create(qa_error *);
void application_match_intents_destroy(application_match_intents *);
bool application_match_intents_idle(const application_match_intents *);
/* Enqueue retains source identity and SELECTED_MAP's actual admission command.
 * NEXT_MAP resolves after the source vote delay at prepare. */
bool application_match_intents_enqueue(application_match_intents *, qa_application *,
    const qa_match_intent *, qa_error *);
/* Only the frontend's drained idle boundary calls prepare/completed. Prepare
 * applies pre-map setters; it neither loads a map nor calls a travel parser. */
bool application_match_intents_prepare(application_match_intents *, qa_application *, qa_error *);
bool application_match_intents_travel_read(const application_match_intents *,
    const application_next_map_plan **, qa_application_travel_request *);
bool application_match_intents_waiting(const application_match_intents *, uint64_t *revision);
bool application_match_intents_queued(application_match_intents *, qa_application *,
    uint64_t travel_revision, qa_error *);
bool application_match_intents_completed(application_match_intents *, qa_application *,
    uint64_t travel_revision, qa_error *);
/* Restore is candidate-only and owns every decoded string/plan. Reconnect is
 * readonly and runs after actual console, mode and travel continuations. */
bool application_match_intents_capture(application_match_intents *, qa_application *,
    qa_buffer *, qa_error *);
bool application_match_intents_restore(application_match_intents *, qa_application *,
    qa_bytes, qa_error *);
bool application_match_intents_reconnect(application_match_intents *, qa_application *, qa_error *);
#endif
