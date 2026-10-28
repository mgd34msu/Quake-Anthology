/* Port of src/app/bootstrap/q2-client-commands.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q2_CLIENT_COMMANDS_H
#define QA_APP_BOOTSTRAP_Q2_CLIENT_COMMANDS_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/contracts/common.h"
#include "qa/core/commands/index.h"
/* TS ExecuteQ2ClientCommand. seat NULL = TS null. */
typedef void (*qa_app_execute_q2_client_command_fn)(void *ctx, const char *name,
    const char *const *args, size_t arg_count, const qa_seat_id *seat, qa_command_context source);
typedef struct qa_app_q2_client_commands qa_app_q2_client_commands;
/* Returns the TS release closure as an owner; release unregisters. */
qa_app_q2_client_commands *qa_app_register_q2_client_commands(qa_command_buffer *commands,
    qa_command_dialect dialect, qa_app_execute_q2_client_command_fn execute, void *execute_ctx);
void qa_app_q2_client_commands_release(qa_app_q2_client_commands *registration);
#endif
