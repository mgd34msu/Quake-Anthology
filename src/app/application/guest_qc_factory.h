#ifndef APPLICATION_GUEST_QC_FACTORY_H
#define APPLICATION_GUEST_QC_FACTORY_H
#include "guest_qc_internal.h"

/* Retains the real source console before instance creation. Ordinary
 * construction consumes this same engine and preserves startup mutations. */
bool application_qc_console_prepare(qa_application *, application_provider *, qa_world *,
    const qa_product *, const qa_launch_choices *, qa_console **, qa_cvars **,
    qa_command_context *, qa_error *);
bool application_qc_console_destroy(application_provider *, qa_error *);
#endif
