/* Port of src/network/q1/constants.ts functions. GPL-2.0-or-later. */
#include "qa/network/q1/constants.h"
#include <math.h>
#include "qa/foundation/js_truthy.h"
double qa_q1_rint(double x) { return x > 0 ? trunc(x + 0.5) : trunc(x - 0.5); }
double qa_q1_entalpha_encode(double a) {
  if (a == 0) return QA_Q1_ENTALPHA_DEFAULT;
  double v = a * 254 + 1;
  return qa_q1_rint(v < 1 ? 1 : v > 255 ? 255 : v);
}
double qa_q1_entalpha_decode(double a) { return a == QA_Q1_ENTALPHA_DEFAULT ? 1.0 : (a - 1) / 254; }
double qa_q1_entalpha_tosave(double a) {
  return a == QA_Q1_ENTALPHA_DEFAULT ? 0.0 : a == QA_Q1_ENTALPHA_ZERO ? -1.0 : (a - 1) / 254;
}
/* TS `a ? ... : ...`: NaN is falsy. */
double qa_q1_entscale_encode(double a) { return qa_js_truthy(a) ? a * QA_Q1_ENTSCALE_DEFAULT : QA_Q1_ENTSCALE_DEFAULT; }
double qa_q1_entscale_decode(double a) { return a / QA_Q1_ENTSCALE_DEFAULT; }
