/* Port of src/app/bootstrap/simulation/network-q1-quakec.ts: native NetQuake serializes
 * the executed program's fields and ordered precaches. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q1_QUAKEC_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q1_QUAKEC_H
#include "qa/app/bootstrap/simulation/network_q1.h"
#include "qa/app/bootstrap/simulation/quakec_source.h"

/* TS createQuakeCNetQuakeHost(options): options.quakec is simulation.quakecSource().
 * Free the host with qa_app_sim_q1_application_server_host_destroy. */
qa_app_q1_server_host qa_app_sim_create_quakec_netquake_host(qa_app_sim_q1_server_binding_options options);
#endif
