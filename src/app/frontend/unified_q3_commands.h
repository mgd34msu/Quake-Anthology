#ifndef QA_FRONTEND_UNIFIED_Q3_COMMANDS_H
#define QA_FRONTEND_UNIFIED_Q3_COMMANDS_H
#include "unified_q3_runtime.h"

typedef struct frontend_unified_q3_commands frontend_unified_q3_commands;
typedef struct frontend_unified_q3_commands_options {
    qa_frontend *frontend;
    frontend_remote_unified *replica;
    frontend_unified_q3_client *client;
    frontend_unified_q3_runtime *runtime;
    void *context;
    bool (*current)(void *,const struct frontend_unified_q3_commands_options *,bool checkpoint);
    /* The real factory routes this exact Source CLIENT, including supplemental
     * instances. A primary-replica channel is not a substitute for that route. */
    bool (*send_client)(void *,frontend_unified_q3_client *,const qa_command_context *,const char *,qa_error *);
} frontend_unified_q3_commands_options;
bool frontend_unified_q3_commands_create(const frontend_unified_q3_commands_options *,frontend_unified_q3_commands **,qa_error *);
bool frontend_unified_q3_commands_register(void *,const q3n_frame *,qa_error *);
bool frontend_unified_q3_commands_registration_ready(frontend_unified_q3_commands *,const q3n_frame *,qa_error *);
bool frontend_unified_q3_commands_client_command(void *,const q3n_frame *,const char *,qa_error *);
qa_command_result frontend_unified_q3_commands_execute(frontend_unified_q3_commands *,const qa_command_invocation *,qa_error *);
bool frontend_unified_q3_commands_idle(const frontend_unified_q3_commands *);
bool frontend_unified_q3_commands_current(const frontend_unified_q3_commands *);
/* Remove the actual registration prefix before fresh CG Init. Partial failure
 * retains the child and remaining rows for checked retry or destruction. */
bool frontend_unified_q3_commands_video_reset(frontend_unified_q3_commands *,qa_error *);
bool frontend_unified_q3_commands_destroy(frontend_unified_q3_commands **,qa_error *);
#endif
