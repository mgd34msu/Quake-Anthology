#ifndef QA_APPLICATION_NATIVE_Q3_REMOTE_ROLE_PRIVATE_H
#define QA_APPLICATION_NATIVE_Q3_REMOTE_ROLE_PRIVATE_H
#include "native_q3_remote_role.h"

struct application_native_q3_remote_role {
    struct application_native_q3_remote_role *next;
    application_provider *provider;
    uint32_t seat;
    qa_console *console;
    qa_cvars *cvars;
    qa_string_id service_owner;
    qa_launch_instance_lease *descriptor;
    uint64_t connection_epoch, configuration_generation;
    qa_native_q3_remote_client_service *service;
    application_native_q3_client_modules *modules;
    qa_command_tokens arguments;
    char *system_info;
    uint64_t argument_revision, module_sequence;
    size_t calls, module_calls;
    bool owns_cvars, initialized, retiring;
};
#endif
