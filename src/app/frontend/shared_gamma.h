#ifndef QA_FRONTEND_SHARED_GAMMA_H
#define QA_FRONTEND_SHARED_GAMMA_H
#include "internal.h"
#include "qa/display_settings.h"

typedef struct frontend_shared_gamma frontend_shared_gamma;
/* Actual installed renderer value; frontend option defaults are not authority. */
bool frontend_shared_gamma_read(const qa_frontend *,float *,qa_error *);
/* Retain this same display and renderer. A failed prepare may retain *out;
 * keep both parents alive until checked abort or retirement succeeds. */
bool frontend_shared_gamma_prepare(qa_frontend *,float,frontend_shared_gamma **,qa_error *);
bool frontend_shared_gamma_ready(frontend_shared_gamma *,qa_error *);
/* Pure retained parent/resource witness after successful checked readiness. */
bool frontend_shared_gamma_ready_is(const frontend_shared_gamma *);
bool frontend_shared_gamma_refresh(frontend_shared_gamma *,qa_error *);
/* No native setters, allocation or callbacks after the final ready boundary. */
void frontend_shared_gamma_publish(frontend_shared_gamma *);
bool frontend_shared_gamma_abort(frontend_shared_gamma **,qa_error *);
bool frontend_shared_gamma_finish(frontend_shared_gamma **,qa_error *);
#endif
