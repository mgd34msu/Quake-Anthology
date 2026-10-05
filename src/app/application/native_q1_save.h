#ifndef QA_APPLICATION_NATIVE_Q1_SAVE_H
#define QA_APPLICATION_NATIVE_Q1_SAVE_H
#include "internal.h"
#include "qa/q1_save.h"

bool application_q1_native_save_admit(const qa_application *, const qa_product *,
    const qa_q1_save_data *, bool *supported, qa_error *);
bool application_q1_native_save_capture(qa_application *, application_provider *,
    qa_q1_save_data *, qa_error *);
bool application_q1_native_save_fit(qa_application *, application_provider *,
    const qa_q1_save_data *, bool *supported, qa_error *);
bool application_q1_native_save_restore(qa_application *, application_provider *,
    const qa_q1_save_data *, qa_error *);
#endif
