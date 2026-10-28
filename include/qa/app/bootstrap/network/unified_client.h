/* Port of src/app/bootstrap/network/unified-client.ts
 * (UnifiedClientNetwork). Channel flush maps to landed
 * transmit_reliable + transmit_frame. Poll failures set rejected,
 * notify disconnected, close, then abort with the TS message
 * (the TS rethrow). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_CLIENT_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_CLIENT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/remote_unified.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/network/common/transport.h"
typedef struct qa_app_unified_client_options {
  qa_datagram_transport *transport;
  qa_net_address remote;
  qa_app_unified_remote *host;
  void *userinfo_ctx;
  const char *(*userinfo)(void *ctx);
} qa_app_unified_client_options;
typedef struct qa_app_unified_client_network qa_app_unified_client_network;
qa_app_unified_client_network *qa_app_unified_client_network_create(
    qa_app_unified_client_options options);
void qa_app_unified_client_network_destroy(qa_app_unified_client_network *network);
const qa_app_unified_client_options *qa_app_unified_client_network_options_of(
    const qa_app_unified_client_network *network);
qa_app_network_phase qa_app_unified_client_network_phase(
    const qa_app_unified_client_network *network);
/* TS wire getter: digest + snapshotSchema from the negotiated
 * composition (aborts before negotiation). */
void qa_app_unified_client_network_wire(const qa_app_unified_client_network *network,
    const char **composition_digest, const qa_save_value **snapshot_schema);
const void *const *qa_app_unified_client_network_poll(qa_app_unified_client_network *network,
    double now, size_t *out_count);
void qa_app_unified_client_network_submit(qa_app_unified_client_network *network,
    const qa_actor_command *commands, size_t count, double now);
void qa_app_unified_client_network_submit_timed(qa_app_unified_client_network *network,
    const qa_actor_command *commands, size_t count, double now, double elapsed_milliseconds);
void qa_app_unified_client_network_command(qa_app_unified_client_network *network,
    const char *text);
void qa_app_unified_client_network_player_command(qa_app_unified_client_network *network,
    const char *name, const char *const *args, size_t count);
void qa_app_unified_client_network_component_command(qa_app_unified_client_network *network,
    qa_presentation_owner owner, double generation, const char *const *args, size_t count);
void qa_app_unified_client_network_userinfo(qa_app_unified_client_network *network,
    const char *value);
void qa_app_unified_client_network_publish(qa_app_unified_client_network *network,
    const void *output, const void *const *events, size_t count, double now);
void qa_app_unified_client_network_close(qa_app_unified_client_network *network);
qa_app_application_network qa_app_unified_client_network_as_network(
    qa_app_unified_client_network *network);
#endif
