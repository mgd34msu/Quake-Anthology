/*
 * prediction/runtime.ts shortAngle: (word << 16 >> 16) * 360 / 65536 runs the
 * shifts in int32, sign-extending the low 16 bits. Expected values printed by
 * bun for the same expression. GPL-2.0-or-later.
 */
#include <math.h>
#include <stdio.h>

#include "qa/app/bootstrap/simulation/prediction/runtime.h"

static int failures = 0, checks = 0;
#define SAME(actual, expected)                                                             \
  do {                                                                                     \
    double got_ = (actual), want_ = (expected);                                            \
    checks++;                                                                              \
    if (!(got_ == want_ && signbit(got_) == signbit(want_))) {                             \
      failures++;                                                                          \
      fprintf(stderr, "FAIL %s:%d: %s = %.17g, want %.17g\n", __FILE__, __LINE__, #actual, \
              got_, want_);                                                                \
    }                                                                                      \
  } while (0)

int main(void) {
  SAME(qa_app_prediction_short_angle(65535), -0.0054931640625); /* int64 shifts gave 359.99... */
  SAME(qa_app_prediction_short_angle(32768), -180);
  SAME(qa_app_prediction_short_angle(32767), 179.9945068359375);
  SAME(qa_app_prediction_short_angle(16384), 90);
  SAME(qa_app_prediction_short_angle(40000), -140.2734375);
  SAME(qa_app_prediction_short_angle(-1), -0.0054931640625);
  SAME(qa_app_prediction_short_angle(98304.7), -180);
  SAME(qa_app_prediction_short_angle(NAN), 0); /* ToInt32(NaN) is 0; the cast was undefined */
  printf("test_js_semantics_bitwise (app): %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
