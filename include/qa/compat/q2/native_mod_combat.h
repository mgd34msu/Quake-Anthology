/* Port of src/compat/q2/native-mod-combat.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_COMBAT_H
#define QA_COMPAT_Q2_NATIVE_MOD_COMBAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/foundation/support.h"

typedef struct qa_compat_q2_native_mod_combat_calls { void *ctx; bool (*eligible)(void *ctx, qa_actor_id a); } qa_compat_q2_native_mod_combat_calls;
typedef struct qa_compat_q2_native_mod_combat_state { void *host_ctx; const qa_native_mod_declaration *declaration; bool closed; bool active; } qa_compat_q2_native_mod_combat_state;
void qa_compat_q2_native_mod_combat_init(qa_compat_q2_native_mod_combat_state *s, void *host_ctx, const qa_native_mod_declaration *d);
void qa_compat_q2_native_mod_combat_close(qa_compat_q2_native_mod_combat_state *s);
#endif
