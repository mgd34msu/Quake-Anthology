#ifndef QA_APPLICATION_SUPPLIES_SAVE_H
#define QA_APPLICATION_SUPPLIES_SAVE_H
#include "qa/application.h"

/* Captures actual publication-owned admissions and independent mapped ammo
 * counters. Canonical inventory and native player continuations have separate
 * owners. Restore requires their fully reconstructed isolated candidate. */
bool qa_application_supplies_capture(qa_application *, qa_buffer *, qa_error *);
bool qa_application_supplies_restore(qa_application *, qa_bytes, qa_error *);
#endif
