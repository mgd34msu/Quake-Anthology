/*
 * float2int regressions in ui (JS number semantics). Expected values were printed by
 * q1WheelItems under bun: owned is true for items 2^31 + 1 with weaponBits 1, for
 * items 2^32 + 2 with bits 2, for items 2^31 with bits 2^31, and for 3.9 with bits 2;
 * false for NaN items. The C cast (int32_t) of items >= 2^31 was undefined (x86 gave
 * INT32_MIN, so a Q1 player holding IT_SIGIL4 = 1 << 31 lost every wheel weapon).
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                  \
  do {                                                                               \
    checks++;                                                                        \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)
#define CHECK_ABORTS(...)                                                            \
  do {                                                                               \
    checks++;                                                                        \
    pid_t pid = fork();                                                              \
    if (pid == 0) {                                                                  \
      (void)prctl(PR_SET_DUMPABLE, 0);                                               \
      if (freopen("/dev/null", "w", stderr) == NULL) _exit(2);                       \
      __VA_ARGS__;                                                                   \
      _exit(0);                                                                      \
    }                                                                                \
    int status = 0;                                                                  \
    waitpid(pid, &status, 0);                                                        \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {                     \
      failures++;                                                                    \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
    }                                                                                \
  } while (0)

#include "qa/ui/hud/q1_wheel.h"

static double no_float(void *ctx, double offset) { (void)ctx; (void)offset; return 0; }
static const char *label(void *ctx, const qa_ui_q1_wheel_slot *slot) { (void)ctx; (void)slot; return "x"; }

static int owned(double items, double bits) {
  qa_ui_q1_wheel_slot slot;
  memset(&slot, 0, sizeof slot);
  slot.slot = 1;
  slot.has_weapon_bits = true;
  slot.weapon_bits = bits;
  qa_ui_q1_wheel_state state = {NULL, items, no_float, label, NULL};
  qa_ui_q1_wheel_items out;
  memset(&out, 0, sizeof out);
  if (!qa_ui_q1_wheel_items_for(&slot, 1, &state, &out)) return -1;
  int result = out.count == 1 && out.items[0].owned;
  qa_ui_q1_wheel_items_free(&out);
  return result;
}

int main(void) {
  CHECK(owned(2147483649.0, 1) == 1);
  CHECK(owned(4294967298.0, 2) == 1);
  CHECK(owned(2147483648.0, 2147483648.0) == 1);
  CHECK(owned(3.9, 2) == 1);
  CHECK(owned(NAN, 1) == 0);
  printf("test_js_semantics_float2int (ui): %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
