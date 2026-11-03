/*
 * Port check for src/ui/common/controls.ts (D8c): wrapWords and
 * buttonControl. Expected lines are bun's wrapWords output at TS HEAD
 * (2026-11-03). GPL-2.0-or-later.
 */
#include <stdio.h>
#include <string.h>

#include "qa/ui/common/controls.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                \
  do {                                                                             \
    checks++;                                                                      \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

typedef struct wrap_case {
  const char *text;
  size_t width;
  const char *lines[4];
  size_t count;
} wrap_case;

static void test_wrap_words(void) {
  static const wrap_case cases[] = {
      {"", 10, {NULL}, 0},
      {"one two three four", 9, {"one two", "three", "four"}, 3},
      {"  lead and trail  ", 8, {"lead and", "trail "}, 2},
      {"abcdefghijklmnopqrstuvwxyz", 10, {"abcdefghij", "klmnopqrst", "uvwxyz"}, 3},
      {"a bb\tccc\ndddd", 4, {"a bb", "ccc", "dddd"}, 3},
      {"Working\xe2\x80\xa6 done", 8, {"Working\xe2\x80\xa6", "done"}, 2},
      {"x\xc2\xa0y z", 3, {"x y", "z"}, 2},
      {"exactly ten", 11, {"exactly ten"}, 1},
      {"exactlyten next", 10, {"exactlyten", "next"}, 2},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    size_t count = 0;
    char **lines = qa_ui_wrap_words(cases[i].text, cases[i].width, &count);
    CHECK(count == cases[i].count);
    for (size_t j = 0; j < count && j < cases[i].count; j++) {
      CHECK(strcmp(lines[j], cases[i].lines[j]) == 0);
      if (strcmp(lines[j], cases[i].lines[j]) != 0)
        fprintf(stderr, "  case %zu line %zu: \"%s\"\n", i, j, lines[j]);
    }
    qa_ui_wrapped_lines_free(lines, count);
  }
}

static int activations = 0;
static void activate(void *ctx, qa_seat_id seat) {
  (void)seat;
  activations += *(int *)ctx;
}

static void test_button_control(void) {
  int sentinel = 7;
  qa_ui_control control =
      qa_ui_button_control("ui:test:go", "Go", (qa_rect){1, 2, 3, 4}, activate, &sentinel, false);
  CHECK(strcmp(control.id, "ui:test:go") == 0 && strcmp(control.label, "Go") == 0);
  CHECK(strcmp(control.kind, "button") == 0 && control.visible && !control.enabled);
  CHECK(control.rect.x == 1 && control.rect.y == 2 && control.rect.width == 3 && control.rect.height == 4);
  qa_seat_id seat;
  memset(&seat, 0, sizeof seat);
  control.activate_seat(control.ctx, seat);
  CHECK(activations == 7);
}

int main(void) {
  test_wrap_words();
  test_button_control();
  printf("test_ui_controls: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
