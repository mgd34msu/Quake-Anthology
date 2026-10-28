/* Port of src/app/bootstrap/network/q3-types.ts. Landed Q3 wire
 * payloads are forwarded as borrowed opaque pointers.
 * Q3GameCallbackError aborts with the exact TS message.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_Q3_TYPES_H
#define QA_APP_BOOTSTRAP_NETWORK_Q3_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/identity.h"
typedef struct qa_app_q3_player {
  qa_client_id client; qa_actor_id actor; double source_entity;
} qa_app_q3_player;
typedef struct qa_app_q3_admission {
  bool accepted; qa_app_q3_player player; const char *reason;
} qa_app_q3_admission;
typedef struct qa_app_q3_source_round_binding {
  void *ctx;
  void (*preflight)(void *ctx);
  void (*rebind)(void *ctx);
  qa_app_q3_admission (*reconnect)(void *ctx, qa_client_id client, const char *userinfo, const void *last_command);
} qa_app_q3_source_round_binding;
typedef struct qa_app_q3_network_round_restart {
  void *ctx;
  const qa_client_id *clients; size_t client_count;
  int snapshot_server_bit; /* 0 | 4 */
  void (*bind_source)(void *ctx);
  void (*receive_events)(void *ctx, const void *const *events, size_t count);
  bool (*reconnect_client)(void *ctx, qa_client_id client);
} qa_app_q3_network_round_restart;
typedef struct qa_app_q3_snapshot {
  const void *player; const uint8_t *area_mask; size_t area_mask_length;
  const void *const *entities; size_t entity_count;
} qa_app_q3_snapshot;
typedef struct qa_app_q3_admission_settings {
  const void *ctx;
  /* Picked Q3ServerAdmissionBindings fields (NULL = TS defaults). */
  int64_t (*private_clients)(const void *ctx);
  const char *(*private_password)(const void *ctx);
  double (*reconnect_limit_seconds)(const void *ctx);
  double (*minimum_ping)(const void *ctx);
  double (*maximum_ping)(const void *ctx);
  bool (*demo_restricted)(const void *ctx);
  bool (*enabled)(const void *ctx);
  const char *(*game_directory)(const void *ctx);
  const char *(*strict_auth)(const void *ctx);
  bool (*flood_protect)(const void *ctx);
} qa_app_q3_admission_settings;
typedef struct qa_app_q3_server_host {
  void *ctx;
  bool has_admission; qa_app_q3_admission_settings admission;
  const void *administration;
  bool has_source_round; qa_app_q3_source_round_binding source_round;
  const void *product; double max_clients;
  void (*prepare)(void *ctx, double checksum_feed, double server_id,
      void (*configstring)(void *, double, const char *), void *configstring_ctx);
  const void *(*pure)(void *ctx, double server_id, const double *checksum_feed_server_id);
  bool (*downloads_enabled)(void *ctx);
  const void *(*open_download)(void *ctx, const char *name);
  const void *(*rate)(void *ctx, qa_app_q3_player player);
  const void *(*supports_source_wire)(void *ctx);
  double (*time)(void *ctx);
  const double *(*occupied_slots)(void *ctx, size_t *out_count);
  qa_app_q3_admission (*admit)(void *ctx, const void *request);
  qa_app_q3_player (*carried_player)(void *ctx, qa_client_id client);
  void (*disconnect)(void *ctx, qa_app_q3_player player, const char *reason);
  const void *(*game_state)(void *ctx, qa_app_q3_player player, double server_id);
  qa_app_q3_snapshot (*snapshot)(void *ctx, qa_app_q3_player player);
  void (*begin)(void *ctx, qa_app_q3_player player, const void *command);
  const void *(*input)(void *ctx, qa_app_q3_player player, const void *command, double sequence);
  void (*command)(void *ctx, qa_app_q3_player player, const char *name, const char *const *args, size_t count);
  void (*userinfo)(void *ctx, qa_app_q3_player player, const char *value);
  const char *(*status)(void *ctx, const char *challenge, bool detailed);
  void (*print)(void *ctx, const char *text);
} qa_app_q3_server_host;
/* TS q3GameCallback: fn returns borrowed error text or NULL. */
void qa_app_q3_game_callback(const char *(*fn)(void *ctx), void *ctx);
#endif
