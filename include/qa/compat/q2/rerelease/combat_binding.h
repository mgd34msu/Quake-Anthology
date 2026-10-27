/* Port of src/compat/q2/rerelease/combat-binding.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_RERELEASE_COMBAT_BINDING_H
#define QA_COMPAT_Q2_RERELEASE_COMBAT_BINDING_H
#include <stdbool.h>
#include "qa/compat/q2/rerelease/host.h"

typedef struct qa_compat_q2_rerelease_combat_bindings qa_compat_q2_rerelease_combat_bindings;
/* NULL only on OOM. */
qa_compat_q2_rerelease_combat_bindings *qa_compat_q2_rerelease_combat_bindings_create(qa_compat_q2_rerelease_q2_guest_host *host);
void qa_compat_q2_rerelease_combat_bindings_destroy(qa_compat_q2_rerelease_combat_bindings *b);
qa_compat_q2_rerelease_q2_guest_host *qa_compat_q2_rerelease_combat_bindings_get_host(const qa_compat_q2_rerelease_combat_bindings *b);
/* False return is the TS null (no world profile). */
bool qa_compat_q2_rerelease_combat_bindings_notarget(qa_compat_q2_rerelease_combat_bindings *b, qa_raw_entity_view view, bool *out);
/* False return is the TS null. The binding's ctx is owned by b and
 * stays valid until b is destroyed. */
bool qa_compat_q2_rerelease_combat_bindings_bind(qa_compat_q2_rerelease_combat_bindings *b, qa_raw_entity_view view, qa_combat_state_binding *out);
#endif
