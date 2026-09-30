#ifndef QA_QVM_SAVE_H
#define QA_QVM_SAVE_H

#include "qa/qvm.h"

/* Restore source counters exactly into a newly constructed isolated executor.
 * Its artifact, execution options and candidate-owned callback bindings must
 * already be qualified by their actual owners. No invocation or observed write
 * may have occurred. Host restoration still sees committed guest RAM; failure
 * requires disposing the whole candidate. */
bool qa_qvm_restore_candidate(qa_qvm *, qa_bytes, qa_error *);

typedef struct qa_qvm_saved_function {
    qa_qvm_binding binding;
    uint32_t instruction;
    bool host_invocations;
    qa_qvm_function_hook hook;
    void *context;
} qa_qvm_saved_function;
/* Read-only exact inventory qualification. Every installed function hook must
 * match one supplied source-owned descriptor; observers, resolvers and write
 * watchers require their own owner and are rejected by this inventory. */
bool qa_qvm_checkpoint_functions(const qa_qvm *, const qa_qvm_saved_function *,
    size_t count, qa_error *);

#endif
