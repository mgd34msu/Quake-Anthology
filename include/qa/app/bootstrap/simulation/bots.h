/* Port of src/app/bootstrap/simulation/bots.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOTS_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOTS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_application_bots qa_app_sim_application_bots;
typedef struct qa_app_sim_application_bots_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_application_bots_options;
qa_app_sim_application_bots *qa_app_sim_application_bots_create(qa_app_sim_application_bots_options options);
void qa_app_sim_application_bots_destroy(qa_app_sim_application_bots *value);
#endif
