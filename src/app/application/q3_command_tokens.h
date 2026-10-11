#ifndef QA_APPLICATION_Q3_COMMAND_TOKENS_H
#define QA_APPLICATION_Q3_COMMAND_TOKENS_H

#include "qa/console.h"
#include "qa/network_q3.h"

typedef struct application_q3_command_storage {
    qa_q3_tokens tokens;
    char *values[1024];
    char args[9216];
} application_q3_command_storage;

bool application_q3_command_parse(application_q3_command_storage *storage,
    const char *text, qa_command_tokens *arguments, qa_error *error);

#endif
