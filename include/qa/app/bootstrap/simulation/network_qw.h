/* Port of src/app/bootstrap/simulation/network-qw.ts (createQwApplicationServerHost).
 * Messages are qa_quakeworld_message values; every array a host member returns is
 * owned by the host until that member is called again. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_NETWORK_QW_H
#define QA_APP_BOOTSTRAP_SIMULATION_NETWORK_QW_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/network.h"
#include "qa/app/bootstrap/simulation/quakec_source.h"
#include "qa/app/bootstrap/network/qw_server.h"
#include "qa/app/bootstrap/network/qw_server_types.h"

/* TS QwApplicationServerBindingOptions. */
typedef struct qa_app_sim_qw_server_binding_options {
  qa_engine_session *session;
  const qa_app_sim_network_simulation *simulation;
  const qa_app_sim_network_content *content;
  /* simulation.quakecSource() (NULL is TS null). */
  qa_app_sim_quakec_source *quakec;
  /* simulation.options.travel?.source: the carried QuakeWorld client's userInfo, false
   * when the carry is not QuakeWorld or names no such client. */
  void *travel_ctx;
  bool (*travel_userinfo)(void *ctx, qa_client_id client, qa_app_sim_quakec_info *out);
  double server_count;
  const void *administration;
  const void *const *masters; size_t master_count;
  void *print_ctx; void (*print)(void *ctx, const char *text);
} qa_app_sim_qw_server_binding_options;

/* host.ctx is owned; free with _destroy. TS authentication is a getter object: the
 * host refreshes the pointed-to values in observe() and admit(). `paused` reads
 * simulation.q1Paused live, as the TS getter. */
qa_app_qw_server_host qa_app_sim_create_qw_application_server_host(qa_app_sim_qw_server_binding_options options);
void qa_app_sim_qw_application_server_host_destroy(qa_app_qw_server_host host);
#endif
