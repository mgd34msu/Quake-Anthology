/* Port of src/app/bootstrap/network/q3.ts (Q3ServerNetwork).
 * Operation serialization is synchronous: a reentrant call raises
 * "Error" with the TS message. Each received datagram runs under qa_try
 * as the TS per-packet catch: the message is printed (budgeted, made
 * printable) and a Q3GameCallbackError costs the sending peer its
 * connection; the server keeps running. Host gameState/snapshot/pure/rate payloads
 * are the landed qa_q3_gamestate / snapshot records /
 * qa_q3_pure_server / qa_q3_server_rate behind the q3-types
 * opaque pointers. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q3_H
#define QA_APP_BOOTSTRAP_NETWORK_Q3_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/q3_types.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/app/bootstrap/simulation/q3/guest_runtime.h"
#include "qa/network/common/transport.h"
#include "qa/network/q3/authorization.h"
typedef struct qa_app_q3_server_network_options {
  qa_datagram_transport *transport;
  qa_app_q3_server_host host;
  double (*random)(void *ctx); void *random_ctx;
  bool has_timeout_milliseconds; double timeout_milliseconds;
  qa_q3_auth_resolve_fn resolve_authorization; void *resolve_ctx;
} qa_app_q3_server_network_options;
typedef struct qa_app_q3_server_network qa_app_q3_server_network;
typedef struct qa_app_q3_recording_source {
  const qa_app_q3_server_host *host; double server_id; int snapshot_server_bit;
} qa_app_q3_recording_source;
qa_app_q3_server_network *qa_app_q3_server_network_create(qa_app_q3_server_network_options options);
void qa_app_q3_server_network_destroy(qa_app_q3_server_network *network);
void qa_app_q3_server_network_heartbeat(qa_app_q3_server_network *network, double now);
qa_app_q3_recording_source qa_app_q3_server_network_recording_source(
    qa_app_q3_server_network *network);
bool qa_app_q3_server_network_address(qa_app_q3_server_network *network, qa_net_address *out);
qa_app_network_phase qa_app_q3_server_network_phase(const qa_app_q3_server_network *network);
size_t qa_app_q3_server_network_clients(qa_app_q3_server_network *network,
    qa_app_q3_player *out, size_t capacity);
const void *const *qa_app_q3_server_network_poll(qa_app_q3_server_network *network,
    double now, size_t *out_count);
void qa_app_q3_server_network_submit(qa_app_q3_server_network *network,
    const void *const *commands, size_t count, double now);
void qa_app_q3_server_network_publish(qa_app_q3_server_network *network,
    const void *output, const void *const *events, size_t count, double now);
qa_app_q3_guest_output qa_app_q3_server_network_game_output(qa_app_q3_server_network *network);
bool qa_app_q3_server_network_disconnect_client(qa_app_q3_server_network *network,
    qa_client_id client, const char *reason);
typedef struct qa_app_q3_rejection { qa_client_id client; const char *reason; } qa_app_q3_rejection;
void qa_app_q3_server_network_change_world(qa_app_q3_server_network *network,
    qa_app_q3_server_host host, const qa_app_q3_rejection *rejected, size_t count);
void qa_app_q3_server_network_restart_source_round(qa_app_q3_server_network *network,
    void (*run)(void *ctx, qa_app_q3_network_round_restart *round), void *run_ctx,
    void (*on_mutation)(void *ctx), void *mutation_ctx);
void qa_app_q3_server_network_close(qa_app_q3_server_network *network);
qa_app_application_network qa_app_q3_server_network_as_network(qa_app_q3_server_network *network);
#endif
