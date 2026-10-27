/* Port of src/compat/q2/classic/pmove.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_CLASSIC_PMOVE_H
#define QA_COMPAT_Q2_CLASSIC_PMOVE_H
#include <stdbool.h>
#include "qa/compat/q2/classic/host.h"
#include "qa/contracts/movement.h"
#include "qa/contracts/numeric.h"

typedef struct qa_compat_q2_classic_guest_pmove_options {
  qa_numeric_operations numeric;
  const qa_bounds *character_bounds; /* optional */
  double air_accelerate; bool has_air_accelerate;
  const qa_equipment_movement *equipment; /* optional */
} qa_compat_q2_classic_guest_pmove_options;

void qa_compat_q2_run_classic_guest_pmove(qa_guest_address address, qa_compat_q2_classic_guest_host *host,
                                          qa_compat_q2_classic_guest_pmove_options options);
#endif
