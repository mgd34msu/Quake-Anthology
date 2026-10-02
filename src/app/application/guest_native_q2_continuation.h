#ifndef QA_APPLICATION_NATIVE_Q2_CONTINUATION_H
#define QA_APPLICATION_NATIVE_Q2_CONTINUATION_H
#include "qa/native_host.h"
struct application_provider;
struct application_native_q2_continuation;
/* Pure declaration qualification; it executes no original module callback. */
bool application_native_q2_continuation_portable(struct application_provider *, qa_error *);
bool application_native_q2_continuation_capture(struct application_provider *,
    const qa_native_checkpoint *, qa_buffer *, qa_error *);
/* Primary source saves own declared scalar values and canonical identities.
 * Callback source saves qualify the actual complete process/HOST capsule;
 * their original private memory is owned by that capsule, without layout guesses. */
bool application_native_q2_continuation_prepare(struct application_provider *,
    const qa_native_checkpoint *, qa_bytes, struct application_native_q2_continuation **, qa_error *);
/* Apply after the source-specific restore path and before observer activation.
 * Callback mode performs no private RAM writes or source callback replay. */
bool application_native_q2_continuation_apply(struct application_provider *,
    const struct application_native_q2_continuation *, qa_error *);
void application_native_q2_continuation_abort(struct application_native_q2_continuation *);
#endif
