#ifndef QA_Q3_NATIVE_TRAJECTORY_H
#define QA_Q3_NATIVE_TRAJECTORY_H

#include "qa/network_q3.h"
#include "qa/physics.h"

/* Presentation ABI conversion only; the common trajectory evaluator owns the
 * source arithmetic, wrapped times and trajectory rules. */
bool q3n_trajectory(const qa_q3_trajectory *, int32_t time, qa_vec3 *, qa_error *);
bool q3n_trajectory_delta(const qa_q3_trajectory *, int32_t time, qa_vec3 *, qa_error *);

#endif
