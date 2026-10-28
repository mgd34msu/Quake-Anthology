/* Port of src/app/bootstrap/simulation/q3/guest-runtime.ts.
 * QvmGame/module/files/parser are host seams (landed compat APIs use
 * divergent checkpoint types); lifecycle/client/bot bookkeeping and
 * host checkpoint encoding live here. All async flows are
 * synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_RUNTIME_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_RUNTIME_H
#include "qa/app/bootstrap/network/q3_types.h"
#include "qa/app/bootstrap/simulation/q3/guest_bots.h"
#include "qa/app/bootstrap/simulation/q3/server_state.h"
#include "qa/compat/qvm/interpreter.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/contracts/execution.h"

typedef struct qa_app_q3_guest_bot_preparation {
  qa_app_q3_guest_bot_options bots; /* library/selected part used */
  void *ctx;
  bool (*create_client)(void *ctx, double slot, qa_client_id *out); /* false = TS null */
  void (*free_client)(void *ctx, qa_client_id client);
  double (*snapshot_entity)(void *ctx, qa_app_q3_player player, double sequence); /* -1 past end */
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

typedef struct qa_app_q3_guest_module {
  void *ctx;
  bool (*is_active)(void *ctx);
  const char *profile_module;
  void *(*checkpoint)(void *ctx);               /* opaque checkpoint handle */
  void (*restore)(void *ctx, void *checkpoint);
  void (*retire)(void *ctx);
  void (*initialize)(void *ctx, double time, double seed, bool restart);
  const char *(*client_connect)(void *ctx, double slot, bool first_time, bool is_bot); /* NULL admitted */
  void (*client_begin)(void *ctx, double slot);
  void (*client_think)(void *ctx, double slot);
  void (*client_userinfo_changed)(void *ctx, double slot);
  void (*client_command)(void *ctx, double slot, const char *const *argv, size_t argc);
  void (*client_disconnect)(void *ctx, double slot);
  bool (*console_command)(void *ctx, const char *const *argv, size_t argc);
  void (*run_frame)(void *ctx, double time);
  void (*bot_start_frame)(void *ctx, double time);
  void (*shutdown)(void *ctx, bool restart);
  /* Host checkpoint bridge: module calls back into runtime. */
  qa_save_value *(*capture_host_bytes)(void *ctx);
  void (*restore_host_bytes)(void *ctx, const qa_save_value *bytes_value);
} qa_app_q3_guest_module;

typedef struct qa_app_q3_qvm_server_game_options {
  qa_app_q3_server_state *state;
  qa_qvm_game_data *data;
  qa_app_q3_guest_record_host records;
  qa_app_q3_guest_module module;
  double max_clients;
  bool has_dedicated;
  bool dedicated;
  double seed;
  const char *entity_text;
  void *ctx;
  double (*now)(void *ctx);
  void (*assert_current)(void *ctx);
  void (*before_disconnect)(void *ctx, qa_actor_id actor);
  void (*before_retire)(void *ctx);
  void (*source_restored)(void *ctx);
  bool (*arsenal)(void *ctx, qa_actor_id actor, qa_arsenal_state *out);
  void (*client_changed)(void *ctx, const char *kind, qa_actor_id actor);
  void (*bot_command)(void *ctx, qa_actor_id actor, qa_q3_base_user_command command);
  /* files/parser seams */
  void *files_ctx;
  qa_save_value *(*files_capture)(void *ctx);
  void (*files_restore)(void *ctx, const qa_save_value *value);
  void (*files_close_all)(void *ctx);
  qa_save_value *(*parser_capture)(void *ctx);
  void (*parser_restore)(void *ctx, const qa_save_value *value);
  /* input binding seam (bindInput is explicit below) */
  void *input_ctx;
  void (*input_close)(void *ctx);
} qa_app_q3_qvm_server_game_options;

typedef struct qa_app_q3_qvm_server_game qa_app_q3_qvm_server_game;
qa_app_q3_qvm_server_game *qa_app_q3_qvm_server_game_create(qa_app_q3_qvm_server_game_options options);
void qa_app_q3_qvm_server_game_destroy(qa_app_q3_qvm_server_game *game);
qa_app_q3_server_state *qa_app_q3_qvm_server_game_state(qa_app_q3_qvm_server_game *game);
qa_app_q3_guest_records *qa_app_q3_qvm_server_game_records(qa_app_q3_qvm_server_game *game);
qa_app_q3_guest_spatial *qa_app_q3_qvm_server_game_spatial(qa_app_q3_qvm_server_game *game);
bool qa_app_q3_qvm_server_game_player_arsenal(qa_app_q3_qvm_server_game *game, qa_actor_id actor, qa_arsenal_state *out);
typedef struct qa_app_q3_mod_client_identity { qa_actor_id actor; qa_client_id client; } qa_app_q3_mod_client_identity;
void qa_app_q3_qvm_server_game_bind_input(qa_app_q3_qvm_server_game *game);
void qa_app_q3_qvm_server_game_attach_bots(qa_app_q3_qvm_server_game *game, qa_app_q3_guest_bot_preparation preparation);
bool qa_app_q3_qvm_server_game_is_retired(const qa_app_q3_qvm_server_game *game);
void *qa_app_q3_qvm_server_game_checkpoint(qa_app_q3_qvm_server_game *game);
qa_save_value *qa_app_q3_qvm_server_game_capture_host(qa_app_q3_qvm_server_game *game);
void qa_app_q3_qvm_server_game_restore_checkpoint(qa_app_q3_qvm_server_game *game, void *checkpoint,
    qa_client_id (*resolve_client)(void *ctx, qa_app_q3_saved_guest_client_id saved), void *resolve_ctx);
void qa_app_q3_qvm_server_game_restore_host(qa_app_q3_qvm_server_game *game, const uint8_t *bytes, size_t length,
    const char *format, size_t callback_count, size_t random_count);
void qa_app_q3_qvm_server_game_complete_restore(qa_app_q3_qvm_server_game *game, qa_app_q3_guest_output output);
void qa_app_q3_qvm_server_game_discard(qa_app_q3_qvm_server_game *game);
double qa_app_q3_qvm_server_game_time_milliseconds(qa_app_q3_qvm_server_game *game);
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
