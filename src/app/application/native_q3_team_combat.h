#ifndef QA_APPLICATION_NATIVE_Q3_TEAM_COMBAT_H
#define QA_APPLICATION_NATIVE_Q3_TEAM_COMBAT_H

#include "internal.h"

bool application_native_q3_team_check_hurt_carrier(void *, qa_actor_id target,
    qa_actor_id attacker, qa_error *);
bool application_native_q3_team_frag_bonuses(void *, qa_actor_id target,
    qa_actor_id attacker_or_none, qa_error *);
bool application_native_q3_source_death_score(void *, qa_actor_id target,
    qa_actor_id attacker_or_none, qa_error *);

#endif
