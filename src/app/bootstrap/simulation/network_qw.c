/* Port of src/app/bootstrap/simulation/network-qw.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/network_qw.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
typedef struct host_ctx { qa_app_sim_qw_server_binding_options options; } host_ctx;
static void print_text(void *ctx, const char *text) {
  host_ctx *h = ctx;
  if (h->options.print) h->options.print(h->options.print_ctx, text);
}
qa_app_qw_server_host qa_app_sim_create_qw_application_server_host(qa_app_sim_qw_server_binding_options options) {
  qa_app_sim_qw_network_source *game = &options.simulation;
  if (!game->present || !is(game->kind, "quakeworld") || !is(options.world_kind, "q1-bsp"))
    qa_range_error("Native QW wire requires its admitted QuakeWorld source and 32 reserved players");
  if (game->model_count >= 256 || game->sound_count >= 256)
    qa_range_error("Native QW precaches exceed 8-bit indices");
  static const char *const downloads[] = {"allow_download", "allow_download_skins", "allow_download_models", "allow_download_sounds", "allow_download_maps"};
  if (game->cvars) for (size_t i = 0; i < 5; i++) {
    qa_cvar_read found;
    if (!qa_cvar_registry_find(game->cvars, downloads[i], &found))
      qa_cvar_registry_register(game->cvars, downloads[i], "1", 0, &(qa_cvar_snapshot){0});
  }
  host_ctx *h = calloc(1, sizeof *h);
  if (!h) qa_range_error("Out of memory");
  h->options = options;
  qa_app_qw_server_host host = options.simulation.host;
  host.ctx = h;
  host.print = print_text;
  if (options.administration) host.administration = options.administration;
  return host;
}
void qa_app_sim_qw_application_server_host_destroy(qa_app_qw_server_host host) { free(host.ctx); }
