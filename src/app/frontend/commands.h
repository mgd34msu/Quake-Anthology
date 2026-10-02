#ifndef QA_FRONTEND_COMMANDS_H
#define QA_FRONTEND_COMMANDS_H
#include "internal.h"
#include "qa/application_client.h"
#include "qa/application_startup_prepare.h"
bool frontend_commands_source(qa_frontend *,const qa_application_startup_source *,
    const qa_command_invocation *,bool *,qa_error *);
typedef struct frontend_client_commands frontend_client_commands;
bool frontend_commands_client_bind(qa_frontend *,const qa_application_client_source *,frontend_client_commands **,qa_error *);
bool frontend_commands_client_unbind(frontend_client_commands **,qa_error *);
void frontend_commands_client_rebind(frontend_client_commands *,qa_frontend *);
#endif
