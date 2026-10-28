/* Port of src/app/bootstrap/simulation/source-transition.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_SOURCE_TRANSITION_H
#define QA_APP_BOOTSTRAP_SIMULATION_SOURCE_TRANSITION_H
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
typedef const char *qa_app_sim_source_level_authority; /* "primary-world"|"actor-source" */
qa_transition_intent qa_app_sim_source_level_transition(qa_transition_intent intent, qa_executable_recipe recipe, qa_app_sim_source_level_authority authority);
#endif
