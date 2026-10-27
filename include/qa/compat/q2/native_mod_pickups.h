/* Port of src/compat/q2/native-mod-pickups.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_PICKUPS_H
#define QA_COMPAT_Q2_NATIVE_MOD_PICKUPS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/foundation/support.h"

void qa_compat_q2_validate_native_mod_pickups(const qa_native_mod_declaration *declaration);
typedef struct qa_compat_q2_native_mod_pickups { void *host_ctx; bool active; int depth; bool closed; } qa_compat_q2_native_mod_pickups;
void qa_compat_q2_native_mod_pickups_init(qa_compat_q2_native_mod_pickups *s, void *ctx);
void qa_compat_q2_native_mod_pickups_close(qa_compat_q2_native_mod_pickups *s);
#endif
