/* Port of src/compat/q2/native-mod-invocations.ts. Retirement ends its source
 * invocation; it is never a fabricated native return value. TS generic Result
 * is left in the invoke closure's own context. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_INVOCATIONS_H
#define QA_COMPAT_Q2_NATIVE_MOD_INVOCATIONS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/native_mod_region.h"

typedef enum qa_compat_q2_native_mod_invocation_kind { QA_Q2INV_COMPLETED, QA_Q2INV_RETIRED } qa_compat_q2_native_mod_invocation_kind;
typedef struct qa_compat_q2_native_mod_invocation_scope { void *ctx; bool (*current)(void *ctx); } qa_compat_q2_native_mod_invocation_scope;
typedef struct qa_compat_q2_native_mod_retirement {
  const qa_compat_q2_native_mod_region_authority *authority;
  const qa_compat_q2_native_mod_invocation_scope *scope;
} qa_compat_q2_native_mod_retirement;
typedef struct qa_compat_q2_native_mod_invocations {
  const qa_compat_q2_native_mod_host *host;
  const qa_compat_q2_native_mod_invocation_scope **scopes; size_t scope_count, scope_capacity;
  qa_compat_q2_native_mod_retirement *retired; size_t retired_count, retired_capacity;
} qa_compat_q2_native_mod_invocations;
void qa_compat_q2_native_mod_invocations_init(qa_compat_q2_native_mod_invocations *s, const qa_compat_q2_native_mod_host *host);
void qa_compat_q2_native_mod_invocations_destroy(qa_compat_q2_native_mod_invocations *s);
/* TS run(current, invoke). Other throws propagate through. */
qa_compat_q2_native_mod_invocation_kind qa_compat_q2_native_mod_invocations_run(qa_compat_q2_native_mod_invocations *s, void *current_ctx, bool (*current)(void *), void *invoke_ctx, void (*invoke)(void *));
/* TS guard(authority, invoke). */
void qa_compat_q2_native_mod_invocations_guard(qa_compat_q2_native_mod_invocations *s, const qa_compat_q2_native_mod_region_authority *authority, void *invoke_ctx, void (*invoke)(void *));
#endif
