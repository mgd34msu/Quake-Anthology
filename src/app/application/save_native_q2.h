#ifndef QA_APPLICATION_SAVE_NATIVE_Q2_H
#define QA_APPLICATION_SAVE_NATIVE_Q2_H

#include "qa/persistence_application.h"

struct application_provider;
struct application_native_q2_scratch;
bool application_create_native_baseline(const qa_application_options *, const qa_strings *,
                                         qa_application **, qa_error *);
/* The enclosing candidate retains the full scratch graph immediately. It is
 * never initialized or map-published before original GAME reconstruction. */
bool application_native_q2_scratch_prepare(struct application_provider *,
    const qa_launch_snapshot *, qa_application_native_baseline_services *,
    struct application_native_q2_scratch **, qa_error *);
bool application_native_q2_scratch_begin(struct application_native_q2_scratch *, qa_error *);
bool application_native_q2_scratch_spawn(struct application_native_q2_scratch *,
                                         const char *, const char *, const char *, qa_error *);
bool application_native_q2_scratch_end(struct application_native_q2_scratch *, qa_error *);
/* False retains every remaining owner for qa_application_destroy retry. */
bool application_native_q2_baselines_destroy(qa_application *, qa_error *);

#endif
