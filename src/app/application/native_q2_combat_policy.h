#ifndef QA_APPLICATION_NATIVE_Q2_COMBAT_POLICY_H
#define QA_APPLICATION_NATIVE_Q2_COMBAT_POLICY_H

#include "internal.h"

bool application_native_q2_combat_policy(application_provider *, qa_combat_policy *, qa_error *);
bool application_native_q2_damage_prepare(void *, qa_damage_request *, bool *, qa_error *);
qa_actor_owner application_native_q2_attack_inventory(void *, qa_actor_id, qa_item_id);

#endif
