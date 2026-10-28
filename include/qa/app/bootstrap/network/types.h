/* Port of src/app/bootstrap/network/types.ts. Host interfaces become
 * fn+ctx vtables; landed wire/session payloads that this header only
 * forwards ride as borrowed opaque pointers (the owning network
 * headers define them). Q2GameCallbackError aborts with the exact TS
 * message. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_NETWORK_TYPES_H
#define QA_APP_BOOTSTRAP_NETWORK_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/identity.h"
#include "qa/contracts/protocol.h"
typedef enum qa_app_network_phase {
  QA_APP_PHASE_CHALLENGING, QA_APP_PHASE_CONNECTING, QA_APP_PHASE_LOADING,
  QA_APP_PHASE_ACTIVE, QA_APP_PHASE_CLOSED, QA_APP_PHASE_REJECTED
} qa_app_network_phase;
const char *qa_app_network_phase_name(qa_app_network_phase phase);
typedef enum qa_app_network_role { QA_APP_ROLE_SERVER, QA_APP_ROLE_CLIENT } qa_app_network_role;
/* TS DemoRecordingSeed/DemoRecordingSink (app/bootstrap/demo-recording,
 * sibling slice) and SimulationOutput/presentation events: opaque. */
typedef struct qa_app_network_recording {
  void *ctx;
  const void *(*seed)(void *ctx);
  void (*attach)(void *ctx, void *sink, void **out_detach_ctx);
  void (*detach)(void *detach_ctx);
} qa_app_network_recording;
/* TS ApplicationNetwork. poll returns borrowed ActorCommand array
 * (opaque items); async -> synchronous. wire is a borrowed
 * WireSelection (network/common/session). */
typedef struct qa_app_application_network {
  void *ctx;
  qa_app_network_role role;
  bool has_server_recording; qa_app_network_recording server_recording;
  bool has_mvd_recording; qa_app_network_recording mvd_recording;
  bool has_recording; qa_app_network_recording recording;
  qa_app_network_phase (*phase)(void *ctx);
  const void *(*wire)(void *ctx);
  void (*heartbeat)(void *ctx, double now_milliseconds); /* NULL = absent */
  const void *const *(*poll)(void *ctx, double now_milliseconds, size_t *out_count);
  void (*submit)(void *ctx, const void *const *commands, size_t count, double now_milliseconds);
  void (*publish)(void *ctx, const void *output, const void *const *events, size_t count, double now_milliseconds);
  void (*close)(void *ctx);
} qa_app_application_network;
typedef struct qa_app_network_player {
  qa_client_id client; qa_actor_id actor; double source_entity;
} qa_app_network_player;
typedef qa_app_network_player qa_app_q2_player;
typedef struct qa_app_q2_admission {
  bool accepted; qa_app_q2_player player; const char *reason; /* borrowed when !accepted */
} qa_app_q2_admission;
/* TS Q2ApplicationGameState: data/configStrings/baselines opaque maps. */
typedef struct qa_app_q2_game_state {
  const void *data; const void *config_strings; const void *baselines;
} qa_app_q2_game_state;
/* TS Q2ApplicationServerEvent = Q2ServerWriteEvent & {reliable?}. */
typedef struct qa_app_q2_server_event {
  const void *event; bool has_reliable; bool reliable;
} qa_app_q2_server_event;
typedef struct qa_app_q2_raw_message { const uint8_t *bytes; size_t length; bool reliable; } qa_app_q2_raw_message;
/* TS Q2ApplicationServerHost. Optional TS members are NULL-able.
 * downloads/protocol/message_options/max_clients are value fields. */
typedef struct qa_app_q2_server_host {
  void *ctx;
  const void *downloads; /* qa_app_q2_downloads service, q2_downloads.h */
  qa_q2_protocol_identity protocol;
  const void *message_options; /* Q2ServerMessageOptions, borrowed */
  double max_clients;
  bool (*rejects)(void *ctx, const void *address);
  const void *administration; const void *discovery;
  const void *const *(*masters)(void *ctx, size_t *out_count);
  const void *(*supports_source_wire)(void *ctx);
  void (*observe)(void *ctx, const void *output, const void *const *events, size_t count);
  const void *(*mvd_capture)(void *ctx, const void *output, const void *const *events, size_t count, double servercount);
  bool (*mvd_settings)(void *ctx, bool *enabled, double *max_viewers, const char **password);
  qa_app_q2_admission (*admit)(void *ctx, const void *from, const void *request);
  void (*disconnect)(void *ctx, qa_app_q2_player player, const char *reason);
  qa_app_q2_player (*carried_player)(void *ctx, qa_client_id client);
  void (*begin)(void *ctx, qa_app_q2_player player);
  const qa_app_q2_raw_message *(*raw_messages)(void *ctx, qa_app_q2_player player, size_t *out_count);
  const qa_app_q2_raw_message *(*source_messages)(void *ctx, qa_app_q2_player player, size_t *out_count);
  void (*observe_progress)(void *ctx);
  const qa_app_q2_raw_message *(*local_messages)(void *ctx, qa_app_q2_player player, size_t *out_count);
  qa_app_q2_game_state (*game_state)(void *ctx, qa_app_q2_player player, const qa_q2_protocol_identity *protocol);
  const void *(*frame)(void *ctx, qa_app_q2_player player, const void *output, const qa_q2_protocol_identity *protocol);
  const qa_app_q2_server_event *(*events)(void *ctx, qa_app_q2_player player, const void *output,
      const void *const *events, size_t count, size_t *out_count);
  const void *(*input)(void *ctx, qa_app_q2_player player, const void *command, double sequence);
  void (*command)(void *ctx, qa_app_q2_player player, const char *name, const char *const *args, size_t count);
  void (*command_text)(void *ctx, qa_app_q2_player player, const char *text);
  const char *(*expand_client_command)(void *ctx, const char *text);
  void (*userinfo)(void *ctx, qa_app_q2_player player, const char *value);
  void (*print)(void *ctx, const char *text);
} qa_app_q2_server_host;
/* TS Q2ApplicationClientHost. */
typedef struct qa_app_q2_client_host {
  void *ctx;
  const void *downloads; /* Q2ApplicationClientDownloads or NULL */
  qa_q2_protocol_identity protocol;
  const void *message_options;
  void (*server_data)(void *ctx, const void *data, void (*assert_current)(void *), void *assert_ctx);
  const char *(*userinfo)(void *ctx);
  const char *(*social_id)(void *ctx); /* NULL fn or NULL result = anonymous */
  void (*prediction_sent)(void *ctx, double sequence, const void *command, double now_milliseconds);
  void (*prediction_acknowledged)(void *ctx, double sequence, double now_milliseconds);
  void (*game_state)(void *ctx, qa_app_q2_game_state state);
  void (*frame)(void *ctx, const void *frame, const void *const *records, size_t count, double now_milliseconds);
  void (*records)(void *ctx, const void *const *records, size_t count);
  const void *(*command)(void *ctx, const void *actor_command);
  void (*disconnected)(void *ctx, const char *reason);
  void (*print)(void *ctx, const char *text);
} qa_app_q2_client_host;
/* TS Q2ServerNetworkOptions / Q2ClientNetworkOptions. Transport and
 * address are borrowed landed datagram values (opaque here). */
typedef struct qa_app_q2_server_network_options {
  void *transport; qa_app_q2_server_host host;
  double (*random)(void *ctx); void *random_ctx;
  bool has_timeout_milliseconds; double timeout_milliseconds;
} qa_app_q2_server_network_options;
typedef struct qa_app_q2_client_network_options {
  void *transport; const void *remote; qa_app_q2_client_host host;
  double qport; bool has_timeout_milliseconds; double timeout_milliseconds;
} qa_app_q2_client_network_options;
/* TS q2GameCallback: fn returns a borrowed error message or NULL.
 * A message already carrying the Q2GameCallbackError prefix is
 * rethrown as-is; any other aborts wrapped, per the TS catch. */
void qa_app_q2_game_callback(const char *(*fn)(void *ctx), void *ctx);
#endif
