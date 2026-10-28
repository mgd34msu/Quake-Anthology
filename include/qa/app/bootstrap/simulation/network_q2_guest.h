/* Port of src/app/bootstrap/simulation/network-q2-guest.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q2_GUEST_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q2_GUEST_H
#include "qa/app/bootstrap/network/types.h"
#include "qa/app/bootstrap/simulation/network.h"
#include "qa/core/cvars/index.h"
typedef struct qa_app_sim_classic_q2_guest_network_view {
  void *ctx;
  bool has_guest;
  qa_cvar_registry *cvars;
  const char *map_name; double max_clients;
  qa_app_q2_server_host host; /* frame projection */
} qa_app_sim_classic_q2_guest_network_view;
typedef struct qa_app_sim_classic_q2_guest_binding_options {
  qa_app_sim_q2_server_binding_options base;
  qa_app_sim_classic_q2_guest_network_view guest;
} qa_app_sim_classic_q2_guest_binding_options;
qa_app_q2_server_host qa_app_sim_create_classic_q2_guest_application_server_host(qa_app_sim_classic_q2_guest_binding_options options);
void qa_app_sim_classic_q2_guest_application_server_host_destroy(qa_app_q2_server_host host);
#endif
