/* Port of src/compat/q2/native-mod-clients.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_MOD_CLIENTS_H
#define QA_COMPAT_Q2_NATIVE_MOD_CLIENTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/native_mod_callbacks.h"
#include "qa/foundation/support.h"

typedef struct qa_compat_q2_native_mod_client_slot { qa_actor_id actor; double slot; bool admitted; } qa_compat_q2_native_mod_client_slot;
typedef struct qa_compat_q2_native_mod_clients_binding { void *host_ctx; const qa_native_mod_declaration *declaration; bool closed; bool active; } qa_compat_q2_native_mod_clients_binding;
void qa_compat_q2_native_mod_clients_init(qa_compat_q2_native_mod_clients_binding *s, void *host_ctx, const qa_native_mod_declaration *d);
void qa_compat_q2_native_mod_clients_close(qa_compat_q2_native_mod_clients_binding *s);
#endif
