#ifndef QA_APPLICATION_NATIVE_Q2_BASELINE_H
#define QA_APPLICATION_NATIVE_Q2_BASELINE_H
#include "qa/native_host.h"
struct application_provider;
struct application_native_q2_baseline;
/* The outer save producer owns both isolated applications until end or abort.
 * Original GAME restore precedes begin; original LEVEL restore runs after spawn
 * while this phase remains active. HOST restore follows successful end. */
bool application_native_q2_baseline_begin(struct application_provider *target,
    struct application_provider *baseline, struct application_native_q2_baseline **, qa_error *);
bool application_native_q2_baseline_spawn(struct application_native_q2_baseline *,
    const char *map, const char *entities, const char *spawn, qa_error *);
bool application_native_q2_baseline_end(struct application_native_q2_baseline *, qa_error *);
/* Rejected Shutdown retains both owners. Admitted destruction consumes target
 * source ownership even on a backend fault, then releases the phase. */
bool application_native_q2_baseline_abort(struct application_native_q2_baseline *, qa_error *);
#endif
