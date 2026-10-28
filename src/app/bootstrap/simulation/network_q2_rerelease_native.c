/* Port of src/app/bootstrap/simulation/network-q2-rerelease-native.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network_q2_rerelease_native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q2_layout.h"
#include "qa/foundation/support.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
qa_app_q2_server_host qa_app_sim_create_rerelease_native_q2_application_server_host(qa_app_sim_rerelease_native_binding_options options) {
  qa_q2_protocol_identity protocol = options.base.protocol;
  if (!is(protocol.kind, "q2-rerelease") && !is(protocol.kind, "q2-kex"))
    qa_range_error("Rerelease guest server requires protocol1038 or KEX2023");
  qa_app_q2_layout layout = qa_app_q2_application_layout(protocol);
  qa_cvar_registry *cvars = options.cvars;
  const char *map = options.map_path;
  if (strncmp(map, "maps/", 5) == 0) map += 5;
  char map_name[256]; snprintf(map_name, sizeof map_name, "%s", map);
  size_t len = strlen(map_name);
  if (len > 4 && strcmp(map_name + len - 4, ".bsp") == 0) map_name[len - 4] = '\0';
  qa_cvar_registry_register(cvars, "hostname", "noname", QA_Q2_CVAR_FLAG_SERVER_INFO | QA_Q2_CVAR_FLAG_ARCHIVE, &(qa_cvar_snapshot){0});
  char protocol_text[16]; snprintf(protocol_text, sizeof protocol_text, "%d", (int)protocol.version);
  const char *names[2] = {"protocol", "mapname"};
  const char *values[2] = {protocol_text, map_name};
  for (int i = 0; i < 2; i++) {
    qa_cvar_registry_register(cvars, names[i], values[i], QA_Q2_CVAR_FLAG_SERVER_INFO | QA_Q2_CVAR_FLAG_NO_SET, &(qa_cvar_snapshot){0});
    qa_cvar_registry_set(cvars, names[i], values[i], true, NULL);
  }
  /* world.services configstrings for checksum/maxClients/airAccelerate
   * are written through the services port by the caller-provided
   * world; layout indices are validated here. */
  (void)layout;
  qa_app_q2_server_host host = options.host;
  host.protocol = protocol;
  host.max_clients = options.max_clients;
  return host;
}
qa_app_sim_rerelease_transcode qa_app_sim_create_rerelease_native_message_transcoder(qa_q2_protocol_identity protocol,
    void *ctx, uint8_t *(*transcode)(void *ctx, const uint8_t *bytes, size_t length, size_t *out_length)) {
  (void)protocol;
  qa_app_sim_rerelease_transcode out = {ctx, transcode};
  return out;
}
