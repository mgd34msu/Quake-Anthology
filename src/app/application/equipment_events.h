#ifndef QA_APPLICATION_EQUIPMENT_EVENTS_PRIVATE_H
#define QA_APPLICATION_EQUIPMENT_EVENTS_PRIVATE_H

#include "qa/application_equipment_events.h"

typedef struct application_equipment_events {
    qa_application *application;
    qa_session *session;
    uint64_t identity;
} application_equipment_events;

/* Runtime custody only; all events and text live in the application ring. */
bool application_equipment_events_create(qa_application *, qa_session *, application_equipment_events **, qa_error *);
void application_equipment_events_destroy(application_equipment_events *);
bool application_equipment_events_publish(application_equipment_events *, const qa_application_equipment_event *, qa_error *);
bool application_equipment_events_capture(const application_equipment_events *, qa_buffer *, qa_error *);
/* Decode into the empty isolated queue. The runtime qualifies every decoded
 * namespace against its actual restored source roster before publication. */
bool application_equipment_events_restore(application_equipment_events *, qa_bytes, qa_error *);

#endif
