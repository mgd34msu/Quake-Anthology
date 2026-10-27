/* Port of src/compat/q2/native-mod-provider.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_PROVIDER_H
#define QA_COMPAT_Q2_NATIVE_MOD_PROVIDER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/foundation/support.h"

void qa_compat_q2_validate_native_mod_declaration(const qa_native_mod_declaration *declaration);
typedef struct qa_compat_q2_native_mod_provider { void *host_ctx; const qa_native_mod_declaration *declaration; bool closed; } qa_compat_q2_native_mod_provider;
void qa_compat_q2_native_mod_provider_init(qa_compat_q2_native_mod_provider *s, void *ctx, const qa_native_mod_declaration *d);
void qa_compat_q2_native_mod_provider_close(qa_compat_q2_native_mod_provider *s);
#endif
