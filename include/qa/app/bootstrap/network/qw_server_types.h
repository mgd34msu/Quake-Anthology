/* Port of src/app/bootstrap/network/qw-server-types.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_QW_SERVER_TYPES_H
#define QA_APP_BOOTSTRAP_NETWORK_QW_SERVER_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/protocol.h"
typedef struct qa_app_qw_player {
  qa_client_id client; qa_actor_id actor; double slot;
} qa_app_qw_player;
typedef struct qa_app_qw_admission {
  bool accepted; qa_app_qw_player player; const char *reason;
} qa_app_qw_admission;
typedef struct qa_app_qw_frame {
  const void *const *entities; size_t entity_count;
  const void *const *messages; size_t message_count;
  const void *const *reliable; size_t reliable_count;
} qa_app_qw_frame;
typedef struct qa_app_qw_bytes { const uint8_t *data; size_t length; } qa_app_qw_bytes;
typedef void (*qa_app_qw_action_fn)(void *ctx);
typedef void (*qa_app_qw_emit_fn)(void *ctx, qa_app_qw_player recipient, const void *message);
typedef struct qa_app_qw_server_host {
  void *ctx;
  const void *administration; const void *authentication;
  double max_clients; bool paused;
  const void *const *(*masters)(void *ctx, size_t *out_count);
  const void *(*supports_source_wire)(void *ctx);
  qa_app_qw_admission (*admit)(void *ctx, const void *request);
  qa_app_qw_player (*carried_player)(void *ctx, qa_client_id client);
  qa_app_qw_player (*recording_player)(void *ctx, qa_client_id client);
  const qa_app_qw_bytes *(*recording_signon)(void *ctx, qa_app_qw_player player, size_t *out_count);
  const void *(*client_info)(void *ctx, qa_app_qw_player player); /* string map, opaque */
  void (*command_phase)(void *ctx, qa_app_qw_player player, qa_app_qw_action_fn action, void *action_ctx,
      qa_app_qw_emit_fn emit, void *emit_ctx);
  void (*disconnect)(void *ctx, qa_app_qw_player player, const char *reason);
  const void *(*signon)(void *ctx, qa_app_qw_player player);
  const void *(*prepare_download)(void *ctx, qa_app_qw_player player, const char *path);
  const void *const *(*baselines)(void *ctx, qa_app_qw_player player, size_t *out_count);
  qa_app_qw_frame (*frame)(void *ctx, qa_app_qw_player player, const void *output);
  void (*command_group)(void *ctx, qa_app_qw_player player,
      const qa_qw_user_command *commands, size_t count, double sequence);
  void (*command)(void *ctx, qa_app_qw_player player, const char *name, const char *const *args, size_t count);
  void (*observe)(void *ctx, const void *output, const void *const *events, size_t count);
  void (*print)(void *ctx, const char *text);
} qa_app_qw_server_host;
typedef struct qa_app_qw_server_network_options {
  void *transport; qa_app_qw_server_host host;
  double (*random)(void *ctx); void *random_ctx;
  bool has_timeout_milliseconds; double timeout_milliseconds;
} qa_app_qw_server_network_options;
#endif
