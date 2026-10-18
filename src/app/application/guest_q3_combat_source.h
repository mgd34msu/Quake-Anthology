#ifndef QA_APPLICATION_GUEST_Q3_COMBAT_SOURCE_H
#define QA_APPLICATION_GUEST_Q3_COMBAT_SOURCE_H

#include "guest_q3_combat_state.h"

typedef bool (*application_q3_damage_values_fn)(void *, const qa_native_value *, size_t, qa_error *);
uint32_t application_q3_combat_canonical_flags(const application_q3_combat_definition *, uint32_t);
uint32_t application_q3_combat_source_flags(const application_q3_combat_definition *, const qa_damage_request *, uint32_t);
bool application_q3_combat_source_call(struct q3g_role *, const qa_qvm_call *parent,
    uint32_t entry, const qa_native_value *, size_t, qa_native_value *, qa_error *);
/* Argument lowering emits Q3_DAMAGE_* semantic order; damage lowering maps that
 * record to the actual declared damageCall, including its typed extras. */
bool application_q3_combat_source_arguments(const application_q3_combat_actor *,
    const qa_qvm_call *parent, const qa_damage_request *, uint32_t original_flags,
    application_q3_damage_values_fn, void *, qa_error *);
bool application_q3_combat_source_damage(const application_q3_combat_actor *,
    const qa_qvm_call *parent, const qa_damage_request *, uint32_t original_flags,
    application_q3_damage_values_fn, void *, qa_error *);

#endif
