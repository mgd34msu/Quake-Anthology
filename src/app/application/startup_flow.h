#ifndef QA_APPLICATION_STARTUP_FLOW_H
#define QA_APPLICATION_STARTUP_FLOW_H
#include "internal.h"
#include "qa/application_startup_prepare.h"

bool application_startup_flow_begin(qa_application *, const qa_launch_draft *, qa_error *);
bool application_startup_flow_begin_replacing(qa_application *, const qa_launch_draft *, qa_error *);
application_provider *application_startup_flow_provider(const qa_application *, uint64_t);
/* Move the real detached ticket into ordinary configuration validation. */
application_publication *application_startup_flow_take_publication(qa_application *,
    const qa_launch_snapshot *, const qa_launch_snapshot *);
void application_startup_flow_discard_candidate(qa_application *, const qa_launch_snapshot *);
bool application_startup_flow_configuration_idle(const qa_application *);
bool application_startup_flow_retirement_ready(const qa_application *, const qa_launch_snapshot *,
    const qa_cvars_edit *, qa_error *);
bool application_startup_flow_consume_publication(qa_application *, application_publication *,
    const qa_launch_snapshot *, qa_error *);
bool application_startup_flow_cleanup_publication(qa_application *, application_publication *, qa_error *);
bool application_startup_flow_release_provider(application_provider *, qa_error *);
bool application_startup_script_read(application_provider *, const qa_command_context *,
    const char *, qa_bytes *, void **, qa_error *);
void application_startup_script_release(application_provider *, void *);
void application_startup_script_complete(application_provider *, const qa_command_context *,
    const char *, bool);
bool application_startup_source_active(const application_provider *);
bool application_startup_command_allowed(application_provider *, const qa_command_invocation *);
bool application_startup_console_active(const application_provider *, const qa_console *);
bool application_startup_console_command_allowed(application_provider *, const qa_console *,
    const qa_command_invocation *);
bool application_startup_console_script_read(application_provider *, const qa_console *,
    const qa_command_context *, const char *, qa_bytes *, void **, qa_error *);
void application_startup_console_script_release(application_provider *, const qa_console *, void *);
void application_startup_console_script_complete(application_provider *, const qa_console *,
    const qa_command_context *, const char *, bool);
bool application_startup_source_preinit(application_provider *, qa_console *, qa_cvars *,
    const qa_command_context *, qa_error *);
bool application_startup_source_restore(application_provider *, qa_console *, qa_cvars *,
    const qa_command_context *, qa_error *);
bool application_startup_source_retire(application_provider *, qa_console *, qa_cvars *, qa_error *);
bool application_startup_source_deconstruct(application_provider *, qa_error *);
bool application_startup_source_carry(application_provider *,
    const qa_application_startup_source *, bool *carried, qa_error *);
bool application_startup_source_configuration(application_provider *, qa_console *,
    qa_cvars *, qa_settings_store *, qa_error *);
bool application_startup_tuple_preinit(application_provider *, const qa_application_startup_source *, qa_error *);
bool application_startup_tuple_restore(application_provider *, const qa_application_startup_source *, qa_error *);
bool application_startup_tuple_retire(application_provider *, const qa_application_startup_source *, qa_error *);
bool application_startup_tuple_retire_client(application_provider *, const qa_application_startup_source *, qa_error *);
bool application_startup_tuple_bind_client(application_provider *, const qa_launch_snapshot *,
    const qa_application_startup_source *, const qa_application_startup_source *, qa_cvars **, qa_error *);
void application_startup_tuple_bound_client(application_provider *, const qa_application_startup_source *);
void application_startup_flow_bound_client(qa_application *, application_provider *,
    const qa_application_startup_source *);
bool application_provider_startup_source_at(application_provider *, size_t,
    qa_application_startup_source *, bool *found, qa_error *);
qa_command_result application_startup_common_command(application_provider *, qa_console *,
    qa_cvars *, const qa_command_invocation *, qa_error *);
qa_cvars *application_startup_cvar_owner(application_provider *, qa_console *,
    const qa_command_context *, const char *);
bool application_startup_console_cvar_edit(qa_application *, qa_console *,
    const qa_command_context *, qa_cvars *, qa_cvars_edit **, qa_error *);
bool application_startup_cvar_edit(application_provider *, qa_console *,
    const qa_command_context *, qa_cvars *, qa_cvars_edit **, qa_error *);
bool application_startup_root_register(application_provider *, const char *, const char *,
    uint32_t, uint64_t, qa_error *);
bool application_startup_visible_cvars(application_provider *, qa_console *,
    const qa_command_context *, size_t, qa_cvars **);
bool application_startup_source_scripts(const application_provider *);
bool application_startup_source_script_read(application_provider *, qa_console *,
    const qa_command_context *, const char *, qa_bytes *, void **, qa_error *);
void application_startup_source_script_release(application_provider *, qa_console *, void *);
bool application_provider_console_prepare(qa_application *, application_provider *, qa_world *,
    qa_catalog *, const qa_product *, const qa_launch_choices *, qa_console **,
    qa_cvars **, qa_command_context *, qa_error *);
bool application_publication_begin(qa_application *, const qa_launch_snapshot *,
    const qa_launch_snapshot *, application_publication **, qa_error *);
bool application_publication_finish(qa_application *, application_publication *, qa_error *);
/* A direct source replacement lends its real validated ticket only during
 * configuration preflight. Its caller retains the candidate until outcome. */
bool application_startup_publication_prepare(qa_application *, application_publication *, qa_error *);
void application_startup_publication_finish(qa_application *, const qa_launch_snapshot *, bool);
#endif
