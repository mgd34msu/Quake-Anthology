/* Port of src/app/bootstrap/simulation/network-q1.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q1_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q1_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_network_q1_binding qa_app_sim_network_q1_binding;
typedef struct qa_app_sim_network_q1_binding_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_network_q1_binding_options;
qa_app_sim_network_q1_binding *qa_app_sim_network_q1_binding_create(qa_app_sim_network_q1_binding_options options);
void qa_app_sim_network_q1_binding_destroy(qa_app_sim_network_q1_binding *value);
#endif
