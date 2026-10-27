/* Port of src/compat/q2/native-mod-actors.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_ACTORS_H
#define QA_COMPAT_Q2_NATIVE_MOD_ACTORS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/foundation/support.h"

typedef struct qa_compat_q2_saved_native_actor { const char *actor; double slot; bool linked; } qa_compat_q2_saved_native_actor;
typedef struct qa_compat_q2_saved_native_actors { double next_frame, frame; const qa_compat_q2_saved_native_actor *actors; size_t actor_count; } qa_compat_q2_saved_native_actors;
typedef struct qa_compat_q2_native_mod_actors { void *host_ctx; const qa_native_mod_declaration *declaration; bool closed; bool active; } qa_compat_q2_native_mod_actors;
void qa_compat_q2_native_mod_actors_init(qa_compat_q2_native_mod_actors *s, void *host_ctx, const qa_native_mod_declaration *d);
void qa_compat_q2_native_mod_actors_close(qa_compat_q2_native_mod_actors *s);
#endif
