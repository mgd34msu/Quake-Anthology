/* Port of src/movement/q2/types.ts. GPL-2.0-or-later. */
#include "qa/movement/q2/types.h"

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
    snprintf(text, sizeof text, "%g", index);
  }
  qa_range_error("Quake II movement index %s outside %zu", text, length);
}

qa_q2_cplane qa_q2_plane(void) {
  qa_q2_cplane plane;
  memset(&plane, 0, sizeof plane);
  return plane;
}
