/* Port of src/app/bootstrap/simulation/network-q3.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q3_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_Q3_H
#include <stdbool.h>
#include "qa/app/bootstrap/network/q3_types.h"
#include "qa/contracts/protocol.h"
#include "qa/core/cvars/index.h"
typedef struct qa_app_sim_q3_network_source {
  void *ctx;
  bool present; bool guest;
  const char *product; double max_clients;
  bool demo_restricted;
  qa_cvar_registry *cvars;
  qa_app_q3_server_host host; /* frame projection */
  qa_app_q3_admission (*connect)(void *ctx, qa_client_id client, const char *userinfo, bool *denied, const char **reason);
  void (*cleanup_client)(void *ctx, qa_client_id client);
} qa_app_sim_q3_network_source;
typedef struct qa_app_sim_q3_server_binding_options {
  const void *session;
  qa_app_sim_q3_network_source simulation;
  const void *content;
  const char *mode; /* "new"|"restore"; NULL = "new" */
  const void *administration;
  void (*print)(void *ctx, const char *text); void *print_ctx;
} qa_app_sim_q3_server_binding_options;
/* TS Q3ApplicationServerAuthority = host + connect. */
typedef struct qa_app_sim_q3_server_authority {
  qa_app_q3_server_host host;
  void *connect_ctx;
  qa_app_q3_admission (*connect)(void *ctx, qa_client_id client, const char *userinfo);
} qa_app_sim_q3_server_authority;
qa_app_sim_q3_server_authority qa_app_sim_create_q3_application_server_host(qa_app_sim_q3_server_binding_options options);
void qa_app_sim_q3_application_server_authority_destroy(qa_app_sim_q3_server_authority authority);
typedef struct qa_app_sim_unified_q3_binding_options { qa_app_sim_q3_server_binding_options base; } qa_app_sim_unified_q3_binding_options;
qa_app_sim_q3_server_authority qa_app_sim_create_unified_q3_application_server_host(qa_app_sim_unified_q3_binding_options options);
#endif
