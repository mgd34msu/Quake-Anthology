/* Port of src/app/bootstrap/network/qw-client.ts (QwClientNetwork).
 * Recording sink appends demo records synchronously (TS promise
 * chain collapses; failures abort). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_QW_CLIENT_H
#define QA_APP_BOOTSTRAP_NETWORK_QW_CLIENT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/qw_types.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/network/common/transport.h"
#include "qa/network/q1/demos.h"
typedef struct qa_app_qw_client_network_options {
  qa_datagram_transport *transport;
  qa_net_address remote;
  qa_app_qw_client_host host;
  double qport;
  void *userinfo_ctx;
  const char *(*userinfo)(void *ctx);
  bool has_timeout_milliseconds; double timeout_milliseconds;
} qa_app_qw_client_network_options;
typedef struct qa_app_qw_recording_sink {
  void *ctx;
  void (*append)(void *ctx, const qa_quakeworld_demo_record *record);
} qa_app_qw_recording_sink;
typedef struct qa_app_qw_client_network qa_app_qw_client_network;
qa_app_qw_client_network *qa_app_qw_client_network_create(qa_app_qw_client_network_options options);
void qa_app_qw_client_network_destroy(qa_app_qw_client_network *network);
qa_app_network_phase qa_app_qw_client_network_phase(const qa_app_qw_client_network *network);
void qa_app_qw_client_network_command(qa_app_qw_client_network *network, const char *text);
void qa_app_qw_client_network_refresh_skins(qa_app_qw_client_network *network);
void qa_app_qw_client_network_resume_downloads(qa_app_qw_client_network *network);
const void *const *qa_app_qw_client_network_poll(qa_app_qw_client_network *network,
    double now, size_t *out_count);
void qa_app_qw_client_network_submit(qa_app_qw_client_network *network,
    const void *const *commands, size_t count, double now);
void qa_app_qw_client_network_publish(qa_app_qw_client_network *network,
    const void *output, const void *const *events, size_t count, double now);
void qa_app_qw_client_network_close(qa_app_qw_client_network *network);
/* Seed records are malloc'd; destroy each + free the array. */
qa_quakeworld_demo_record *qa_app_qw_client_network_recording_seed(
    qa_app_qw_client_network *network, size_t *out_count);
void *qa_app_qw_client_network_recording_attach(qa_app_qw_client_network *network,
    qa_app_qw_recording_sink sink);
void qa_app_qw_client_network_recording_detach(qa_app_qw_client_network *network, void *token);
qa_app_application_network qa_app_qw_client_network_as_network(qa_app_qw_client_network *network);
#endif
