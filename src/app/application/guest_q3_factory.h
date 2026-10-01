#ifndef QA_APPLICATION_GUEST_Q3_FACTORY_H
#define QA_APPLICATION_GUEST_Q3_FACTORY_H

#include "internal.h"

/* Prepare the actual detached GAME engine and console before configuration.
 * No host, executor, source Init, or world publication occurs here. */
bool application_guest_q3_console_prepare(qa_application *, application_provider *,
    qa_world *, const qa_product *, const qa_launch_choices *, qa_console **,
    qa_cvars **, qa_command_context *, qa_error *);
bool application_guest_q3_factory_reuse(qa_application *, application_provider *,
    qa_world *, const qa_product *, const qa_launch_choices *, qa_error *);

#endif
