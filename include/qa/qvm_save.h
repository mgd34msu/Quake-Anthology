#ifndef QA_QVM_SAVE_H
#define QA_QVM_SAVE_H

#include "qa/qvm.h"

/* Restore source counters exactly into a newly constructed isolated executor.
 * Its artifact, execution options and candidate-owned callback bindings must
 * already be qualified by their actual owners. No invocation or observed write
 * may have occurred. Host restoration still sees committed guest RAM; failure
 * requires disposing the whole candidate. */
bool qa_qvm_restore_candidate(qa_qvm *, qa_bytes, qa_error *);

#endif
