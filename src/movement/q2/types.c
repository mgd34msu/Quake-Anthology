/* Port of src/movement/q2/types.ts. GPL-2.0-or-later. */
#include "qa/movement/q2/types.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "qa/foundation/support.h"

const int qa_q2_axes[3] = {0, 1, 2};
const qa_q2_pm_config qa_q2_pm_config_default = {0, false};

double qa_q2_element(const double *values, size_t length, double index) {
  if (index == trunc(index) && index >= 0 && index < (double)length)
    return values[(size_t)index];
  char text[40];
  if (isnan(index)) {
    snprintf(text, sizeof text, "NaN");
  } else if (isinf(index)) {
    snprintf(text, sizeof text, "%s", index > 0 ? "Infinity" : "-Infinity");
  } else if (index == trunc(index) && fabs(index) < 1e15) {
    snprintf(text, sizeof text, "%lld", (long long)index);
  } else {
    snprintf(text, sizeof text, "%s", QA_JS_NUMBER(index));
  }
  qa_range_error("Quake II movement index %s outside %zu", text, length);
}

qa_q2_cplane qa_q2_plane(void) {
  qa_q2_cplane plane;
  memset(&plane, 0, sizeof plane);
  return plane;
}

bool qa_q2_flag_test(double flags, int mask) {
  return (qa_int32(flags) & mask) != 0;
}
double qa_q2_flag_set(double flags, int mask) {
  return (double)(qa_int32(flags) | mask);
}
double qa_q2_flag_clear(double flags, int mask) {
  return (double)(qa_int32(flags) & ~mask);
}

bool qa_q2_same_hit(qa_trace_hit first, qa_trace_hit second) {
  if (first.kind != second.kind) return false;
  if (first.kind == QA_TRACE_HIT_WORLD) return first.model == second.model;
  if (first.kind == QA_TRACE_HIT_ACTOR) return qa_same_actor(first.actor, second.actor);
  return true;
}
