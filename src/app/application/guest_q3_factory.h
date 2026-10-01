#ifndef QA_APPLICATION_GUEST_Q3_FACTORY_H
#define QA_APPLICATION_GUEST_Q3_FACTORY_H

#include "internal.h"
#include "qa/application_startup_prepare.h"
#include "qa/console_program.h"

struct application_q3_guest;
struct q3g_role;
/* The retained source client authors its UI VM independently of visible MENU. */
bool application_guest_q3_source_ui_create(struct application_q3_guest *, uint32_t,
    struct q3g_role **, qa_error *);

/* Prepare the actual detached GAME or CLIENT consoles before configuration.
 * No host, executor, source Init, or world publication occurs here. */
bool application_guest_q3_console_prepare(qa_application *, application_provider *,
    qa_world *, const qa_product *, const qa_launch_choices *, qa_console **,
    qa_cvars **, qa_command_context *, qa_error *);
bool application_guest_q3_factory_reuse(qa_application *, application_provider *,
    qa_world *, const qa_product *, const qa_launch_choices *, qa_error *);
bool application_guest_q3_startup_source_at(application_provider *, size_t,
    qa_application_startup_source *, bool *found, qa_error *);
bool application_guest_q3_program_identity(application_provider *, application_provider *,
    const qa_application_startup_source *, const qa_application_startup_source *,
    qa_console_program_identity, uint64_t, uint64_t *, bool *, qa_error *);

#endif
