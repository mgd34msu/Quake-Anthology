#ifndef QA_APPLICATION_GUEST_Q3_SAVE_H
#define QA_APPLICATION_GUEST_Q3_SAVE_H

#include "guest_q3_private.h"

/* Actual application-side Q3 client/server continuation. This nested owner
 * does not replace the role executors, host handles or shared services. */
bool application_guest_q3_state_capture(application_provider *, qa_buffer *, qa_error *);
bool application_guest_q3_state_restore(application_provider *, qa_bytes, qa_error *);

#endif
