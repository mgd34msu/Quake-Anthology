/* Port of src/app/bootstrap/simulation/random.ts: glibc TYPE_3
 * rand/srand, or Q2 rerelease MT19937 when profile selects it.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_RANDOM_H
#define QA_APP_BOOTSTRAP_SIMULATION_RANDOM_H
#include <stdbool.h>
#include <stdint.h>
#include "qa/contracts/numeric.h"
#include "qa/core/random/q2_rerelease.h"
typedef struct qa_app_sim_source_random_state {
  const char *kind; /* "glibc-random"|"q2-rerelease-mt19937" */
  int32_t words[31]; int front, rear; int64_t draws;
  qa_q2_rerelease_random_state rerelease;
} qa_app_sim_source_random_state;
typedef struct qa_app_sim_source_random qa_app_sim_source_random;
/* seed is uint32 (TS RangeError otherwise); profile "classic"|"q2-rerelease". */
qa_app_sim_source_random *qa_app_sim_source_random_create(double seed, const char *profile);
void qa_app_sim_source_random_destroy(qa_app_sim_source_random *r);
bool qa_app_sim_source_random_has_rerelease(const qa_app_sim_source_random *r);
double qa_app_sim_source_random_next_integer(qa_app_sim_source_random *r);
double qa_app_sim_source_random_next_unit(qa_app_sim_source_random *r);
qa_app_sim_source_random_state qa_app_sim_source_random_checkpoint(qa_app_sim_source_random *r);
void qa_app_sim_source_random_restore(qa_app_sim_source_random *r, qa_app_sim_source_random_state state);
#endif
