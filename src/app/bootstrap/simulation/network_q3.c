/* Port of src/app/bootstrap/simulation/network-q3.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network_q3.h"
#include <stdlib.h>
#include <string.h>
#include "qa/core/cvars/index.h"
#include "qa/foundation/support.h"
typedef struct authority_ctx { qa_app_sim_q3_server_binding_options options; } authority_ctx;
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static qa_app_q3_admission connect_fn(void *ctx, qa_client_id client, const char *userinfo) {
  authority_ctx *a = ctx;
  qa_app_sim_q3_network_source *source = &a->options.simulation;
  if (source->guest) return source->connect(source->ctx, client, userinfo, NULL, NULL);
  bool denied = false; const char *reason = NULL;
  qa_app_q3_admission result = source->connect(source->ctx, client, userinfo, &denied, &reason);
  if (denied) {
    if (source->cleanup_client) source->cleanup_client(source->ctx, client);
    qa_app_q3_admission rejected; memset(&rejected, 0, sizeof rejected);
    rejected.accepted = false; rejected.reason = reason;
    return rejected;
  }
  return result;
}
static void print_text(void *ctx, const char *text) {
  authority_ctx *a = ctx;
  if (a->options.print) a->options.print(a->options.print_ctx, text);
}
qa_app_sim_q3_server_authority qa_app_sim_create_q3_application_server_host(qa_app_sim_q3_server_binding_options options) {
  qa_app_sim_q3_network_source *source = &options.simulation;
  if (!source->present) qa_range_error("Q3 network requires a Q3 game provider");
  const char *mode = options.mode ? options.mode : "new";
  if (is(mode, "restore") && !source->guest)
    qa_range_error("Restored Q3 client authority requires a saved QVM source");
  if (is(mode, "new") && source->cvars) {
    qa_cvar_read current;
    const char *value = source->guest
        ? (qa_cvar_registry_find(source->cvars, "fs_game", &current) ? current.value : "")
        : is(source->product, "missionpack") ? "missionpack" : "";
    qa_cvar_registry_register(source->cvars, "fs_game", value, QA_CVAR_FLAG_SYSTEM_INFO, &(qa_cvar_snapshot){0});
  }
  authority_ctx *a = calloc(1, sizeof *a);
  if (!a) qa_range_error("Out of memory");
  a->options = options;
  qa_app_sim_q3_server_authority out;
  out.host = options.simulation.host;
  out.host.ctx = a;
  out.host.product = source->product;
  out.host.max_clients = source->max_clients;
  out.host.print = print_text;
  if (options.administration) out.host.administration = options.administration;
  out.connect_ctx = a;
  out.connect = connect_fn;
  return out;
}
void qa_app_sim_q3_application_server_authority_destroy(qa_app_sim_q3_server_authority authority) { free(authority.connect_ctx); }
qa_app_sim_q3_server_authority qa_app_sim_create_unified_q3_application_server_host(qa_app_sim_unified_q3_binding_options options) {
  return qa_app_sim_create_q3_application_server_host(options.base);
}
