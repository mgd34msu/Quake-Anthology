/* Port of src/compat/q2/native-mod-weapon-stage.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_WEAPON_STAGE_H
#define QA_COMPAT_Q2_NATIVE_MOD_WEAPON_STAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/foundation/support.h"

typedef struct qa_compat_q2_native_weapon_operations { void *ctx; bool (*selected)(void *ctx, qa_actor_id a); } qa_compat_q2_native_weapon_operations;
typedef struct qa_compat_q2_native_weapon_dispatcher { void *host_ctx; bool closed; bool has_current; qa_actor_id current; } qa_compat_q2_native_weapon_dispatcher;
typedef struct qa_compat_q2_native_mod_weapon_stage { void *host_ctx; const qa_native_mod_declaration *declaration; bool closed; bool active; } qa_compat_q2_native_mod_weapon_stage;
void qa_compat_q2_native_mod_weapon_stage_init(qa_compat_q2_native_mod_weapon_stage *s, void *host_ctx, const qa_native_mod_declaration *d);
void qa_compat_q2_native_mod_weapon_stage_close(qa_compat_q2_native_mod_weapon_stage *s);
void qa_compat_q2_native_weapon_dispatcher_close(qa_compat_q2_native_weapon_dispatcher *s);
#endif
