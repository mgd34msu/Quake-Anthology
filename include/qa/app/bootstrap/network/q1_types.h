/* Port of src/app/bootstrap/network/q1-types.ts. NetQuake messages,
 * simulation output and wire admission are landed payloads forwarded
 * as borrowed opaque pointers. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q1_TYPES_H
#define QA_APP_BOOTSTRAP_NETWORK_Q1_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/protocol.h"
typedef struct qa_app_q1_player {
  qa_client_id client; qa_actor_id actor; double source_entity;
} qa_app_q1_player;
typedef struct qa_app_q1_game_state {
  const void *info;       /* server-info message */
  const void *baselines;  /* number -> Q1ExtendedEntityState map */
  const void *const *signon; size_t signon_count;
} qa_app_q1_game_state;
typedef struct qa_app_q1_admission {
  bool accepted; qa_app_q1_player player; const char *reason;
} qa_app_q1_admission;
typedef struct qa_app_q1_frame {
  double seconds;
  const void *const *messages; size_t message_count;
  const void *const *reliable; size_t reliable_count;
  const void *const *datagram; size_t datagram_count;
  const void *const *entities; size_t entity_count;
} qa_app_q1_frame;
typedef struct qa_app_q1_server_host {
  void *ctx;
  qa_q1_protocol_identity protocol; double max_clients; const char *map_name;
  bool (*rejects)(void *ctx, const void *address);
  const void *(*supports_source_wire)(void *ctx);
  qa_app_q1_admission (*admit)(void *ctx, const void *from);
  qa_app_q1_player (*carried_player)(void *ctx, qa_client_id client);
  void (*disconnect)(void *ctx, qa_app_q1_player player, const char *reason);
  qa_app_q1_game_state (*game_state)(void *ctx, qa_app_q1_player player);
  const void *const *(*spawn)(void *ctx, qa_app_q1_player player, size_t *out_count);
  qa_app_q1_frame (*frame)(void *ctx, qa_app_q1_player player, const void *output);
  const void *(*input)(void *ctx, qa_app_q1_player player, const qa_q1_user_command *command, double sequence);
  void (*command)(void *ctx, qa_app_q1_player player, const char *name, const char *const *args, size_t count);
  void (*observe)(void *ctx, const void *output, const void *const *events, size_t count);
  void (*print)(void *ctx, const char *text);
} qa_app_q1_server_host;
typedef struct qa_app_q1_server_network_options {
  void *transport; qa_app_q1_server_host host;
  bool has_timeout_milliseconds; double timeout_milliseconds;
} qa_app_q1_server_network_options;
#endif
