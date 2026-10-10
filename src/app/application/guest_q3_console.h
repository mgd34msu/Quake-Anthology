#ifndef QA_APPLICATION_GUEST_Q3_CONSOLE_H
#define QA_APPLICATION_GUEST_Q3_CONSOLE_H

#include "internal.h"
#include "q3_product.h"

struct application_q3_guest;
struct application_guest_q3_console;

bool application_guest_q3_console_create(struct application_q3_guest *, const char *,
    bool restoring, qa_error *);
bool application_guest_q3_console_destroy(struct application_q3_guest *, qa_error *);
bool application_guest_q3_console_idle(const struct application_q3_guest *);
qa_cvars *application_guest_q3_console_registry(const application_provider *);
qa_console *application_guest_q3_console_owner(const application_provider *);
qa_cvars *application_guest_q3_cvar_owner(const application_provider *, const char *);
bool application_guest_q3_console_startup(application_provider *, qa_error *);

qa_cvar_handle application_guest_q3_console_control(const application_provider *, application_q3_cvar_control);

#endif
