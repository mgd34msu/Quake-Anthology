#ifndef QA_APPLICATION_Q3_SOUND_EVENTS_H
#define QA_APPLICATION_Q3_SOUND_EVENTS_H

#include "internal.h"

bool application_q3_sound_events_prepare(application_provider *, qa_error *);
void application_q3_sound_events_destroy(application_provider *);
bool application_q3_sound_events_frame(application_provider *, const qa_source_frame *, qa_error *);
bool application_q3_sound_builtin(qa_application *, const qa_builtin_event *, qa_error *);

#endif
