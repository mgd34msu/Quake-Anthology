#ifndef QA_APPLICATION_NATIVE_Q2_COMBAT_POLICY_H
#define QA_APPLICATION_NATIVE_Q2_COMBAT_POLICY_H

#include "internal.h"
bool application_native_q2_combat_before_reaction(qa_application *, const qa_damage_outcome *,
                                                qa_error *);
bool application_native_q2_armor_context(void *, const qa_damage_request *,
    const qa_combat_state *, const qa_damage_geometry *, qa_armor_context *, qa_error *);

bool application_native_q2_combat_policy(application_provider *, qa_combat_policy *, qa_error *);
bool application_native_q2_damage_prepare(void *, qa_damage_request *, bool *, qa_error *);
qa_actor_owner application_native_q2_attack_inventory(void *, qa_actor_id, qa_item_id);

#endif
