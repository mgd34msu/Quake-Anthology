/* TS drift D7 checks for src/world: modCallbackInputs (src/world/session/mod-callback-inputs.ts,
 * table read in full; QC and QVM mod validation share it), the Q2 null surface and the
 * same-family geometry mask. GPL-2.0-or-later. */
#include <stdio.h>
#include <string.h>
#include "qa/world/collision/contents.h"
#include "qa/world/session/mod_callback_inputs.h"

static int failures, checks;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); } } while (0)

static bool set_is(const char *stage, const char *operation, const char *const *expected, size_t n) {
  const char *got[16];
  size_t count = qa_mod_callback_inputs(stage, operation, got, 16);
  if (count != n) return false;
  for (size_t i = 0; i < n; i++) if (strcmp(got[i], expected[i]) != 0) return false;
  return true;
}
static void test_callback_inputs(void) {
  const char *damage[] = {"self", "time", "result", "attacker", "inflictor", "amount", "knockback", "direction", "point", "normal"};
  CHECK(set_is("observe", "damage", damage, 10));
  const char *give[] = {"self", "time", "item", "amount"};
  CHECK(set_is("replace", "inventory.give", give, 4));
  CHECK(set_is("replace", "inventory.consume", give, 4));
  const char *use[] = {"self", "time", "other", "activator"};
  CHECK(set_is("before", "actor.use", use, 4));
  const char *touch[] = {"self", "time", "other"};
  CHECK(set_is("before", "actor.touch", touch, 3));
  const char *think[] = {"self", "time", "result", "elapsed"};
  CHECK(set_is("observe", "actor.think", think, 4));
  const char *pain[] = {"self", "time", "attacker", "amount", "knockback"};
  CHECK(set_is("before", "actor.pain", pain, 5));
  const char *die[] = {"self", "time", "attacker", "inflictor", "amount", "knockback", "point"};
  CHECK(set_is("before", "actor.die", die, 7));
  CHECK(qa_mod_callback_input_available("observe", "damage", "normal"));
  CHECK(!qa_mod_callback_input_available("before", "actor.touch", "result"));
}
static void test_contents(void) {
  CHECK(qa_q2_null_surface.flags == 0 && qa_q2_null_surface.value == 0 && strcmp(qa_q2_null_surface.name, "") == 0);
  qa_trace_policy policy; memset(&policy, 0, sizeof policy);
  policy.kind = "q2"; policy.contents_mask = 0x02010003 + 4294967296.0; /* | 0 wraps */
  CHECK(qa_geometry_mask(policy, "q2") == (double)0x02010003);
}
int main(void) {
  test_callback_inputs();
  test_contents();
  printf("test_d7_drift: %d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
