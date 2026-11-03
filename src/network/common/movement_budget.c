/* Port of src/network/common/movement-budget.ts. GPL-2.0-or-later. */
#include "qa/network/common/movement_budget.h"
#include <math.h>
#include "qa/foundation/js.h"
void qa_movement_budget_init(qa_movement_budget *b, double cap_milliseconds, double refill_per_millisecond) {
  b->cap_milliseconds = cap_milliseconds;
  b->refill_per_millisecond = refill_per_millisecond;
  b->balance = cap_milliseconds;
  b->last = 0;
  b->has_last = false;
}
bool qa_movement_budget_admit(qa_movement_budget *b, double milliseconds, double now) {
  if (b->has_last)
    b->balance = qa_js_min(b->cap_milliseconds, b->balance + qa_js_max(0, now - b->last) * b->refill_per_millisecond);
  b->last = now;
  b->has_last = true;
  double cost = qa_js_max(1, milliseconds);
  if (cost > b->balance) return false;
  b->balance -= cost;
  return true;
}
