/* Port of src/app/bootstrap/simulation/q3/guest-runtime.ts. TS Q3ServerGame
 * keeps its earlier C name, qa_app_q3_qvm_server_game. The server game owns its
 * Q3GameEngine (an interpreted QVM, q3/qvm_engine.h, or a native process image,
 * q3/native_guest_source.h), QvmFiles, the entity-token parser and the shared
 * trap table (q3/server_traps.h). All async flows are synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_RUNTIME_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_RUNTIME_H
#include "qa/app/bootstrap/network/q3_types.h"
#include "qa/app/bootstrap/simulation/q3/engine.h"
#include "qa/app/bootstrap/simulation/q3/guest_bots.h"
#include "qa/app/bootstrap/simulation/q3/native_guest_source.h"
#include "qa/app/bootstrap/simulation/q3/server_traps.h"
#include "qa/app/bootstrap/simulation/q3/server_state.h"
#include "qa/compat/qvm/common_syscalls.h"
#include "qa/compat/qvm/file_syscalls.h"
#include "qa/compat/qvm/game.h"
#include "qa/compat/qvm/game_input.h"
#include "qa/compat/qvm/interpreter.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/contracts/execution.h"

/* TS Q3GuestBotPreparation. library and selected are borrowed for the bots' lifetime.
 * snapshot_entity is TS snapshotEntities(player)[sequence] ?? -1. Every callback receives ctx. */
typedef struct qa_app_q3_guest_bot_preparation {
  qa_app_q3_guest_bot_library library;
  const qa_app_sim_bot_navigation *selected;
  void *ctx;
  bool (*create_client)(void *ctx, double slot, qa_client_id *out); /* false = TS null */
  void (*free_client)(void *ctx, qa_client_id client);
  double (*snapshot_entity)(void *ctx, qa_app_q3_player player, double sequence);
} qa_app_q3_guest_bot_preparation;

typedef struct qa_app_q3_guest_output {
  void *ctx;
  void (*drop_client)(void *ctx, double slot, const char *reason);
  void (*send_server_command)(void *ctx, double slot, const char *text);
  void (*configstring)(void *ctx, double index, const char *value);
} qa_app_q3_guest_output;

typedef struct qa_app_q3_saved_guest_client_id { double slot, generation; } qa_app_q3_saved_guest_client_id;
typedef struct qa_app_q3_saved_guest_client {
  double source_entity;
  qa_app_q3_saved_guest_client_id client;
  qa_saved_actor_id actor;
  const char *phase;
} qa_app_q3_saved_guest_client;
qa_app_q3_saved_guest_client *qa_app_saved_q3_guest_clients(const uint8_t *host_bytes, size_t host_length,
    const char *host_format, size_t *out_count);

typedef struct qa_app_q3_guest_map_client { qa_client_id client; const char *userinfo; bool bot; } qa_app_q3_guest_map_client;
typedef struct qa_app_q3_guest_map_transition {
  qa_save_value *cvars; /* owned */
  qa_app_q3_guest_map_client *clients; /* malloc'd; strings owned */
  size_t client_count;
} qa_app_q3_guest_map_transition;
void qa_app_q3_guest_map_transition_destroy(qa_app_q3_guest_map_transition *transition);

/* TS Q3GuestModule: which execution form of the qagame this server game hosts. kind "qvm" uses artifact,
 * "native" uses image (borrowed; outlives the game) and the optional instruction budget. */
typedef struct qa_app_q3_guest_module {
  const char *kind;
  const qa_resolved_qvm_artifact *artifact;
  const qa_app_q3_native_image *image;
  bool has_instruction_budget;
  int64_t instruction_budget;
} qa_app_q3_guest_module;

/* TS Q3GuestRuntimeOptions. module, state, mounts, writable and common are
 * borrowed and outlive the game. common supplies milliseconds, real_time and
 * commands (its other members are the game's own). Optional callbacks are NULL
 * when TS leaves them undefined; every callback receives ctx. */
typedef struct qa_app_q3_qvm_server_game_options {
  qa_app_q3_guest_module module;
  qa_app_q3_server_state *state;
  qa_app_q3_guest_record_host records;
  qa_content_mounted *mounts;
  qa_user_file_store *writable;
  double max_clients;
  bool has_dedicated;
  bool dedicated;
  double seed;
  const char *entity_text;
  const qa_qvm_common_services *common;
  void *ctx;
  double (*now)(void *ctx);
  void (*assert_current)(void *ctx);
  void (*before_disconnect)(void *ctx, qa_actor_id actor);
  void (*before_retire)(void *ctx);
  void (*source_restored)(void *ctx);
  bool (*arsenal)(void *ctx, qa_actor_id actor, qa_arsenal_state *out);
  void (*client_changed)(void *ctx, const char *kind, qa_actor_id actor);
  void (*bot_command)(void *ctx, qa_actor_id actor, qa_q3_base_user_command command);
} qa_app_q3_qvm_server_game_options;

typedef struct qa_app_q3_qvm_server_game qa_app_q3_qvm_server_game;
qa_app_q3_qvm_server_game *qa_app_q3_qvm_server_game_create(qa_app_q3_qvm_server_game_options options);
void qa_app_q3_qvm_server_game_destroy(qa_app_q3_qvm_server_game *game);
qa_app_q3_server_state *qa_app_q3_qvm_server_game_state(qa_app_q3_qvm_server_game *game);
qa_app_q3_guest_records *qa_app_q3_qvm_server_game_records(qa_app_q3_qvm_server_game *game);
qa_app_q3_guest_spatial *qa_app_q3_qvm_server_game_spatial(qa_app_q3_qvm_server_game *game);
bool qa_app_q3_qvm_server_game_player_arsenal(qa_app_q3_qvm_server_game *game, qa_actor_id actor, qa_arsenal_state *out);
typedef struct qa_app_q3_mod_client_identity { qa_actor_id actor; qa_client_id client; } qa_app_q3_mod_client_identity;
/* bindInput(definition, services): replaces the input binding. definition is copied. */
void qa_app_q3_qvm_server_game_bind_input(qa_app_q3_qvm_server_game *game, const qa_qvm_input_definition *definition,
    qa_qvm_input_services services);
/* TS Q3ServerGame.qvmGame(): the interpreted module (borrowed until destroy); raises
 * "This Q3 guest runs a native module; interpreted-module bindings do not apply" for a native one. */
qa_qvm_game *qa_app_q3_qvm_server_game_game(qa_app_q3_qvm_server_game *game);
/* TS Q3ServerGame.engine (borrowed until destroy). */
qa_app_q3_game_engine *qa_app_q3_qvm_server_game_engine(qa_app_q3_qvm_server_game *game);
void qa_app_q3_qvm_server_game_attach_bots(qa_app_q3_qvm_server_game *game, qa_app_q3_guest_bot_preparation preparation);
bool qa_app_q3_qvm_server_game_is_retired(const qa_app_q3_qvm_server_game *game);
/* checkpoint(): engine.checkpoint() (a qvm or native-guest checkpoint). The result is owned by the engine and
 * stays valid until the next checkpoint or destroy. */
const qa_guest_checkpoint *qa_app_q3_qvm_server_game_checkpoint(qa_app_q3_qvm_server_game *game);
void qa_app_q3_qvm_server_game_restore_checkpoint(qa_app_q3_qvm_server_game *game, const qa_guest_checkpoint *checkpoint,
    qa_client_id (*resolve_client)(void *ctx, qa_app_q3_saved_guest_client_id saved), void *resolve_ctx);
void qa_app_q3_qvm_server_game_complete_restore(qa_app_q3_qvm_server_game *game, qa_app_q3_guest_output output);
void qa_app_q3_qvm_server_game_discard(qa_app_q3_qvm_server_game *game);
double qa_app_q3_qvm_server_game_time_milliseconds(qa_app_q3_qvm_server_game *game);
/* TS engine.active: the guest module is running an operation. */
bool qa_app_q3_qvm_server_game_engine_active(qa_app_q3_qvm_server_game *game);
void qa_app_q3_qvm_server_game_set_configstring(qa_app_q3_qvm_server_game *game, double index, const char *value);
/* start_kind "new"|"map-change"|"map-restart". */
void qa_app_q3_qvm_server_game_initialize(qa_app_q3_qvm_server_game *game, qa_app_q3_guest_output output,
    const char *start_kind, const qa_app_q3_guest_map_client *clients, size_t client_count);
qa_app_q3_admission qa_app_q3_qvm_server_game_connect(qa_app_q3_qvm_server_game *game, qa_client_id client, const char *userinfo);
qa_app_q3_admission qa_app_q3_qvm_server_game_reconnect(qa_app_q3_qvm_server_game *game, qa_client_id client);
void qa_app_q3_qvm_server_game_begin(qa_app_q3_qvm_server_game *game, qa_app_q3_player player, qa_q3_base_user_command command);
void qa_app_q3_qvm_server_game_think(qa_app_q3_qvm_server_game *game, qa_app_q3_player player, qa_q3_base_user_command command);
void qa_app_q3_qvm_server_game_userinfo(qa_app_q3_qvm_server_game *game, qa_app_q3_player player, const char *value);
void qa_app_q3_qvm_server_game_command(qa_app_q3_qvm_server_game *game, qa_app_q3_player player, const char *const *argv, size_t argc);
void qa_app_q3_qvm_server_game_command_immediate(qa_app_q3_qvm_server_game *game, qa_app_q3_player player, const char *const *argv, size_t argc);
void qa_app_q3_qvm_server_game_disconnect(qa_app_q3_qvm_server_game *game, qa_app_q3_player player);
void qa_app_q3_qvm_server_game_drop(qa_app_q3_qvm_server_game *game, double slot, const char *reason);
bool qa_app_q3_qvm_server_game_console_command(qa_app_q3_qvm_server_game *game, const char *const *argv, size_t argc);
void qa_app_q3_qvm_server_game_run_frame(qa_app_q3_qvm_server_game *game, double time_milliseconds);
bool qa_app_q3_qvm_server_game_is_bot(qa_app_q3_qvm_server_game *game, qa_client_id client);
size_t qa_app_q3_qvm_server_game_players(qa_app_q3_qvm_server_game *game, qa_app_q3_player *out, size_t capacity);
bool qa_app_q3_qvm_server_game_player(qa_app_q3_qvm_server_game *game, qa_client_id client, qa_app_q3_player *out);
void qa_app_q3_qvm_server_game_shutdown(qa_app_q3_qvm_server_game *game);
qa_app_q3_guest_map_transition qa_app_q3_qvm_server_game_shutdown_for_map_change(qa_app_q3_qvm_server_game *game, bool restart);
#endif
