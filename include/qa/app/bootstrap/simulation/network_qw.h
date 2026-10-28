/* Port of src/app/bootstrap/simulation/network-qw.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_QW_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_QW_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/qw_server_types.h"
#include "qa/core/cvars/index.h"
typedef struct qa_app_sim_qw_network_source {
  bool present; const char *kind; /* "quakeworld" */
  size_t model_count, sound_count;
  qa_cvar_registry *cvars;
  qa_app_qw_server_host host; /* frame projection */
} qa_app_sim_qw_network_source;
typedef struct qa_app_sim_qw_server_binding_options {
  const void *session;
  qa_app_sim_qw_network_source simulation;
  const char *world_kind; /* "q1-bsp" */
  const void *content;
  double server_count;
  const void *administration, *masters;
  void (*print)(void *ctx, const char *text); void *print_ctx;
} qa_app_sim_qw_server_binding_options;
/* Synchronous form of the TS async create. */
qa_app_qw_server_host qa_app_sim_create_qw_application_server_host(qa_app_sim_qw_server_binding_options options);
void qa_app_sim_qw_application_server_host_destroy(qa_app_qw_server_host host);
#endif
