#ifndef QA_APPLICATION_NATIVE_Q3_REMOTE_ROLE_PRIVATE_H
#define QA_APPLICATION_NATIVE_Q3_REMOTE_ROLE_PRIVATE_H
#include "native_q3_remote_role.h"

typedef enum native_q3_remote_lifecycle {
    NATIVE_Q3_REMOTE_COLD,
    NATIVE_Q3_REMOTE_ATTACHED,
    NATIVE_Q3_REMOTE_CLEARED
} native_q3_remote_lifecycle;

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
    qa_native_q3_remote_client_transport *transport;
    application_native_q3_client_modules *modules;
    qa_command_tokens arguments;
    qa_buffer modules_restore;
    char *system_info;
    uint64_t argument_revision, module_sequence, module_generation;
    size_t calls, module_calls;
    native_q3_remote_lifecycle lifecycle;
    bool owns_cvars, initialized, acquired_initialized, retiring;
};
#endif
