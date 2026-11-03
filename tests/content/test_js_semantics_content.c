/*
 * JS-semantics regressions in content. Expected values from the TypeScript under bun:
 *   millisecondSum(-0.0025, 0) is -0.002 (Math.round(-2.5) is -2; C round() gave -3),
 *   millisecondSum(-0.0005, 0) is 0, millisecondSum(1.0005, 0.1) is 1.101.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdio.h>

#include "qa/content/q2/foundation/weapons/generic_frame.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                  \
  do {                                                                               \
    checks++;                                                                        \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

int main(void) {
  CHECK(qa_q2w_millisecond_sum(-0.0025, 0) == -0.002);
  double zero = qa_q2w_millisecond_sum(-0.0005, 0);
  CHECK(zero == 0 && !signbit(zero));
  CHECK(qa_q2w_millisecond_sum(1.0005, 0.1) == 1.101);
  printf("test_js_semantics_content: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
