#ifndef QA_APPLICATION_GUEST_CHECKPOINT_H
#define QA_APPLICATION_GUEST_CHECKPOINT_H
#include "internal.h"

bool application_guest_checkpoint_capture(application_provider *, qa_buffer *, qa_error *);
/* Restore only into an isolated prepared provider; failed restore is discarded. */
bool application_guest_checkpoint_restore(application_provider *, qa_bytes, qa_error *);

#endif
