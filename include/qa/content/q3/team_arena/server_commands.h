/* Port of src/content/q3/team-arena/server-commands.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_TEAM_ARENA_SERVER_COMMANDS_H
#define QA_CONTENT_Q3_TEAM_ARENA_SERVER_COMMANDS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/core/cvars/index.h"
#include "qa/persistence/value.h"

enum { QA_Q3_TA_MAX_IP_FILTERS = 1024 };
#define QA_Q3_TA_FREE_FILTER 0xffffffffu

typedef struct qa_q3_ta_ip_filter { uint32_t mask, compare; } qa_q3_ta_ip_filter;

/* TS ServerCommandCapability union. reason borrowed. */
typedef enum qa_q3_ta_server_capability_kind { QA_Q3_TA_CAP_AVAILABLE, QA_Q3_TA_CAP_UNAVAILABLE } qa_q3_ta_server_capability_kind;
typedef struct qa_q3_ta_server_command_capability {
  qa_q3_ta_server_capability_kind kind;
  const char *reason;
  void *ctx;
  void (*run)(void *ctx, const char *const *argv, size_t argc);
} qa_q3_ta_server_command_capability;

/* TS GameServerCommandHost. read_vm_cvar fills an owned snapshot the
 * caller frees with qa_cvar_read_free; name is a ServerCommandCvar. */
typedef struct qa_q3_ta_game_server_command_host {
  void *ctx;
  qa_cvar_snapshot (*read_vm_cvar)(void *ctx, const char *name);
  void (*print)(void *ctx, const char *text);
  void (*send_server_command)(void *ctx, double client_number, const char *command);
  void (*execute_console_now)(void *ctx, const char *text);
  void (*set_team)(void *ctx, qa_q3_game_entity *entity, const char *team);
  qa_q3_ta_server_command_capability bots, memory, podium;
} qa_q3_ta_game_server_command_host;

/* TS GameServerCommandState. filters is a caller-sized dynamic array. */
typedef struct qa_q3_ta_game_server_command_state {
  qa_q3_ta_ip_filter *filters; size_t filter_count, filter_capacity;
} qa_q3_ta_game_server_command_state;
void qa_q3_ta_game_server_command_state_init(qa_q3_ta_game_server_command_state *state);
void qa_q3_ta_game_server_command_state_destroy(qa_q3_ta_game_server_command_state *state);

/* TS captureSaveState() result shape. filters malloc'd (caller frees
 * via destroy); ban value malloc'd or NULL for TS null. */
typedef struct qa_q3_ta_server_command_save_state {
  qa_q3_ta_ip_filter *filters; size_t filter_count;
  bool has_ban_vm_string; double ban_modification_count; char *ban_value;
} qa_q3_ta_server_command_save_state;
void qa_q3_ta_server_command_save_state_destroy(qa_q3_ta_server_command_save_state *state);

typedef struct qa_q3_ta_game_server_command_runtime {
  qa_q3_entity_pool *pool;
  qa_cvar_registry *cvars;
  qa_q3_ta_game_server_command_host host;
  qa_q3_ta_game_server_command_state *state;
  qa_q3_ta_game_server_command_state owned_state;
  bool has_ban_vm_string; double ban_modification_count; char *ban_value;
} qa_q3_ta_game_server_command_runtime;
void qa_q3_ta_game_server_command_runtime_init(qa_q3_ta_game_server_command_runtime *runtime,
    qa_q3_entity_pool *pool, qa_cvar_registry *cvars, qa_q3_ta_game_server_command_host host,
    qa_q3_ta_game_server_command_state *state);
void qa_q3_ta_game_server_command_runtime_destroy(qa_q3_ta_game_server_command_runtime *runtime);
qa_q3_ta_server_command_save_state qa_q3_ta_game_server_command_runtime_capture(qa_q3_ta_game_server_command_runtime *runtime);
void qa_q3_ta_game_server_command_runtime_restore(qa_q3_ta_game_server_command_runtime *runtime, const qa_save_value *value);
void qa_q3_ta_game_server_command_runtime_process_ip_bans(qa_q3_ta_game_server_command_runtime *runtime);
bool qa_q3_ta_game_server_command_runtime_filter_packet(qa_q3_ta_game_server_command_runtime *runtime, const char *from);
qa_q3_game_client *qa_q3_ta_game_server_command_runtime_client_for_string(qa_q3_ta_game_server_command_runtime *runtime, const char *value);
bool qa_q3_ta_game_server_command_runtime_console_command(qa_q3_ta_game_server_command_runtime *runtime,
    const char *const *argv, size_t argc);
#endif
