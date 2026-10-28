/* Port of src/app/bootstrap/simulation/classic-guest-services.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_SERVICES_H
#define QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_SERVICES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_classic_guest_services qa_app_sim_classic_guest_services;
typedef struct qa_app_sim_classic_guest_services_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_classic_guest_services_options;
qa_app_sim_classic_guest_services *qa_app_sim_classic_guest_services_create(qa_app_sim_classic_guest_services_options options);
void qa_app_sim_classic_guest_services_destroy(qa_app_sim_classic_guest_services *value);
#endif
