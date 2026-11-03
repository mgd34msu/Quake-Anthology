/* Port of src/app/bootstrap/simulation/network-q1.ts (createQ1ApplicationServerHost).
 * Messages are qa_netquake_message values (the Q1ApplicationMessage the NetQuake
 * server encodes); every array a host member returns is owned by the host until that
 * member is called again for the same player (or for the host, when not per player).
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q1_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q1_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/network.h"
#include "qa/app/bootstrap/network/q1.h"
#include "qa/app/bootstrap/network/q1_types.h"

/* TS Q1ApplicationServerBindingOptions. */
typedef struct qa_app_sim_q1_server_binding_options {
  qa_engine_session *session;
  void *rejects_ctx; bool (*rejects)(void *ctx, const qa_net_address *address); /* NULL = TS undefined */
  const qa_app_sim_network_simulation *simulation;
  const qa_app_sim_network_content *content;
  qa_q1_protocol_identity protocol;
  /* simulation.quakecSource(): NULL is TS null (the NetQuake QuakeC host is chosen first). */
  struct qa_app_sim_quakec_source *quakec;
  void *print_ctx; void (*print)(void *ctx, const char *text);
} qa_app_sim_q1_server_binding_options;

/* TS createQ1ApplicationServerHost. host.ctx is owned; free with
 * qa_app_sim_q1_application_server_host_destroy (both Q1 hosts). */
qa_app_q1_server_host qa_app_sim_create_q1_application_server_host(qa_app_sim_q1_server_binding_options options);
void qa_app_sim_q1_application_server_host_destroy(qa_app_q1_server_host host);
#endif
