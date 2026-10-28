/* Port of src/app/bootstrap/simulation/network.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q2_layout.h"
#include "qa/foundation/support.h"
typedef struct host_ctx { qa_app_sim_q2_server_binding_options options; } host_ctx;
static bool rejects(void *ctx, const void *address) {
  host_ctx *h = ctx;
  return h->options.rejects ? h->options.rejects(h->options.rejects_ctx, address) : false;
}
static void print_text(void *ctx, const char *text) {
  host_ctx *h = ctx;
  if (h->options.print) h->options.print(h->options.print_ctx, text);
}
qa_app_q2_server_host qa_app_sim_create_q2_application_server_host(qa_app_sim_q2_server_binding_options options) {
  qa_app_sim_q2_network_simulation *simulation = &options.simulation;
  if (!simulation->has_q2_source) qa_range_error("Q2 server network host requires the Q2 source game provider");
  qa_cvar_registry *cvars = simulation->cvars;
  if (cvars == NULL) qa_range_error("Q2 server network host requires the source cvar registry");
  qa_cvar_registry_register(cvars, "sv_mvd_enable", "0", QA_Q2_CVAR_FLAG_LATCH, &(qa_cvar_snapshot){0});
  qa_cvar_registry_register(cvars, "sv_mvd_maxclients", "8", QA_Q2_CVAR_FLAG_LATCH, &(qa_cvar_snapshot){0});
  qa_cvar_registry_register(cvars, "sv_mvd_password", "", QA_Q2_CVAR_FLAG_PRIVATE, &(qa_cvar_snapshot){0});
  qa_cvar_registry_register(cvars, "hostname", "noname", QA_Q2_CVAR_FLAG_SERVER_INFO | QA_Q2_CVAR_FLAG_ARCHIVE, &(qa_cvar_snapshot){0});
  char protocol[16], max_clients[16];
  snprintf(protocol, sizeof protocol, "%d", (int)options.protocol.version);
  snprintf(max_clients, sizeof max_clients, "%d", (int)simulation->max_clients);
  const char *names[3] = {"protocol", "mapname", "maxclients"};
  const char *values[3] = {protocol, simulation->map_name, max_clients};
  for (int i = 0; i < 3; i++) {
    qa_cvar_registry_register(cvars, names[i], values[i],
        QA_Q2_CVAR_FLAG_SERVER_INFO | (i == 2 ? QA_Q2_CVAR_FLAG_LATCH : QA_Q2_CVAR_FLAG_NO_SET), &(qa_cvar_snapshot){0});
    qa_cvar_registry_set(cvars, names[i], values[i], true, NULL);
  }
  host_ctx *h = calloc(1, sizeof *h);
  if (!h) qa_range_error("Out of memory");
  h->options = options;
  qa_app_q2_server_host host = options.simulation.host;
  host.ctx = h;
  host.protocol = options.protocol;
  host.max_clients = simulation->max_clients;
  host.rejects = rejects;
  host.print = print_text;
  if (options.administration) host.administration = options.administration;
  (void)qa_app_q2_application_layout(options.protocol);
  return host;
}
void qa_app_sim_q2_application_server_host_destroy(qa_app_q2_server_host host) { free(host.ctx); }
