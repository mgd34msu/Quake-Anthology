#ifndef QA_NATIVE_Q3_CLIENT_MODULES_PRIVATE_H
#define QA_NATIVE_Q3_CLIENT_MODULES_PRIVATE_H

#include "native_q3_client_modules.h"
#include "native_q3_remote_role.h"
#include "guest_q3_equipment.h"
#include "qa/native_host.h"

typedef struct native_client_opening {
    char *path;
    qa_resource *resource;
    qa_vfs_acquisition acquisition;
} native_client_opening;

typedef struct native_client_module {
    application_native_q3_client_modules *owner;
    qa_qvm_role kind;
    qa_qvm_abi abi;
    uint64_t sequence;
    qa_string_id service_owner;
    native_client_opening artifact, declaration;
    qa_qvm_image *image;
    qa_native_module *module;
    qa_native_declaration *native_declaration;
    application_q3_equipment_profile profile;
    qa_q3_host *host;
    qa_qvm *vm;
    qa_native_host *native;
    application_q3_equipment *equipment;
    qa_q3_host_client_services client;
    qa_buffer saved_services;
    bool ready, initialized, init_succeeded, native_load_failed;
} native_client_module;

struct application_native_q3_client_modules {
    qa_application *app;
    application_provider *provider;
    qa_launch_instance_lease *metadata;
    qa_application_q3_remote_source source;
    qa_application_native_q3_client_modules_options options;
    native_client_module ui, cgame;
    native_client_module *initializing;
    native_client_module *entered;
    size_t calls;
    bool attached, pure, retiring, restore_pending;
    qa_buffer saved;
};

bool native_client_modules_physical(const application_native_q3_client_modules *,
    qa_application_q3_remote_source *, qa_error *);
bool native_client_modules_policy(const qa_q3_gamestate *, bool, qa_error *);
bool native_client_module_construct(native_client_module *, bool restoring, qa_error *);
bool native_client_module_namespace(native_client_module *, bool restoring, qa_error *);
bool native_client_modules_allocate(qa_application *,
    const qa_application_native_q3_client_modules_options *, bool restoring,
    application_native_q3_client_modules **, qa_error *);
bool native_client_module_qualify(native_client_module *, qa_error *);
void native_client_opening_dispose(native_client_opening *);

#endif
