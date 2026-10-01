#ifndef QA_APPLICATION_EQUIPMENT_EVENTS_PRIVATE_H
#define QA_APPLICATION_EQUIPMENT_EVENTS_PRIVATE_H

#include "qa/application_equipment_events.h"

typedef struct application_equipment_events application_equipment_events;
struct application_equipment_source_event;

/* One queue belongs to one actual retained runtime. Construct before Init;
 * destroy only after its source executors and components have been consumed. */
bool application_equipment_events_create(qa_session *, application_equipment_events **, qa_error *);
void application_equipment_events_destroy(application_equipment_events *);
bool application_equipment_events_publish(void *, const struct application_equipment_source_event *, qa_error *);
size_t application_equipment_events_count(const application_equipment_events *);
uint64_t application_equipment_events_generation(const application_equipment_events *);
bool application_equipment_events_at(const application_equipment_events *, size_t,
    qa_application_equipment_event *);
/* Preflight with every other pending queue before any clear. The application
 * holds its actual idle controller/runtime boundary through the no-fail clear. */
bool application_equipment_events_clear_ready(const application_equipment_events *, qa_error *);
void application_equipment_events_clear(application_equipment_events *);
bool application_equipment_events_capture(const application_equipment_events *, qa_buffer *, qa_error *);
/* Decode into the empty isolated queue. The runtime qualifies every decoded
 * namespace against its actual restored source roster before publication. */
bool application_equipment_events_restore(application_equipment_events *, qa_bytes, qa_error *);

#endif
