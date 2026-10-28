/* Port of src/app/bootstrap/simulation/q3/server-state.ts.
 * registerQ3ServerCvars re-export is landed qa_app_register_q3_server_cvars.
 * WireUserCommand is landed qa_q3_base_user_command angles as vec3.
 * Strings stored are owned copies. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_SERVER_STATE_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_SERVER_STATE_H
#include "qa/app/bootstrap/q3_common_cvars.h"
#include "qa/app/bootstrap/simulation/q3/types.h"
#include "qa/persistence/value.h"

typedef struct qa_app_q3_host_cvar { const char *name, *value; } qa_app_q3_host_cvar;
typedef struct qa_app_q3_host_settings {
  double game_type;
  bool single_player;
  double max_clients;
  const char *map_name;
  qa_cvar_registry *source_registry; /* borrowed, NULL absent */
  const qa_cvar_archive_entry *source_archive;
  size_t source_archive_count;
  const qa_app_q3_host_cvar *cvars;
  size_t cvar_count;
} qa_app_q3_host_settings;

typedef qa_q3_base_user_command qa_app_q3_wire_user_command;
typedef struct qa_app_q3_server_state_options {
  qa_session_id session;
  qa_app_q3_host_settings settings;
  void *ctx;
  qa_app_q3_wire_user_command (*filter_user_command)(void *ctx, double slot, qa_app_q3_wire_user_command value); /* NULL absent */
  double (*now)(void *ctx);
  void (*print)(void *ctx, const char *text);
} qa_app_q3_server_state_options;

struct qa_app_q3_server_state;
typedef struct qa_app_q3_server_state qa_app_q3_server_state;
qa_app_q3_server_state *qa_app_q3_server_state_create(qa_app_q3_server_state_options options);
void qa_app_q3_server_state_destroy(qa_app_q3_server_state *state);
qa_cvar_registry *qa_app_q3_server_state_cvars(qa_app_q3_server_state *state);
/* Owned tree; NULL only on OOM. */
qa_save_value *qa_app_q3_server_state_capture_save_state(qa_app_q3_server_state *state);
void qa_app_q3_server_state_restore_save_state(qa_app_q3_server_state *state, const qa_save_value *value);
/* NULL when unchanged; otherwise borrowed stored string. */
const char *qa_app_q3_server_state_refresh_server_info(qa_app_q3_server_state *state);
char *qa_app_q3_server_state_server_info(qa_app_q3_server_state *state); /* malloc'd */
const char *qa_app_q3_server_state_configstring_get(qa_app_q3_server_state *state, double index);
void qa_app_q3_server_state_configstring_set(qa_app_q3_server_state *state, double index, const char *value);
bool qa_app_q3_server_state_has_configstring(qa_app_q3_server_state *state, double index);
void qa_app_q3_server_state_print(qa_app_q3_server_state *state, const char *text);
/* NULL = TS undefined. */
const char *qa_app_q3_server_state_get_userinfo(qa_app_q3_server_state *state, double slot);
void qa_app_q3_server_state_set_userinfo(qa_app_q3_server_state *state, double slot, const char *value);
bool qa_app_q3_server_state_get_user_command(qa_app_q3_server_state *state, double slot, qa_app_q3_wire_user_command *out);
void qa_app_q3_server_state_set_user_command(qa_app_q3_server_state *state, double slot, qa_app_q3_wire_user_command value);
void qa_app_q3_server_state_clear_client(qa_app_q3_server_state *state, double slot);
#endif
