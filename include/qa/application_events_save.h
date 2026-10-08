#ifndef QA_APPLICATION_EVENTS_SAVE_H
#define QA_APPLICATION_EVENTS_SAVE_H

#include "qa/application.h"

/* Save durable state and content references; transient delivery queues and
 * page IDs are rebuilt on load. No asset bytes are included. */
bool qa_application_events_checkpoint(qa_application *, qa_buffer *, qa_error *);
/* Restore after the actor registry and string table have been installed. */
bool qa_application_events_restore(qa_application *candidate, qa_bytes,
                                   qa_error *);

#endif
