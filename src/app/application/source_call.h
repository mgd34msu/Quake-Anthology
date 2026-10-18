#ifndef QA_APPLICATION_SOURCE_CALL_H
#define QA_APPLICATION_SOURCE_CALL_H

#include "qa/native_observe.h"
#include "qa/qvm.h"

typedef struct application_source_call {
    const qa_qvm_call *qvm;
    uint64_t entry;
    const qa_native_value *arguments;
    size_t count;
    void *context;
    bool (*proceed)(void *, const qa_native_value *, size_t, qa_error *);
    bool (*cancel)(void *, qa_error *);
    bool (*cancelled)(void *, bool *, qa_error *);
} application_source_call;

typedef struct application_source_qvm_call {
    const qa_qvm_call *call;
    int32_t *result;
} application_source_qvm_call;

typedef struct application_source_native_call {
    qa_native_entry_observer *binding;
    const qa_native_value *arguments;
    size_t count;
    qa_native_value *result;
    qa_error cancellation;
    bool cancelling;
} application_source_native_call;

bool application_source_qvm_arguments(const qa_qvm_call *, const qa_native_value_type *,
    size_t, qa_native_value *, qa_error *);
bool application_source_qvm_proceed(void *, const qa_native_value *, size_t, qa_error *);
bool application_source_qvm_cancel(void *, qa_error *);
bool application_source_qvm_cancelled(void *, bool *, qa_error *);
bool application_source_native_proceed(void *, const qa_native_value *, size_t, qa_error *);
bool application_source_native_cancel(void *, qa_error *);
bool application_source_native_cancelled(void *, bool *, qa_error *);

#endif
