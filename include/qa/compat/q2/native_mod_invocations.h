/* Port of src/compat/q2/native-mod-invocations.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_INVOCATIONS_H
#define QA_COMPAT_Q2_NATIVE_MOD_INVOCATIONS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum qa_compat_q2_native_mod_invocation_kind { QA_Q2INV_COMPLETED, QA_Q2INV_RETIRED } qa_compat_q2_native_mod_invocation_kind;
typedef struct qa_compat_q2_native_mod_invocations { int depth; bool retired_pending; } qa_compat_q2_native_mod_invocations;
void qa_compat_q2_native_mod_invocations_init(qa_compat_q2_native_mod_invocations *s);
/* TS run(): invoke fn; when it returns false the invocation retired. */
qa_compat_q2_native_mod_invocation_kind qa_compat_q2_native_mod_invocations_run(qa_compat_q2_native_mod_invocations *s, void *ctx, bool (*current)(void *), bool (*invoke)(void *));
#endif
