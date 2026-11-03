/*
 * Confirmation-gate review checks for src/foundation. Each expected value
 * comes from running the TypeScript under bun. GPL-2.0-or-later.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "qa/foundation.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

/* StampSet.begin re-zeroes once the epoch reaches 0xffffffff (TS:
 * 4294967295 true true 4294967295 1 / 1 false 0 0 true 1). */
static void test_stamp_epoch_ceiling(void) {
  qa_stamp_set set;
  CHECK(qa_stamp_set_init(&set, 4));
  CHECK(qa_stamp_set_mark(&set, 2));
  set.epoch = 0xfffffffeu;
  qa_stamp_set_begin(&set);
  CHECK(set.epoch == 4294967295u);
  CHECK(qa_stamp_set_mark(&set, 0));
  CHECK(qa_stamp_set_test(&set, 0));
  CHECK(qa_stamp_set_value(&set, 0) == 4294967295u);
  CHECK(qa_stamp_set_value(&set, 2) == 1);
  qa_stamp_set_begin(&set);
  CHECK(set.epoch == 1);
  CHECK(!qa_stamp_set_test(&set, 0));
  CHECK(qa_stamp_set_value(&set, 0) == 0);
  CHECK(qa_stamp_set_value(&set, 2) == 0);
  CHECK(qa_stamp_set_mark(&set, 0));
  CHECK(qa_stamp_set_value(&set, 0) == 1);
  qa_stamp_set_destroy(&set);
}

int main(void) {
  test_stamp_epoch_ceiling();
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
