/* Port of src/app/bootstrap/network/q1.ts (Q1ServerNetwork).
 * Game-state baselines ride as the entry array below in the
 * q1-types baselines slot; frame message arrays are
 * qa_netquake_message arrays, entities qa_q1_extended_entity_state
 * arrays. Transport is a qa_datagram_transport*. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q1_H
#define QA_APP_BOOTSTRAP_NETWORK_Q1_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/q1_types.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/network/common/transport.h"
#include "qa/network/q1/netquake.h"
typedef struct qa_app_q1_baseline_entry {
  double number; qa_q1_extended_entity_state state;
} qa_app_q1_baseline_entry;
typedef struct qa_app_q1_baselines {
  const qa_app_q1_baseline_entry *entries; size_t count;
} qa_app_q1_baselines;
typedef struct qa_app_q1_client_ping { qa_client_id client; double ping; } qa_app_q1_client_ping;
typedef struct qa_app_q1_server_network qa_app_q1_server_network;
qa_app_q1_server_network *qa_app_q1_server_network_create(qa_app_q1_server_network_options options);
void qa_app_q1_server_network_destroy(qa_app_q1_server_network *network);
const qa_app_q1_server_network_options *qa_app_q1_server_network_options_of(
    const qa_app_q1_server_network *network);
/* Fills up to capacity ping rows; returns total count. */
size_t qa_app_q1_server_network_client_pings(qa_app_q1_server_network *network,
    qa_app_q1_client_ping *out, size_t capacity);
bool qa_app_q1_server_network_address(qa_app_q1_server_network *network, qa_net_address *out);
qa_app_network_phase qa_app_q1_server_network_phase(const qa_app_q1_server_network *network);
size_t qa_app_q1_server_network_clients(qa_app_q1_server_network *network,
    qa_app_q1_player *out, size_t capacity);
void qa_app_q1_server_network_change_world(qa_app_q1_server_network *network,
    qa_app_q1_server_host host);
bool qa_app_q1_server_network_disconnect_client(qa_app_q1_server_network *network,
    qa_client_id client, const char *reason);
/* Commands are the opaque ActorCommand pointers from host.input. */
const void *const *qa_app_q1_server_network_poll(qa_app_q1_server_network *network,
    double now, size_t *out_count);
/* Always aborts "Server input must enter the application input batch". */
void qa_app_q1_server_network_submit(qa_app_q1_server_network *network,
    const void *const *commands, size_t count, double now);
void qa_app_q1_server_network_publish(qa_app_q1_server_network *network,
    const void *output, const void *const *events, size_t event_count,
    double frame_time_seconds, double now);
void qa_app_q1_server_network_close(qa_app_q1_server_network *network);
qa_app_application_network qa_app_q1_server_network_as_network(qa_app_q1_server_network *network);
#endif
