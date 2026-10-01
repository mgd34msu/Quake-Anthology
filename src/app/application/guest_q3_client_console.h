#ifndef QA_APPLICATION_GUEST_Q3_CLIENT_CONSOLE_H
#define QA_APPLICATION_GUEST_Q3_CLIENT_CONSOLE_H
#include "guest_q3_private.h"
#include "qa/application_startup_prepare.h"

bool application_guest_q3_client_console_prepare(struct application_q3_guest *,
    qa_qvm_role, uint32_t, qa_error *);
bool application_guest_q3_client_consoles_prepare(struct application_q3_guest *,
    const qa_launch_choices *, qa_error *);
bool application_guest_q3_client_console_source(struct application_q3_guest *, size_t,
    qa_application_startup_source *);
bool application_guest_q3_client_console_at(struct application_q3_guest *, uint32_t,
    qa_console **, qa_cvars **);
bool application_guest_q3_client_console_bind(struct application_q3_guest *, uint32_t,
    qa_cvars *, qa_error *);
bool application_guest_q3_client_console_take(struct application_q3_guest *, uint32_t,
    qa_cvars **, qa_error *);
bool application_guest_q3_client_console_idle(const struct application_q3_guest *);
bool application_guest_q3_client_console_destroy(struct application_q3_guest *, qa_error *);
#endif
