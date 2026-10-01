#ifndef QA_APPLICATION_NATIVE_Q3_LOG_H
#define QA_APPLICATION_NATIVE_Q3_LOG_H

#include "internal.h"

/* Source log calls retain a distinct queued operation. The default frontend
 * consumes it without console, HUD or file output, as the source host does. */
bool application_native_q3_log(application_provider *, const char *, qa_error *);
bool application_native_q3_source_log(void *, const char *, qa_error *);

#endif
