#ifndef QA_APPLICATION_SAVE_NATIVE_Q2_RECORD_H
#define QA_APPLICATION_SAVE_NATIVE_Q2_RECORD_H

#include "qa/persistence_application.h"
#include "qa/native.h"

struct application_provider;
bool application_native_q2_save_capture(struct application_provider *, qa_save_purpose,
    const qa_application_native_resource_refs *, qa_buffer *, qa_error *);
bool application_native_q2_save_restore(struct application_provider *, struct application_provider *, qa_bytes,
    const qa_application_options *, const qa_application_persistence_ops *, qa_error *);
/* Pure envelope lookup before native process construction. */
bool application_native_q2_save_resource_recipe(const qa_save_record *, qa_bytes *, qa_error *);
/* Decode the original file or process continuation before source construction. */
bool application_native_q2_save_checkpoint(const qa_save_record *, qa_native_checkpoint *, qa_error *);

#endif
