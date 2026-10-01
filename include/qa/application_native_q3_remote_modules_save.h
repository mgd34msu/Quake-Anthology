#ifndef QA_APPLICATION_NATIVE_Q3_REMOTE_MODULES_SAVE_H
#define QA_APPLICATION_NATIVE_Q3_REMOTE_MODULES_SAVE_H
#include "qa/application_q3_factory.h"

/* Row-owned bytes remain retained until the actual resource-backed module
 * constructor imports them and completes its checked host/media restoration. */
bool qa_native_q3_remote_client_modules_restore_read(qa_application *,
    const qa_application_q3_remote_source *, qa_bytes *, bool *, qa_error *);
bool qa_native_q3_remote_client_modules_restore_complete(qa_application *,
    const qa_application_q3_remote_source *, qa_error *);
#endif
