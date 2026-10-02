#ifndef QA_NATIVE_Q3_CLIENT_MODULES_PRIVATE_H
#define QA_NATIVE_Q3_CLIENT_MODULES_PRIVATE_H

#include "native_q3_client_modules.h"
#include "native_q3_remote_role.h"
#include "guest_q3_equipment.h"
#include "guest_q3_body.h"
#include "qa/native_host.h"
#include "native_process_owner.h"

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
    native_client_opening artifact, declaration, body_declaration;
    qa_qvm_image *image;
    qa_native_module *module;
    qa_native_declaration *native_declaration;
    application_q3_equipment_profile profile;
    application_q3_body_profile body_profile;
    qa_q3_host *host;
    qa_qvm *vm;
    qa_native_host *native;
    application_native_process_owner process;
    application_q3_equipment *equipment;
    application_q3_body *body;
    qa_application_q3_body_services body_services;
    qa_q3_host_client_services client;
    qa_buffer saved_services;
    int32_t draw_arguments[3];
    uint64_t draw_revision;
    bool draw_entry;
    bool ready, initialized, init_succeeded, native_load_failed;
} native_client_module;

struct application_native_q3_client_modules {
    qa_application *app;
    application_provider *provider;
    qa_launch_instance_lease *metadata;
    qa_application_q3_remote_source source;
    qa_application_native_q3_client_modules_options options;
    qa_script_defines *script_globals;
    native_client_module ui, cgame;
    native_client_module *initializing;
    native_client_module *entered;
    const native_client_module *command_role;
    const qa_command_tokens *command_arguments;
    uint64_t command_revision;
    size_t calls;
    struct qa_application_native_q3_client_modules_video *video;
    bool video_entering;
    bool attached, prepared, pure, retiring, restore_pending;
    qa_buffer saved;
};

bool native_client_modules_physical(const application_native_q3_client_modules *,
    qa_application_q3_remote_source *, qa_error *);
bool native_client_modules_executors_idle(const application_native_q3_client_modules *);
bool native_client_module_close_executor(native_client_module *, qa_error *);
bool native_client_modules_policy(const qa_q3_gamestate *, bool, qa_error *);
bool native_client_module_construct(native_client_module *, bool restoring, qa_error *);
bool native_client_module_namespace(native_client_module *, bool restoring, qa_error *);
bool native_client_modules_allocate(qa_application *,
    const qa_application_native_q3_client_modules_options *, bool restoring,
    application_native_q3_client_modules **, qa_error *);
bool native_client_module_qualify(native_client_module *, qa_error *);
void native_client_opening_dispose(native_client_opening *);
bool native_client_module_body_open(native_client_module *, qa_error *);
bool native_client_module_functions_checkpoint(native_client_module *, qa_buffer *, qa_error *);
bool native_client_module_functions_restore(native_client_module *, qa_bytes, qa_bytes, qa_error *);

#endif
