#ifndef QA_FRONTEND_COMMANDS_H
#define QA_FRONTEND_COMMANDS_H
#include "internal.h"
#include "qa/application_client.h"
#include "qa/application_startup_prepare.h"
bool frontend_commands_source(qa_frontend *,const qa_application_startup_source *,
    const qa_command_invocation *,bool *,qa_error *);
bool frontend_commands_menus_pump(qa_frontend *,qa_error *);
#endif
