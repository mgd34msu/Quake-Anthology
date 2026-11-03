/* Port of src/app/bootstrap/network/q2.ts (Q2ServerNetwork +
 * Q2ClientNetwork). Transports are qa_datagram_transport*.
 * Recording sinks mirror the sibling demo-recording append per
 * packet kind. Host gameState configStrings/baselines use the
 * landed entry arrays (q2_client_receiver.h snapshot +
 * qa_q2_baseline_list). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q2_H
#define QA_APP_BOOTSTRAP_NETWORK_Q2_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/network/q2_client_receiver.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/network/q2/connectionless.h"
typedef struct qa_app_q2_server_network qa_app_q2_server_network;
/* TS Q2ApplicationServerHost.discovery: the value qa_app_q2_server_host.discovery points at. */
typedef struct qa_app_q2_discovery {
  void *ctx;
  qa_q2_status (*status)(void *ctx);
  qa_q2_host_info (*info)(void *ctx);
} qa_app_q2_discovery;
typedef struct qa_app_q2_recording_sink {
  void *ctx;
  void (*append)(void *ctx, const char *kind, const uint8_t *message, size_t length);
} qa_app_q2_recording_sink;
typedef struct qa_app_q2_server_recording_seed {
  const char *kind; double protocol; /* q2-server */
  uint8_t *message; size_t length;
} qa_app_q2_server_recording_seed;
typedef struct qa_app_q2_mvd_recording_seed {
  double revision;
  uint8_t **packets; size_t *lengths; size_t packet_count;
} qa_app_q2_mvd_recording_seed;
qa_app_q2_server_network *qa_app_q2_server_network_create(qa_app_q2_server_network_options options);
void qa_app_q2_server_network_destroy(qa_app_q2_server_network *network);
const qa_app_q2_server_network_options *qa_app_q2_server_network_options_of(
    const qa_app_q2_server_network *network);
qa_app_network_phase qa_app_q2_server_network_phase(const qa_app_q2_server_network *network);
size_t qa_app_q2_server_network_clients(qa_app_q2_server_network *network,
    qa_app_q2_player *out, size_t capacity);
void qa_app_q2_server_network_change_world(qa_app_q2_server_network *network,
    qa_app_q2_server_host host);
void qa_app_q2_server_network_heartbeat(qa_app_q2_server_network *network, double now);
bool qa_app_q2_server_network_disconnect_client(qa_app_q2_server_network *network,
    qa_client_id client, const char *reason);
const void *const *qa_app_q2_server_network_poll(qa_app_q2_server_network *network,
    double now, size_t *out_count);
void qa_app_q2_server_network_submit(qa_app_q2_server_network *network,
    const void *const *commands, size_t count, double now);
/* output is a qa_simulation_output; its snapshot frame time is the MVD source clock. */
void qa_app_q2_server_network_publish(qa_app_q2_server_network *network,
    const void *output, const void *const *events, size_t event_count, double now);
void qa_app_q2_server_network_close(qa_app_q2_server_network *network);
void qa_app_q2_server_network_server_recording_seed(qa_app_q2_server_network *network,
    qa_app_q2_server_recording_seed *out);
void qa_app_q2_server_recording_seed_free(qa_app_q2_server_recording_seed *seed);
void *qa_app_q2_server_network_server_recording_attach(qa_app_q2_server_network *network,
    qa_app_q2_recording_sink sink);
void qa_app_q2_server_network_server_recording_detach(qa_app_q2_server_network *network,
    void *token);
void qa_app_q2_server_network_mvd_recording_seed(qa_app_q2_server_network *network,
    qa_app_q2_mvd_recording_seed *out);
void qa_app_q2_mvd_recording_seed_free(qa_app_q2_mvd_recording_seed *seed);
void *qa_app_q2_server_network_mvd_recording_attach(qa_app_q2_server_network *network,
    qa_app_q2_recording_sink sink);
void qa_app_q2_server_network_mvd_recording_detach(qa_app_q2_server_network *network,
    void *token);
qa_app_application_network qa_app_q2_server_network_as_network(qa_app_q2_server_network *network);
typedef struct qa_app_q2_client_network qa_app_q2_client_network;
qa_app_q2_client_network *qa_app_q2_client_network_create(qa_app_q2_client_network_options options);
void qa_app_q2_client_network_destroy(qa_app_q2_client_network *network);
const qa_app_q2_client_network_options *qa_app_q2_client_network_options_of(
    const qa_app_q2_client_network *network);
qa_q2_server_message_reader *qa_app_q2_client_network_reader(qa_app_q2_client_network *network);
qa_app_q2_client_receiver *qa_app_q2_client_network_receiver(qa_app_q2_client_network *network);
qa_app_network_phase qa_app_q2_client_network_phase(const qa_app_q2_client_network *network);
double qa_app_q2_client_network_acknowledged_frame(const qa_app_q2_client_network *network);
void qa_app_q2_client_network_command(qa_app_q2_client_network *network, const char *text);
void qa_app_q2_client_network_userinfo(qa_app_q2_client_network *network, const char *text);
const void *const *qa_app_q2_client_network_poll(qa_app_q2_client_network *network,
    double now, size_t *out_count);
void qa_app_q2_client_network_submit(qa_app_q2_client_network *network,
    const void *const *commands, size_t count, double now);
void qa_app_q2_client_network_publish(qa_app_q2_client_network *network,
    const void *output, const void *const *events, size_t count, double now);
void qa_app_q2_client_network_close(qa_app_q2_client_network *network);
void qa_app_q2_client_network_recording_seed(qa_app_q2_client_network *network,
    qa_app_q2_recording_seed *out);
void *qa_app_q2_client_network_recording_attach(qa_app_q2_client_network *network,
    qa_app_q2_recording_sink sink);
void qa_app_q2_client_network_recording_detach(qa_app_q2_client_network *network, void *token);
qa_app_application_network qa_app_q2_client_network_as_network(qa_app_q2_client_network *network);
#endif
