#ifndef QA_APPLICATION_NATIVE_Q3_REMOTE_ROLE_SAVE_H
#define QA_APPLICATION_NATIVE_Q3_REMOTE_ROLE_SAVE_H
#include "native_q3_remote_role.h"

bool application_native_q3_remote_roles_capture(application_provider *, qa_buffer *, qa_error *);
/* Imports the physical CLIENT heap and private metadata before service readers.
 * Console programmes and compiled service caches have their own actual owners. */
bool application_native_q3_remote_roles_restore_prepare(application_provider *, qa_bytes, qa_error *);
bool application_native_q3_remote_roles_restore_match(application_provider *, qa_bytes, qa_error *);
bool application_native_q3_remote_roles_content_visit(const application_provider *,
    const qa_application_content_visitor *, qa_error *);

#endif
