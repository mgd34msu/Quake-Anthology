#ifndef QA_APPLICATION_NATIVE_PROCESS_OWNER_H
#define QA_APPLICATION_NATIVE_PROCESS_OWNER_H
#include "internal.h"

typedef struct application_native_process_owner {
    qa_native_process_platform *platform;
    qa_native_process_resources *resources;
    qa_native_process_options process;
} application_native_process_owner;

bool application_native_process_prepare(qa_application *, const qa_launch_instance *,
    qa_actor_owner, uint64_t, const qa_native_process_resource_artifact *, size_t,
    size_t, const qa_native_image_info *, bool,
    bool (*)(void *, const qa_launch_instance *, qa_actor_owner, uint64_t, qa_error *),
    void *, const qa_native_process_resources *, qa_bytes, application_native_process_owner *, qa_error *);
bool application_native_process_release(application_native_process_owner *, qa_error *);
#endif
