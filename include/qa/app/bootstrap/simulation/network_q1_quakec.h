/* Port of src/app/bootstrap/simulation/network-q1-quakec.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q1_QUAKEC_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q1_QUAKEC_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/types.h"
typedef struct qa_app_sim_network_q1_quakec qa_app_sim_network_q1_quakec;
typedef struct qa_app_sim_network_q1_quakec_options { void *ctx; const void *simulation; const void *host; } qa_app_sim_network_q1_quakec_options;
qa_app_sim_network_q1_quakec *qa_app_sim_network_q1_quakec_create(qa_app_sim_network_q1_quakec_options options);
void qa_app_sim_network_q1_quakec_destroy(qa_app_sim_network_q1_quakec *value);
#endif
