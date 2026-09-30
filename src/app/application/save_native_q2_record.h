#ifndef QA_APPLICATION_SAVE_NATIVE_Q2_RECORD_H
#define QA_APPLICATION_SAVE_NATIVE_Q2_RECORD_H

#include "qa/persistence_application.h"

struct application_provider;
bool application_native_q2_save_capture(struct application_provider *, qa_save_purpose,
                                         qa_buffer *, qa_error *);
bool application_native_q2_save_restore(struct application_provider *, qa_bytes,
    const qa_application_options *, const qa_application_persistence_ops *, qa_error *);
/* Qualify current typed private and HOST state without replaying original
 * exporters or comparing their historical process-address bytes. */
bool application_native_q2_save_matches(struct application_provider *, qa_bytes, qa_error *);

#endif
