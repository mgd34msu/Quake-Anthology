/* Port of src/app/bootstrap/q1-client-commands.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q1_CLIENT_COMMANDS_H
#define QA_APP_BOOTSTRAP_Q1_CLIENT_COMMANDS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/common.h"
#include "qa/contracts/identity.h"
#include "qa/core/commands/index.h"
typedef void (*qa_app_execute_q1_client_command_fn)(void *ctx, const char *name,
    const char *const *args, size_t arg_count, const qa_seat_id *seat, qa_command_context source);
typedef struct qa_app_q1_client_commands qa_app_q1_client_commands;
qa_app_q1_client_commands *qa_app_register_q1_client_commands(qa_command_buffer *commands,
    qa_command_dialect dialect, qa_app_execute_q1_client_command_fn execute, void *execute_ctx);
void qa_app_q1_client_commands_release(qa_app_q1_client_commands *registration);
typedef struct qa_app_q1_host_player { qa_actor_id actor; qa_client_id client; } qa_app_q1_host_player;
typedef qa_actor_id (*qa_app_local_actor_fn)(void *ctx);
/* TS resolveQ1HostCommandActor. source NULL = TS undefined. Aborts
 * with the TS Error text on misuse. */
qa_actor_id qa_app_resolve_q1_host_command_actor(const char *name,
    const char *const *args, size_t arg_count, const qa_command_context *source,
    const qa_app_q1_host_player *players, size_t player_count,
    qa_app_local_actor_fn local_actor, void *local_actor_ctx, bool takes_arguments);
#endif
