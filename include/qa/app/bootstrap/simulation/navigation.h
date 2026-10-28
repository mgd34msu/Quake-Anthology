/* Port of src/app/bootstrap/simulation/navigation.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NAVIGATION_H
#define QA_APP_BOOTSTRAP_SIMULATION_NAVIGATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_application_bot_navigation qa_app_sim_application_bot_navigation;
typedef struct qa_app_sim_application_bot_navigation_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_application_bot_navigation_options;
qa_app_sim_application_bot_navigation *qa_app_sim_application_bot_navigation_create(qa_app_sim_application_bot_navigation_options options);
void qa_app_sim_application_bot_navigation_destroy(qa_app_sim_application_bot_navigation *value);
#endif
