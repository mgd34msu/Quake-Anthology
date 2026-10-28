/* Port of src/app/bootstrap/network/q3-client.ts. The recording
 * seed/sink are sibling demo-recording types carried locally.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q3_CLIENT_H
#define QA_APP_BOOTSTRAP_NETWORK_Q3_CLIENT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/network/types.h"
#include "qa/core/cvars/index.h"
#include "qa/network/common/transport.h"
#include "qa/network/q3/admission.h"
#include "qa/network/q3/client.h"
typedef void (*qa_app_q3_assert_fn)(void *ctx);
typedef struct qa_app_q3_client_host {
  void *ctx;
  qa_q3_connection_identity identity;
  bool (*downloading)(void *ctx);
  const char *(*userinfo)(void *ctx);
  void (*system_info)(void *ctx, const char *info);
  void (*snapshot)(void *ctx, const qa_q3_wire_snapshot *snapshot, double ping);
  void (*map_restart)(void *ctx);
  void (*print)(void *ctx, const char *text);
  double (*download_size)(void *ctx, double size);
  void (*download)(void *ctx, const qa_q3_download_block *block);
  void (*clear_active)(void *ctx, qa_app_q3_assert_fn assert_current, void *assert_ctx);
  void (*gamestate)(void *ctx, const qa_q3_gamestate *state, double generation,
      qa_app_q3_assert_fn assert_current, void *assert_ctx);
  void (*attach)(void *ctx, qa_q3_client_connection *connection);
  qa_q3_wire_user_command (*command)(void *ctx, const void *actor_command);
  void (*disconnected)(void *ctx, const char *reason);
} qa_app_q3_client_host;
typedef struct qa_app_q3_client_lifecycle {
  void *ctx;
  void (*assert_current)(void *ctx);
  void (*cleared)(void *ctx);
  void (*primed)(void *ctx);
  void (*snapshot)(void *ctx, const qa_q3_wire_snapshot *snapshot, double ping);
} qa_app_q3_client_lifecycle;
/* Builds landed Q3ClientBindings over the application host. The
 * storage is caller-owned (bindings ctx points into it). */
typedef struct qa_app_q3_client_bindings_storage {
  qa_app_q3_client_host host;
  qa_app_q3_client_lifecycle lifecycle;
  qa_q3_client_bindings bindings;
} qa_app_q3_client_bindings_storage;
void qa_app_q3_application_client_bindings(qa_app_q3_client_bindings_storage *storage,
    qa_app_q3_client_host host, qa_app_q3_client_lifecycle lifecycle);
typedef struct qa_app_q3_recording_sink {
  void *ctx;
  void (*append)(void *ctx, double sequence, const uint8_t *message, size_t length);
} qa_app_q3_recording_sink;
typedef struct qa_app_q3_recording_seed {
  double sequence; uint8_t *message; size_t length; /* owned */
} qa_app_q3_recording_seed;
typedef struct qa_app_q3_authorization {
  void *ctx;
  void (*request)(void *ctx, qa_app_q3_assert_fn assert_current, void *assert_ctx,
      void (*send)(void *send_ctx, const qa_q3_address *address,
          const uint8_t *payload, size_t length), void *send_ctx);
} qa_app_q3_authorization;
typedef struct qa_app_q3_client_network_options {
  qa_datagram_transport transport;
  qa_q3_address remote; /* ipv4/ipx (strings borrowed) */
  qa_app_q3_client_host host;
  double qport;
  qa_cvar_registry *cvars; /* may be NULL */
  bool has_timeout_milliseconds; double timeout_milliseconds;
  bool has_authorization; qa_app_q3_authorization authorization;
  double (*now)(void *ctx); void *now_ctx; /* TS performance.now */
} qa_app_q3_client_network_options;
typedef struct qa_app_q3_client_network qa_app_q3_client_network;
qa_app_q3_client_network *qa_app_q3_client_network_create(qa_app_q3_client_network_options options);
void qa_app_q3_client_network_destroy(qa_app_q3_client_network *network);
const qa_app_q3_client_network_options *qa_app_q3_client_network_options_of(
    const qa_app_q3_client_network *network);
qa_app_network_phase qa_app_q3_client_network_phase(const qa_app_q3_client_network *network);
qa_q3_client_connection *qa_app_q3_client_network_native(qa_app_q3_client_network *network);
double qa_app_q3_client_network_connect_packet_count(const qa_app_q3_client_network *network);
void qa_app_q3_client_network_command(qa_app_q3_client_network *network, const char *text);
void qa_app_q3_client_network_send_packet(qa_app_q3_client_network *network);
void qa_app_q3_client_network_poll(qa_app_q3_client_network *network, double now);
void qa_app_q3_client_network_submit(qa_app_q3_client_network *network,
    const void *const *commands, size_t count, double now);
/* Always aborts "Remote client cannot publish authoritative state". */
void qa_app_q3_client_network_publish(qa_app_q3_client_network *network,
    const void *output, const void *const *events, size_t count, double now);
void qa_app_q3_client_network_close(qa_app_q3_client_network *network);
void qa_app_q3_client_network_recording_seed(qa_app_q3_client_network *network,
    qa_app_q3_recording_seed *out);
/* Returns a detach token to pass to _recording_detach. */
void *qa_app_q3_client_network_recording_attach(qa_app_q3_client_network *network,
    qa_app_q3_recording_sink sink);
void qa_app_q3_client_network_recording_detach(qa_app_q3_client_network *network, void *token);
qa_app_application_network qa_app_q3_client_network_as_network(qa_app_q3_client_network *network);
#endif
