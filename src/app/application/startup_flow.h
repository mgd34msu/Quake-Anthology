#ifndef QA_APPLICATION_STARTUP_FLOW_H
#define QA_APPLICATION_STARTUP_FLOW_H
#include "internal.h"
#include "qa/application_startup_prepare.h"

bool application_startup_flow_begin(qa_application *, const qa_launch_draft *, qa_error *);
application_provider *application_startup_flow_provider(const qa_application *, uint64_t);
/* Move the real detached ticket into ordinary configuration validation. */
application_publication *application_startup_flow_take_publication(qa_application *,
    const qa_launch_snapshot *, const qa_launch_snapshot *);
void application_startup_flow_discard_candidate(qa_application *, const qa_launch_snapshot *);
bool application_startup_script_read(application_provider *, const qa_command_context *,
    const char *, qa_bytes *, void **, qa_error *);
void application_startup_script_release(application_provider *, void *);
void application_startup_script_complete(application_provider *, const qa_command_context *,
    const char *, bool);
bool application_startup_source_active(const application_provider *);
bool application_startup_command_allowed(application_provider *, const qa_command_invocation *);
bool application_startup_source_preinit(application_provider *, qa_console *, qa_cvars *,
    const qa_command_context *, qa_error *);
bool application_startup_source_restore(application_provider *, qa_console *, qa_cvars *, qa_error *);
bool application_startup_source_retire(application_provider *, qa_console *, qa_cvars *, qa_error *);
qa_cvars *application_startup_cvar_owner(application_provider *, qa_console *,
    const qa_command_context *, const char *);
bool application_startup_visible_cvars(application_provider *, qa_console *,
    const qa_command_context *, size_t, qa_cvars **);
bool application_provider_console_prepare(qa_application *, application_provider *, qa_world *,
    qa_catalog *, const qa_product *, const qa_launch_choices *, qa_console **,
    qa_cvars **, qa_command_context *, qa_error *);
bool application_publication_begin(qa_application *, const qa_launch_snapshot *,
    const qa_launch_snapshot *, application_publication **, qa_error *);
bool application_publication_finish(qa_application *, application_publication *, qa_error *);
#endif
