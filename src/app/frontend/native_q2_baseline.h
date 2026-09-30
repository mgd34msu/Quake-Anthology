#ifndef QA_FRONTEND_NATIVE_Q2_BASELINE_H
#define QA_FRONTEND_NATIVE_Q2_BASELINE_H
#include "qa/application.h"
#include "qa/persistence_application.h"
/* Root owns the independent scratch application. This lease owns all platform
 * import consumers through failed application/native teardown and must be
 * destroyed only after the scratch source has released every frontend lease. */
bool frontend_native_q2_baseline_create(void *frontend, const qa_application_options *,
    qa_application_options *, void **owned_context, qa_error *);
bool frontend_native_q2_baseline_ready(void *, qa_error *);
bool frontend_native_q2_baseline_destroy(void *, qa_error *);
bool frontend_native_q2_baseline_prepare(void *, qa_application *, qa_actor_owner,
    qa_application_native_baseline_services *, qa_error *);
#endif
