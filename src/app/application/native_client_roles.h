#ifndef QA_APPLICATION_NATIVE_CLIENT_ROLES_H
#define QA_APPLICATION_NATIVE_CLIENT_ROLES_H
#include "internal.h"
#include "qa/application_client.h"
bool application_native_client_only(const application_provider *);
bool application_native_client_source_associated(const qa_application *,const qa_application_client_source *);
bool application_native_client_role_source_at(application_provider *, size_t,
    qa_application_startup_source *, bool *, qa_error *);
bool application_native_client_role_configuration(application_provider *, uint32_t,
    qa_application_startup_source *, qa_error *);
bool application_native_client_roles_idle(const application_provider *);
bool application_native_client_roles_destroy(application_provider *, qa_error *);
bool application_native_client_console_scope(const application_provider *, const qa_console *,
    qa_application_console_scope *);
#endif
