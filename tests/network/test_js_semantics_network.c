/*
 * JS-semantics regressions in network. Expected values from the TypeScript under bun:
 *   new Int32Array([pmFloatToShort(f)])[0] for f = NaN, 0.062499999999999993, -0.3125,
 *   Infinity, -1e10, 4096.0625 is 0, 0, -2, 32767, -32768, 32767. pmFloatToShort is
 *   clampInt16(Math.round(f * 8)); a peer's NaN float was cast to int32 in C (INT32_MIN on
 *   x86, undefined behavior), and floor(x + 0.5) rounded 0.49999999999999994 up.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdio.h>

#include "qa/network/q2/codecs/q2repro.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                  \
  do {                                                                               \
    checks++;                                                                        \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

int main(void) {
  CHECK(qa_q2repro_pm_float_to_short(NAN) == 0);
  CHECK(qa_q2repro_pm_float_to_short(0.062499999999999993) == 0);
  CHECK(qa_q2repro_pm_float_to_short(-0.3125) == -2);
  CHECK(qa_q2repro_pm_float_to_short(INFINITY) == 32767);
  CHECK(qa_q2repro_pm_float_to_short(-1e10) == -32768);
  CHECK(qa_q2repro_pm_float_to_short(4096.0625) == 32767);
  printf("test_js_semantics_network: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
