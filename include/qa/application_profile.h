#ifndef QA_APPLICATION_PROFILE_H
#define QA_APPLICATION_PROFILE_H
#include "qa/application.h"

/* Bind the selected input ConfigStore directory at an idle host boundary.
 * Repeated binding to the retained owner is harmless; an application keeps
 * that input profile through internal map travel. */
bool qa_application_player_profile_bind(qa_application *, qa_fs_root *, qa_error *);
qa_fs_root *qa_application_player_profile_root(const qa_application *);
#endif
