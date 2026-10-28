/* Port of src/app/bootstrap/network/q1-client.ts (Q1ClientNetwork).
 * Recording seed packets carry view angles as in the sibling
 * demo-recording types. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q1_CLIENT_H
#define QA_APP_BOOTSTRAP_NETWORK_Q1_CLIENT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/network/types.h"
#include "qa/contracts/math.h"
#include "qa/network/common/transport.h"
#include "qa/network/q1/netquake.h"
#include "qa/network/q1/session.h"
typedef struct qa_app_q1_client_host {
  void *ctx;
  void (*receive)(void *ctx, qa_netquake_message *messages, size_t count, double now_milliseconds);
  qa_q1_user_command (*command)(void *ctx, const void *actor_command);
  void (*disconnected)(void *ctx, const char *reason);
} qa_app_q1_client_host;
typedef struct qa_app_q1_client_network_options {
  qa_datagram_transport transport;
  qa_net_address remote; /* strings borrowed */
  qa_app_q1_client_host host;
  qa_netquake_seat_identity seat;
  bool has_timeout_milliseconds; double timeout_milliseconds;
} qa_app_q1_client_network_options;
typedef struct qa_app_q1_recording_packet {
  uint8_t *message; size_t length; qa_vec3 view_angles;
} qa_app_q1_recording_packet;
typedef struct qa_app_q1_recording_seed {
  double protocol_version; double track;
  qa_app_q1_recording_packet *packets; size_t packet_count;
} qa_app_q1_recording_seed;
void qa_app_q1_recording_seed_free(qa_app_q1_recording_seed *seed);
typedef struct qa_app_q1_recording_sink {
  void *ctx;
  void (*append)(void *ctx, const uint8_t *message, size_t length, qa_vec3 view_angles);
} qa_app_q1_recording_sink;
typedef struct qa_app_q1_client_network qa_app_q1_client_network;
qa_app_q1_client_network *qa_app_q1_client_network_create(qa_app_q1_client_network_options options);
void qa_app_q1_client_network_destroy(qa_app_q1_client_network *network);
const qa_app_q1_client_network_options *qa_app_q1_client_network_options_of(
    const qa_app_q1_client_network *network);
qa_app_network_phase qa_app_q1_client_network_phase(const qa_app_q1_client_network *network);
bool qa_app_q1_client_network_server_address(qa_app_q1_client_network *network, qa_net_address *out);
void qa_app_q1_client_network_command(qa_app_q1_client_network *network, const char *text);
const void *const *qa_app_q1_client_network_poll(qa_app_q1_client_network *network,
    double now, size_t *out_count);
void qa_app_q1_client_network_submit(qa_app_q1_client_network *network,
    const void *const *commands, size_t count, double now);
void qa_app_q1_client_network_publish(qa_app_q1_client_network *network,
    const void *output, const void *const *events, size_t count, double now);
void qa_app_q1_client_network_close(qa_app_q1_client_network *network);
void qa_app_q1_client_network_recording_seed(qa_app_q1_client_network *network,
    qa_app_q1_recording_seed *out);
void *qa_app_q1_client_network_recording_attach(qa_app_q1_client_network *network,
    qa_app_q1_recording_sink sink);
void qa_app_q1_client_network_recording_detach(qa_app_q1_client_network *network, void *token);
qa_app_application_network qa_app_q1_client_network_as_network(qa_app_q1_client_network *network);
#endif
