/* Port of src/app/bootstrap/simulation/network-q3.ts (createQ3ApplicationServerHost).
 * The SharedSimulation and LoadedApplicationContent members the TS reads come through
 * the qa_app_sim_network_simulation / qa_app_sim_network_content access structs
 * (network.h); the Q3 game provider is simulation.q3_guest() (Q3QvmServerGame) or
 * simulation.q3_source() (Q3SourceRuntime). Every pointer a host member returns is
 * owned by the host and valid until that member is called again (the input commands
 * until the host is destroyed). TS throws raise ("Error", or the source's own error);
 * async TS members are synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q3_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q3_H
#include <stdbool.h>
#include "qa/app/bootstrap/network/q3_types.h"
#include "qa/app/bootstrap/simulation/network.h"

/* TS Q3ApplicationServerBindingOptions. */
typedef struct qa_app_sim_q3_server_binding_options {
  qa_engine_session *session;
  const qa_app_sim_network_simulation *simulation;
  const qa_app_sim_network_content *content;
  const char *mode;            /* "new" | "restore"; NULL is TS undefined ("new") */
  const void *administration;  /* NULL is TS undefined */
  void *print_ctx; void (*print)(void *ctx, const char *text);
} qa_app_sim_q3_server_binding_options;

/* TS Q3ApplicationServerAuthority = Q3ApplicationServerHost + connect. connect's ctx is
 * host.ctx. A source admission denial (Q3ClientAdmissionDenied) is a rejected
 * admission; other failures raise. */
typedef struct qa_app_sim_q3_server_authority {
  qa_app_q3_server_host host;
  qa_app_q3_admission (*connect)(void *ctx, qa_client_id client, const char *userinfo);
} qa_app_sim_q3_server_authority;

qa_app_sim_q3_server_authority qa_app_sim_create_q3_application_server_host(qa_app_sim_q3_server_binding_options options);
/* Frees host.ctx and everything the host members returned. */
void qa_app_sim_q3_application_server_authority_destroy(qa_app_sim_q3_server_authority authority);
#endif
