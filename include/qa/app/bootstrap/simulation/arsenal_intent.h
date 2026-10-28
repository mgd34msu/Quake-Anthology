/* Port of src/app/bootstrap/simulation/arsenal-intent.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_INTENT_H
#define QA_APP_BOOTSTRAP_SIMULATION_ARSENAL_INTENT_H
#include <stdbool.h>
#include "qa/content/q3/foundation/arsenal.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/movement.h"
qa_q3_arsenal_controls qa_app_sim_resolve_q3_arsenal_controls(qa_arsenal_state arsenal,
    const qa_arsenal_intent *intent, qa_movement_command command, const char *product);
#endif
