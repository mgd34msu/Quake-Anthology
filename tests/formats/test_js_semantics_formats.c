/*
 * Q1 MD5 skin paths: `progs/${shader}_${String(group).padStart(2, "0")}_...`, values
 * printed by bun. The C %02d of (int)group is undefined past int range.
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/formats/q3_model/replacements.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    checks++;                                                         \
    if (!(cond)) {                                                    \
      failures++;                                                     \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                 \
  } while (0)

static void path_is(const char *shader, double group, double frame, const char *expected) {
  char *path = qa_q1_md5_skin_path(shader, group, frame);
  CHECK(strcmp(path, expected) == 0);
  if (strcmp(path, expected) != 0) fprintf(stderr, "  got %s want %s\n", path, expected);
  free(path);
}
int main(void) {
  path_is("x", 3, 12, "progs/x_03_12");
  path_is("x", 4294967296.0, 0, "progs/x_4294967296_00");
  path_is("x", 1e21, 100, "progs/x_1e+21_100");
  printf("test_js_semantics_formats: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
