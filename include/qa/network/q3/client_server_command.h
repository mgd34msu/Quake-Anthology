/* Port of src/network/q3/client-server-command.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_CLIENT_SERVER_COMMAND_H
#define QA_NETWORK_Q3_CLIENT_SERVER_COMMAND_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/game_state.h"
typedef void (*qa_q3_scmd_thunk_fn)(void *ctx);
typedef bool (*qa_q3_scmd_bool_fn)(void *ctx);
/* TS Q3ServerCommandBindings (systemInfo is async in TS; called synchronously here). */
typedef struct qa_q3_server_command_bindings {
  qa_q3_scmd_thunk_fn assert_current, system_info, map_restart, level_shot;
  qa_q3_scmd_bool_fn local_server_running;
  void *ctx;
} qa_q3_server_command_bindings;
typedef struct qa_q3_server_command_executor qa_q3_server_command_executor;
bool qa_q3_server_command_executor_init(qa_q3_server_command_executor **out);
void qa_q3_server_command_executor_destroy(qa_q3_server_command_executor *e);
void qa_q3_server_command_executor_clear(qa_q3_server_command_executor *e);
/* execute: false = TS null. On true, argv_out owns strings (qa_command_tokens_destroy shape):
 * caller frees each argv string + argv array. */
bool qa_q3_server_command_executor_execute(qa_q3_server_command_executor *e, const char *command,
                                           qa_q3_client_game_state_storage *game_state,
                                           const qa_q3_server_command_bindings *bindings,
                                           char ***argv_out, size_t *argc_out);
#endif
