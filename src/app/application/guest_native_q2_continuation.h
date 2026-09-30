#ifndef QA_APPLICATION_NATIVE_Q2_CONTINUATION_H
#define QA_APPLICATION_NATIVE_Q2_CONTINUATION_H
#include "qa/native_host.h"
struct application_provider;
struct application_native_q2_continuation;
/* Pure declaration qualification; it executes no original module callback. */
bool application_native_q2_continuation_portable(struct application_provider *, qa_error *);
bool application_native_q2_continuation_capture(struct application_provider *,
    const qa_native_checkpoint *, qa_buffer *, qa_error *);
/* Identity/base qualification precedes original module import. The prepared
 * token owns scalar values and resolved canonical identities, not addresses. */
bool application_native_q2_continuation_prepare(struct application_provider *,
    const qa_native_checkpoint *, qa_bytes, struct application_native_q2_continuation **, qa_error *);
/* Apply only to the isolated candidate after GAME/baseline/LEVEL/HOST and before
 * observer activation or shared primary adoption. Failure leaves it discardable. */
bool application_native_q2_continuation_apply(struct application_provider *,
    const struct application_native_q2_continuation *, qa_error *);
void application_native_q2_continuation_abort(struct application_native_q2_continuation *);
#endif
