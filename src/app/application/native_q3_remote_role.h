#ifndef QA_APPLICATION_NATIVE_Q3_REMOTE_ROLE_H
#define QA_APPLICATION_NATIVE_Q3_REMOTE_ROLE_H
#include "internal.h"
#include "qa/application_q3_client.h"
#include "qa/application_startup_prepare.h"
#include "qa/application_q3_factory.h"

typedef struct qa_native_q3_remote_client_service qa_native_q3_remote_client_service;

bool application_native_q3_remote_roles_prepare(application_provider *, const qa_launch_choices *, qa_error *);
bool application_native_q3_remote_roles_preinit(application_provider *, qa_error *);
bool application_native_q3_remote_role_source_at(application_provider *, size_t,
    qa_application_startup_source *, bool *, qa_error *);
bool application_native_q3_remote_role_configuration(application_provider *, uint32_t,
    qa_application_startup_source *, qa_error *);
bool application_native_q3_remote_role_take(application_provider *, uint32_t, qa_cvars **, qa_error *);
bool application_native_q3_remote_role_bind(application_provider *, uint32_t, qa_cvars *, qa_error *);
bool application_native_q3_remote_role_context(application_provider *, uint32_t,
    qa_application_q3_client_context *, qa_error *);
bool application_native_q3_remote_role_current(application_provider *, const qa_application_q3_client_context *);
bool application_native_q3_remote_role_attach(application_provider *, uint32_t,
    qa_native_q3_remote_client_service *, qa_error *);
bool application_native_q3_remote_role_initialized(application_provider *, uint32_t,
    qa_native_q3_remote_client_service *, qa_error *);
bool application_native_q3_remote_role_detach(application_provider *, uint32_t,
    qa_native_q3_remote_client_service *, qa_error *);
bool application_native_q3_remote_role_command(application_provider *, uint32_t,
    const qa_q3_tokens *, qa_error *);
bool application_native_q3_remote_role_arguments(application_provider *, uint32_t,
    const qa_command_tokens **, uint64_t *, qa_error *);
bool application_native_q3_remote_role_system_info(application_provider *, uint32_t, const char *, qa_error *);
bool application_native_q3_remote_role_product(application_provider *, uint32_t, qa_q3_product *, qa_error *);
bool application_native_q3_remote_role_source_read(application_provider *, uint32_t, uint64_t,
    qa_application_q3_remote_source *, qa_error *);
bool application_native_q3_remote_role_source_current(application_provider *, const qa_application_q3_remote_source *);
bool application_native_q3_remote_role_descriptor_bind(application_provider *, uint32_t,
    const qa_launch_instance *, uint64_t epoch, uint64_t generation, qa_error *);
bool application_native_q3_remote_roles_idle(const application_provider *);
bool application_native_q3_remote_roles_destroy(application_provider *, qa_error *);
/* Secondary compiled gameplay selections retain their real GAME kernel. */
bool application_native_q3_remote_client_only(const application_provider *);
#endif
