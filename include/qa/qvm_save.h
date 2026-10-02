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
typedef struct qa_qvm_saved_resolver {
    qa_qvm_binding binding;
    qa_qvm_function_resolver resolver;
    void *context;
} qa_qvm_saved_resolver;
/* Exact complete callback inventory. The optional descriptor names the one
 * real dynamic resolver; its entry selection remains with that source owner.
 * Observers and write watchers still require a separate qualified owner. */
bool qa_qvm_checkpoint_callbacks(const qa_qvm *, const qa_qvm_saved_function *,
    size_t count, const qa_qvm_saved_resolver *, qa_error *);
/* Read-only exact inventory qualification. Every installed function hook must
 * match one supplied source-owned descriptor; observers, resolvers and write
 * watchers require their own owner and are rejected by this inventory. */
bool qa_qvm_checkpoint_functions(const qa_qvm *, const qa_qvm_saved_function *,
    size_t count, qa_error *);

/* Reconstruct saved identities on a fresh isolated executor. The full saved
 * executor envelope must match, and every real installed function must match
 * its constructor descriptor. Identities are distinct, positive and bounded
 * by the saved generation. Qualification precedes the no-fail reassignment;
 * the owner must then adopt every returned identity before RAM restoration. */
bool qa_qvm_restore_candidate_bindings(qa_qvm *, qa_bytes checkpoint,
    const qa_qvm_saved_function *constructed, const qa_qvm_binding *saved,
    size_t count, qa_error *);
bool qa_qvm_restore_candidate_callbacks(qa_qvm *, qa_bytes checkpoint,
    const qa_qvm_saved_function *constructed, const qa_qvm_binding *saved,
    size_t count, const qa_qvm_saved_resolver *constructed_resolver,
    qa_qvm_binding saved_resolver, qa_error *);

#endif
