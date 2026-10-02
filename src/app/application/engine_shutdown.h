#ifndef QA_APPLICATION_ENGINE_SHUTDOWN_INTERNAL_H
#define QA_APPLICATION_ENGINE_SHUTDOWN_INTERNAL_H
#include "internal.h"
#include "qa/application_engine_shutdown.h"
/* Borrow the held registry only from an entered physical source destructor. */
qa_cvars *application_engine_shutdown_cvars(const application_provider *);
void application_engine_shutdown_release_candidate(qa_application *, const qa_launch_snapshot *);
#endif
