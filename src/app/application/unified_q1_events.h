#ifndef QA_APPLICATION_UNIFIED_Q1_EVENTS_H
#define QA_APPLICATION_UNIFIED_Q1_EVENTS_H

#include "internal.h"

bool application_unified_q1_event(qa_application *, const qa_builtin_event *, qa_error *);
bool application_unified_q1_sound_precache(void *, const char *, qa_error *);
bool application_unified_q1_precache_reset(void *, qa_error *);

#endif
