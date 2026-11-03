/*
 * float2int regressions in content. Expected values printed by the TypeScript under bun:
 *   latchQ2WeaponButtons({buttons: 0, latchedButtons: 0}, 2 ** 32 + 1): latchedButtons 1
 *     (ToInt32 wraps; the C (int32_t) cast of 4294967297 was undefined), buttons 4294967297;
 *   then latchQ2WeaponButtons(..., NaN): latchedButtons 1, buttons NaN;
 *   latchQ2WeaponButtons({buttons: 1, latchedButtons: 2147483648}, 3): -2147483646;
 *   String.fromCharCode(97 + NaN) is "\u0000" (addonLightStyle with a NaN fraction).
 * GPL-2.0-or-later.
 */
#include <math.h>
#include <stdio.h>

#include "qa/content/q1/addons/lights.h"
#include "qa/content/q2/foundation/weapons/turn.h"

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                  \
  do {                                                                               \
    checks++;                                                                        \
    if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

int main(void) {
  qa_q2w_turn_state s = {0, 0, false};
  qa_q2w_latch_buttons(&s, 4294967297.0);
  CHECK(s.latched_buttons == 1 && s.buttons == 4294967297.0);
  qa_q2w_latch_buttons(&s, NAN);
  CHECK(s.latched_buttons == 1 && isnan(s.buttons));
  qa_q2w_turn_state t = {1, 2147483648.0, false};
  qa_q2w_latch_buttons(&t, 3);
  CHECK(t.latched_buttons == -2147483646.0);
  CHECK(qa_q1ad_light_style(NAN, true) == '\0');
  CHECK(qa_q1ad_light_style(0.5, false) == 'g');
  printf("test_js_semantics_float2int: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
