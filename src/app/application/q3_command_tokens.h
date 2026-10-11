#ifndef QA_APPLICATION_Q3_COMMAND_TOKENS_H
#define QA_APPLICATION_Q3_COMMAND_TOKENS_H

#include "qa/console.h"
#include "qa/network_q3.h"
#include "qa/application_q3_client.h"

typedef struct application_q3_command_storage {
    qa_q3_tokens tokens;
    char *values[1024];
    char args[9216];
} application_q3_command_storage;

bool application_q3_command_parse(application_q3_command_storage *storage,
    const char *text, qa_command_tokens *arguments, qa_error *error);

bool application_q3_command_bind(application_q3_command_storage *,qa_command_tokens *,qa_error *);
bool application_q3_server_command_deliver(application_q3_command_storage *,qa_command_tokens *,
    const qa_q3_server_command_result *,void *,
    bool (*effect)(void *,qa_application_q3_client_effect,const char *,qa_error *),qa_error *);
bool application_q3_client_effect_state_update(qa_application_q3_client_effect,const char *,
    int32_t *,qa_q3_usercmd *,size_t,qa_error *);
#endif
