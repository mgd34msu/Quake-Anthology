/* Port of src/app/bootstrap/network/unified-server.ts
 * (UnifiedServerNetwork). The authoritative host mirrors
 * UnifiedApplicationServerHost (simulation/network-unified owner
 * in TS; declared here since the landed header carries only the
 * presentation half). A fault raised while handling one peer's
 * message (decode error, queue overflow, host failure) is caught
 * and drops that peer, as the TS try/catch does. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_UNIFIED_SERVER_H
#define QA_APP_BOOTSTRAP_NETWORK_UNIFIED_SERVER_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/network/types.h"
#include "qa/app/bootstrap/network/unified_component_publication.h"
#include "qa/app/bootstrap/network/unified_frame_codec.h"
#include "qa/network/common/transport.h"
#include "qa/persistence/value.h"
typedef qa_app_network_player qa_app_unified_player;
typedef struct qa_app_unified_admission {
  bool accepted; qa_app_unified_player player; const char *reason;
} qa_app_unified_admission;
typedef struct qa_app_unified_server_host {
  void *ctx;
  const char *mode;
  double max_clients;
  qa_session_id session;
  bool (*session_matches)(void *ctx, qa_session_id session);
  qa_app_unified_actor_reference (*actor_reference)(void *ctx, qa_actor_id actor);
  qa_app_unified_admission (*admit)(void *ctx, const qa_net_address *address,
      const char *userinfo);
  void (*disconnect)(void *ctx, qa_app_unified_player player);
  qa_app_unified_player (*carried_player)(void *ctx, qa_client_id client);
  void (*userinfo)(void *ctx, qa_app_unified_player player, const char *value);
  void (*command)(void *ctx, qa_app_unified_player player, const char *name,
      const char *const *args, size_t count);
  /* NULL = TS undefined. */
  bool (*component_command)(void *ctx, qa_app_unified_player player,
      qa_presentation_owner owner, double generation,
      const char *const *args, size_t count);
  qa_actor_command (*input)(void *ctx, qa_app_unified_player player, double sequence,
      const qa_user_command *command, const qa_arsenal_intent *arsenal);
  qa_save_value *const *(*initial_presentation)(void *ctx, qa_app_unified_player player,
      size_t *out_count);
  const qa_app_unified_resource_key *(*resources)(void *ctx, qa_app_unified_player player,
      const void *output, size_t *out_count);
  qa_save_value *const *(*presentation_events)(void *ctx, qa_app_unified_player player,
      const void *const *events, size_t count, size_t *out_count);
  /* Returned frame is owned by the caller (free with _frame_free). */
  qa_app_unified_presentation_frame_t (*frame)(void *ctx, qa_app_unified_player player,
      const void *output, double epoch, double acknowledged_input);
  /* NULL fn = TS undefined. */
  const qa_app_unified_component_publication *(*components)(void *ctx,
      qa_app_unified_player player, size_t *out_count);
  const qa_app_unified_native_publication *(*native_components)(void *ctx,
      qa_app_unified_player player, size_t *out_count);
} qa_app_unified_server_host;
typedef struct qa_app_unified_server_options {
  qa_datagram_transport *transport;
  qa_app_unified_server_host host;
  qa_app_unified_composition *composition;
  void *print_ctx;
  void (*print)(void *ctx, const char *text);
} qa_app_unified_server_options;
typedef struct qa_app_unified_server_network qa_app_unified_server_network;
qa_app_unified_server_network *qa_app_unified_server_network_create(
    qa_app_unified_server_options options);
void qa_app_unified_server_network_destroy(qa_app_unified_server_network *network);
qa_app_network_phase qa_app_unified_server_network_phase(
    const qa_app_unified_server_network *network);
void qa_app_unified_server_network_wire(const qa_app_unified_server_network *network,
    const char **composition_digest, const qa_save_value **snapshot_schema);
size_t qa_app_unified_server_network_clients(qa_app_unified_server_network *network,
    qa_app_unified_player *out, size_t capacity);
bool qa_app_unified_server_network_disconnect_client(qa_app_unified_server_network *network,
    qa_client_id client, const char *reason);
const qa_actor_command *qa_app_unified_server_network_poll(
    qa_app_unified_server_network *network, double now, size_t *out_count);
void qa_app_unified_server_network_submit(qa_app_unified_server_network *network,
    const void *const *commands, size_t count, double now);
void qa_app_unified_server_network_publish(qa_app_unified_server_network *network,
    const void *output, const void *const *events, size_t count, double now);
void qa_app_unified_server_network_change_world(qa_app_unified_server_network *network,
    qa_app_unified_server_host host, qa_app_unified_composition *composition);
void qa_app_unified_server_network_close(qa_app_unified_server_network *network);
qa_app_application_network qa_app_unified_server_network_as_network(
    qa_app_unified_server_network *network);
#endif
