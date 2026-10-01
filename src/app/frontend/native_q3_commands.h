#ifndef QA_FRONTEND_NATIVE_Q3_COMMANDS_H
#define QA_FRONTEND_NATIVE_Q3_COMMANDS_H
#include "native_q3_client.h"
typedef struct frontend_native_q3_commands frontend_native_q3_commands;
bool frontend_native_q3_commands_create(frontend_native_q3 *,bool restoring,
    frontend_native_q3_commands **,qa_error *);
bool frontend_native_q3_commands_register(frontend_native_q3_commands *,const q3n_frame *,qa_error *);
bool frontend_native_q3_commands_idle(const frontend_native_q3_commands *);
bool frontend_native_q3_commands_destroy(frontend_native_q3_commands *,qa_error *);
bool frontend_native_q3_commands_checkpoint(const frontend_native_q3_commands *,qa_buffer *,qa_error *);
bool frontend_native_q3_commands_restore(frontend_native_q3_commands *,qa_bytes,qa_error *);
void frontend_native_q3_commands_rebind(frontend_native_q3_commands *,qa_frontend *);
#endif
