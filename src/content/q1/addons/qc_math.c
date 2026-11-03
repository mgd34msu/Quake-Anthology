/* Port of src/content/q1/addons/qc-math.ts.
 * quakec_mg3/math.qc: cos() takes degrees and is computed through makevectors. GPL-2.0-or-later. */
#include "qa/content/q1/addons/qc_math.h"

#include <math.h>

double qa_q1ad_qc_cos(double degrees) {
  return (double)(float)cos((double)(float)degrees * 3.141592653589793 / 180);
}
