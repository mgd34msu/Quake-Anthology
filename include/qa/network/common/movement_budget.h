/* Port of src/network/common/movement-budget.ts. GPL-2.0-or-later.
 * Per-client movement time allowance in the manner of Q2 sv_enforcetime: a command
 * spends its msec (at least 1) from a balance that refills with wall time. */
#ifndef QA_NETWORK_COMMON_MOVEMENT_BUDGET_H
#define QA_NETWORK_COMMON_MOVEMENT_BUDGET_H
#include <stdbool.h>
typedef struct qa_movement_budget {
  double cap_milliseconds, refill_per_millisecond, balance, last;
  bool has_last;
} qa_movement_budget;
/* TS defaults: cap 1800, refill 1.8. */
void qa_movement_budget_init(qa_movement_budget *b, double cap_milliseconds, double refill_per_millisecond);
bool qa_movement_budget_admit(qa_movement_budget *b, double milliseconds, double now);
#endif
