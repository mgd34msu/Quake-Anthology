#ifndef QA_APPLICATION_GUEST_Q3_FACTORY_H
#define QA_APPLICATION_GUEST_Q3_FACTORY_H

#include "internal.h"

struct application_q3_guest;
struct q3g_role;
/* The retained source client authors its UI VM independently of visible MENU. */
bool application_guest_q3_source_ui_create(struct application_q3_guest *, uint32_t,
    struct q3g_role **, qa_error *);

/* Prepare the actual detached GAME engine and console before configuration.
 * No host, executor, source Init, or world publication occurs here. */
bool application_guest_q3_console_prepare(qa_application *, application_provider *,
    qa_world *, const qa_product *, const qa_launch_choices *, qa_console **,
    qa_cvars **, qa_command_context *, qa_error *);
bool application_guest_q3_factory_reuse(qa_application *, application_provider *,
    qa_world *, const qa_product *, const qa_launch_choices *, qa_error *);

#endif
