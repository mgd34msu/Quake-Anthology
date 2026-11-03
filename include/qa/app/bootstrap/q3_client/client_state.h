/* Port of src/app/bootstrap/q3-client/client-state.ts.
 * CommonError("drop", ...) aborts via qa_common_error; the async
 * getServerCommand is synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_CLIENT_STATE_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_CLIENT_STATE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qvm/client_state.h"
#include "qa/contracts/identity.h"
#include "qa/network/q3/client.h"
#include "qa/network/q3/game_state.h"
#include "qa/network/q3/server_message.h"
/* TS LocalQ3ClientBindings. sound_actor NULL = TS undefined. */
typedef struct qa_app_local_q3_client_bindings {
  void *ctx;
  void (*assert_current)(void *ctx);
  qa_actor_id (*actor_at)(void *ctx, double number);
  bool (*sound_actor)(void *ctx, double number, qa_actor_id *out); /* false = null */
  void (*system_info)(void *ctx, const char *info);
  void (*map_restart)(void *ctx);
  void (*level_shot)(void *ctx);
  void (*print)(void *ctx, const char *text);
} qa_app_local_q3_client_bindings;
/* TS LocalQ3ClientState. */
typedef struct qa_app_local_q3_client_state qa_app_local_q3_client_state;
qa_app_local_q3_client_state *qa_app_local_q3_client_state_create(
    const qa_q3_gamestate *initial, qa_app_local_q3_client_bindings bindings);
void qa_app_local_q3_client_state_destroy(qa_app_local_q3_client_state *state);
/* TS readonly fields. */
qa_q3_client_game_state_storage *qa_app_local_q3_client_state_game_state(qa_app_local_q3_client_state *state);
qa_q3_wire_command_history *qa_app_local_q3_client_state_commands(qa_app_local_q3_client_state *state);
double qa_app_local_q3_client_state_client_number(const qa_app_local_q3_client_state *state);
double qa_app_local_q3_client_state_generation(const qa_app_local_q3_client_state *state);
double qa_app_local_q3_client_state_server_message_sequence(const qa_app_local_q3_client_state *state);
double qa_app_local_q3_client_state_server_command_sequence(const qa_app_local_q3_client_state *state);
double qa_app_local_q3_client_state_last_executed_server_command(const qa_app_local_q3_client_state *state);
/* TS source.* accessors. */
double qa_app_local_q3_client_state_time(qa_app_local_q3_client_state *state);
/* source.current(): false when no snapshot yet (TS current throws
 * through HistorySnapshotSource in that case in practice). */
bool qa_app_local_q3_client_state_current(qa_app_local_q3_client_state *state, qa_q3_wire_snapshot *out);
/* source.read(number): false is the TS null; out owned (destroy
 * with qa_q3_wire_snapshot_destroy). */
bool qa_app_local_q3_client_state_read(qa_app_local_q3_client_state *state, double number, qa_q3_wire_snapshot *out);
/* source.getGameState(): malloc'd strings (caller frees each+array). */
char **qa_app_local_q3_client_state_game_state_strings(qa_app_local_q3_client_state *state, size_t *out_count);
/* source.systemInfo(): malloc'd ("" when absent). */
char *qa_app_local_q3_client_state_system_info(qa_app_local_q3_client_state *state);
qa_actor_id qa_app_local_q3_client_state_actor_at(qa_app_local_q3_client_state *state, double number);
bool qa_app_local_q3_client_state_sound_actor(qa_app_local_q3_client_state *state, double number, qa_actor_id *out);
bool qa_app_local_q3_client_state_has_sound_actor(const qa_app_local_q3_client_state *state);
/* TS receiveServerCommand / receiveSnapshot (exact Error texts). */
void qa_app_local_q3_client_state_receive_server_command(qa_app_local_q3_client_state *state,
                                                         double sequence, const char *text);
void qa_app_local_q3_client_state_receive_snapshot(qa_app_local_q3_client_state *state,
                                                   const qa_q3_wire_snapshot *snapshot, double ping);
/* TS snapshotPing: false is the TS null. */
bool qa_app_local_q3_client_state_snapshot_ping(qa_app_local_q3_client_state *state, double number, double *out);
/* TS getServerCommand: false is the TS null; argv strings owned. */
bool qa_app_local_q3_client_state_get_server_command(qa_app_local_q3_client_state *state, double sequence,
                                                     char ***argv_out, size_t *argc_out);
void qa_app_local_q3_client_state_retire(qa_app_local_q3_client_state *state);
/* TS readonly source: the ApplicationQ3ClientSource view (q3_client.h), owned by the state. Its borrowed results
 * (game state strings, system info, server command) stay valid until the next call of the same member. */
struct qa_app_q3_client_source;
const struct qa_app_q3_client_source *qa_app_local_q3_client_state_source(qa_app_local_q3_client_state *state);
/* LocalQ3ClientState as Q3ClientState (compat/qvm/client-state.ts): generation, sequences, client number, snapshotPing,
 * getServerCommand, gameState (get, copySourceRecord) and commands (currentNumber, read). ctx is the state. */
qa_q3_client_state qa_app_local_q3_client_state_connection(qa_app_local_q3_client_state *state);
#endif
