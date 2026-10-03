#ifndef QA_APPLICATION_EVENTS_SAVE_PRIVATE_H
#define QA_APPLICATION_EVENTS_SAVE_PRIVATE_H

#include "internal.h"
#include "qa/application_events_save.h"

/* The application persistence coordinator owns APPLICATION_PERSISTING for
 * the complete capture/import graph. These calls retain that outer lease. */
bool application_events_save_capture(qa_application *, qa_buffer *, qa_error *);
bool application_events_save_restore(qa_application *, qa_bytes, qa_error *);
bool application_events_save_validate(qa_application *, qa_error *);

#endif
