/* Port of src/compat/q2/native-mod-invocations.ts. TS exception-based
 * retirement becomes an explicit false return (C has no throw). */
#include "qa/compat/q2/native_mod_invocations.h"
void qa_compat_q2_native_mod_invocations_init(qa_compat_q2_native_mod_invocations *s) { s->depth=0; s->retired_pending=false; }
qa_compat_q2_native_mod_invocation_kind qa_compat_q2_native_mod_invocations_run(qa_compat_q2_native_mod_invocations *s, void *ctx, bool (*current)(void *), bool (*invoke)(void *)) {
  s->depth++; bool ok=invoke(ctx); s->depth--;
  if (!ok && current && !current(ctx)) return QA_Q2INV_RETIRED;
  return QA_Q2INV_COMPLETED;
}
