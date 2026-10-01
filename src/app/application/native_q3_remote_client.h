#ifndef QA_APPLICATION_NATIVE_Q3_REMOTE_CLIENT_PRIVATE_H
#define QA_APPLICATION_NATIVE_Q3_REMOTE_CLIENT_PRIVATE_H
#include "qa/application_native_q3_remote_client.h"
#include "native_q3_client.h"
#include "native_q3_remote_role.h"

struct qa_native_q3_remote_client_service {
    qa_native_q3_remote_client_services services;
    qa_native_q3_character_selection character;
    qa_launch_instance_lease *descriptor;
    application_provider *provider;
    qa_native_q3_client_cvar cache[QA_NATIVE_CLIENT_CVARS];
    uint64_t cache_revision, force_model_count, overlay_count;
    int32_t local_server;
    char *system_info;
    size_t actions;
    bool registered, updating, overlay_initial, attached, retiring;
};
bool native_remote_client_allocate(qa_native_q3_remote_client_services *,
    qa_native_q3_character_selection *, qa_native_q3_remote_client_service **, qa_error *);
bool native_remote_client_commit(qa_native_q3_remote_client_service *, qa_error *);
size_t native_remote_client_symbol(const qa_native_q3_remote_client_service *, const char *);
#endif
