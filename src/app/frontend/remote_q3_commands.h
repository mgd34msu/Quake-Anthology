#ifndef QA_FRONTEND_REMOTE_Q3_COMMANDS_H
#define QA_FRONTEND_REMOTE_Q3_COMMANDS_H

#include "remote_q3_client.h"

typedef struct frontend_remote_q3_commands frontend_remote_q3_commands;
/* Runtime retains this empty command continuation before CG_Init. Actual
 * Q3N_INIT_CONSOLE_COMMANDS registers names against the CLIENT receiver. */
bool frontend_remote_q3_commands_create(frontend_remote_q3_runtime *,
    frontend_remote_q3_commands **,qa_error *);
/* Pure empty-child construction requires the actual restoring frontend,
 * attached runtime and qualified empty backend/registry candidate. */
bool frontend_remote_q3_commands_create_restored(frontend_remote_q3_runtime *,
    frontend_remote_q3_commands **,qa_error *);
bool frontend_remote_q3_commands_register(frontend_remote_q3_commands *,qa_error *);
/* Actual source-command callback: local CG commands borrow the lexical
 * CLIENT frame; forwarded and unknown commands remain engine fallbacks. */
qa_command_result frontend_remote_q3_commands_execute(frontend_remote_q3_commands *,
    const qa_command_invocation *,qa_error *);
bool frontend_remote_q3_commands_idle(const frontend_remote_q3_commands *);
bool frontend_remote_q3_commands_destroy(frontend_remote_q3_commands **,qa_error *);
bool frontend_remote_q3_commands_checkpoint(const frontend_remote_q3_commands *,qa_buffer *,qa_error *);
/* Imports command receipts without replaying Init or command dispatch. */
bool frontend_remote_q3_commands_restore(frontend_remote_q3_commands *,qa_bytes,qa_error *);

#endif
