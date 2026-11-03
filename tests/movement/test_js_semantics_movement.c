/*
 * pmove_fixed command time, Math.imul(Math.trunc(((t + msec - 1) | 0) / msec), msec),
 * against values printed by bun. The C cast trunc(..) * (int64_t)msec, which is
 * undefined for a zero msec (Infinity * 0) instead of 0.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/movement/q3/prediction.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

int main(void) {
  static const double rows[][3] = {
      {1000, 16, 1008}, {1001, 16, 1008}, {1000, 0, 0}, {0, 0, 0}, {1000, 16.5, 976},
      {1000, NAN, 0}, {2147483647, 8, -2147483640}, {-50, 16, -32}, {1000, -16, 976},
      {5e9, 33, 705032724}};
  for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
    double got = qa_q3_fixed_server_time(rows[i][0], rows[i][1]);
    CHECK(got == rows[i][2] && signbit(got) == signbit(rows[i][2]));
  }
  printf("test_js_semantics_movement: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
