/* Port of src/app/bootstrap/simulation/q3/command-policy.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_COMMAND_POLICY_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_COMMAND_POLICY_H
#include <stdbool.h>
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/contracts/session.h"
/* Converted arsenal intent storage for the frozen envelope rewrite. */
qa_actor_command qa_app_apply_q3_command_policy(qa_actor_command pending, qa_q3_base_user_command before,
    qa_q3_base_user_command after, qa_user_command converted, bool frozen,
    qa_arsenal_intent *arsenal_storage);
#endif
