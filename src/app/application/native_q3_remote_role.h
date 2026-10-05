#ifndef QA_APPLICATION_NATIVE_Q3_REMOTE_ROLE_H
#define QA_APPLICATION_NATIVE_Q3_REMOTE_ROLE_H
#include "internal.h"
#include "qa/application_q3_client.h"
#include "qa/application_startup_prepare.h"
#include "qa/application_q3_factory.h"

typedef struct qa_native_q3_remote_client_service qa_native_q3_remote_client_service;
typedef struct qa_native_q3_remote_client_transport qa_native_q3_remote_client_transport;
typedef struct application_native_q3_client_modules application_native_q3_client_modules;

bool application_native_q3_remote_roles_prepare(application_provider *, const qa_launch_choices *, qa_error *);
bool application_native_q3_remote_role_selected(const qa_launch_instance *, const qa_launch_choices *, uint32_t seat);
/* Call only after the physical services for omitted seats have returned. */
bool application_native_q3_remote_roles_retain(application_provider *, const qa_launch_choices *, qa_error *);
bool application_native_q3_remote_role_retirement(application_provider *, const qa_application_startup_source *);
bool application_native_q3_remote_roles_preinit(application_provider *, qa_error *);
bool application_native_q3_remote_role_source_at(application_provider *, size_t,
    qa_application_startup_source *, bool *, qa_error *);
bool application_native_q3_remote_role_configuration(application_provider *, uint32_t,
    qa_application_startup_source *, qa_error *);
bool application_native_q3_remote_role_take(application_provider *, uint32_t, qa_cvars **, qa_error *);
bool application_native_q3_remote_role_unborrowed(application_provider *,uint32_t);
bool application_native_q3_remote_role_bind(application_provider *, uint32_t, qa_cvars *, qa_error *);
bool application_native_q3_remote_role_context(application_provider *, uint32_t,
    qa_application_q3_client_context *, qa_error *);
bool application_native_q3_remote_role_current(application_provider *, const qa_application_q3_client_context *);
bool application_native_q3_remote_role_attach(application_provider *, uint32_t,
    qa_native_q3_remote_client_service *, qa_error *);
bool application_native_q3_remote_role_service_read(application_provider *, uint32_t,
    qa_native_q3_remote_client_service **, qa_error *);
bool application_native_q3_remote_role_transport_attach(application_provider *,
    const qa_application_q3_remote_source *, qa_native_q3_remote_client_transport *, qa_error *);
bool application_native_q3_remote_role_transport_current(application_provider *, uint32_t,
    const qa_native_q3_remote_client_transport *);
bool application_native_q3_remote_role_transport_detach_ready(application_provider *, uint32_t,
    const qa_native_q3_remote_client_transport *, qa_error *);
bool application_native_q3_remote_role_transport_detach(application_provider *, uint32_t,
    const qa_native_q3_remote_client_transport *, qa_error *);
bool application_native_q3_remote_role_initialized(application_provider *, uint32_t,
    qa_native_q3_remote_client_service *, qa_error *);
bool application_native_q3_remote_role_video_reset(application_provider *,uint32_t,
    qa_native_q3_remote_client_service *,qa_error *);
bool application_native_q3_remote_role_detach(application_provider *, uint32_t,
    qa_native_q3_remote_client_service *, qa_error *);
bool application_native_q3_remote_role_detach_ready(application_provider *, uint32_t,
    const qa_native_q3_remote_client_service *, qa_error *);
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
bool application_native_q3_remote_role_modules_attach(application_provider *,
    const qa_application_q3_remote_source *, application_native_q3_client_modules *, qa_error *);
bool application_native_q3_remote_role_modules_read(application_provider *,
    const qa_application_q3_remote_source *, application_native_q3_client_modules **, qa_error *);
bool application_native_q3_remote_role_modules_pointer_read(application_provider *, uint32_t,
    application_native_q3_client_modules **, qa_error *);
bool application_native_q3_remote_role_modules_source_read(application_provider *, uint32_t,
    qa_application_q3_remote_source *, application_native_q3_client_modules **, qa_error *);
bool application_native_q3_remote_role_modules_current(application_provider *,
    const qa_application_q3_remote_source *, const application_native_q3_client_modules *);
/* Acquired CGAME completion has its own actual receipt; it does not mark the
 * compiled service or QNRC continuation initialized. */
bool application_native_q3_remote_role_modules_initialized(application_provider *,
    const qa_application_q3_remote_source *, const application_native_q3_client_modules *, qa_error *);
bool application_native_q3_remote_role_modules_initialized_read(application_provider *,
    const qa_application_q3_remote_source *, const application_native_q3_client_modules *, bool *, qa_error *);
/* Physical attachment proof for the module owner's entered teardown loan. */
bool application_native_q3_remote_role_modules_retained(application_provider *,
    const qa_application_q3_remote_source *, const application_native_q3_client_modules *);
bool application_native_q3_remote_role_modules_borrow(application_provider *,
    const qa_application_q3_remote_source *, const application_native_q3_client_modules *, qa_error *);
bool application_native_q3_remote_role_modules_return(application_provider *, uint32_t,
    const application_native_q3_client_modules *, qa_error *);
bool application_native_q3_remote_role_modules_detach(application_provider *, uint32_t,
    const application_native_q3_client_modules *, qa_error *);
bool application_native_q3_remote_role_module_sequence_read(application_provider *,
    const qa_application_q3_remote_source *, uint64_t *, qa_error *);
bool application_native_q3_remote_role_module_sequence_reserve(application_provider *,
    const qa_application_q3_remote_source *, const application_native_q3_client_modules *, uint64_t *, qa_error *);
bool application_native_q3_remote_roles_idle(const application_provider *);
bool application_native_q3_remote_roles_destroy(application_provider *, qa_error *);
/* Secondary compiled gameplay selections retain their real GAME kernel. */
bool application_native_q3_remote_client_only(const application_provider *);
#endif
