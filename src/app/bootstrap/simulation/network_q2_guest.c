/* Port of src/app/bootstrap/simulation/network-q2-guest.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network_q2_guest.h"
#include <stdio.h>
#include <stdlib.h>
#include "qa/foundation/support.h"
qa_app_q2_server_host qa_app_sim_create_classic_q2_guest_application_server_host(qa_app_sim_classic_q2_guest_binding_options options) {
  if (!options.guest.has_guest) qa_range_error("Classic Q2 guest server requires the ClassicGuestSource");
  qa_cvar_registry *cvars = options.guest.cvars;
  if (cvars == NULL) qa_range_error("Classic Q2 guest server requires the source cvar registry");
  qa_cvar_registry_register(cvars, "hostname", "noname", QA_Q2_CVAR_FLAG_SERVER_INFO | QA_Q2_CVAR_FLAG_ARCHIVE, &(qa_cvar_snapshot){0});
  char protocol[16], max_clients[16];
  snprintf(protocol, sizeof protocol, "%d", (int)options.base.protocol.version);
  snprintf(max_clients, sizeof max_clients, "%d", (int)options.guest.max_clients);
  const char *names[3] = {"protocol", "mapname", "maxclients"};
  const char *values[3] = {protocol, options.guest.map_name, max_clients};
  for (int i = 0; i < 3; i++) {
    qa_cvar_registry_register(cvars, names[i], values[i],
        QA_Q2_CVAR_FLAG_SERVER_INFO | (i == 2 ? QA_Q2_CVAR_FLAG_LATCH : QA_Q2_CVAR_FLAG_NO_SET), &(qa_cvar_snapshot){0});
    qa_cvar_registry_set(cvars, names[i], values[i], true, NULL);
  }
  qa_app_q2_server_host host = options.guest.host;
  host.protocol = options.base.protocol;
  host.max_clients = options.guest.max_clients;
  return host;
}
void qa_app_sim_classic_q2_guest_application_server_host_destroy(qa_app_q2_server_host host) { (void)host; }
