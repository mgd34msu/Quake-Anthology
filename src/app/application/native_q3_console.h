#ifndef QA_APPLICATION_NATIVE_Q3_CONSOLE_H
#define QA_APPLICATION_NATIVE_Q3_CONSOLE_H

#include "internal.h"

/* map_path is the actual incoming map, before public map metadata is replaced.
 * Engine defaults and genuine startup current/latch values share this owner;
 * empty package cvars are not evidence that pure metadata was prepared. */
bool application_native_q3_console_create(application_provider *, const char *map_path,
    qa_error *);
bool application_native_q3_console_destroy(application_provider *, qa_error *);
bool application_native_q3_console_idle(const application_provider *);
bool application_native_q3_console_borrow(application_provider *, qa_error *);
void application_native_q3_console_release(application_provider *);
bool application_native_q3_console_at(application_provider *, qa_console **,
                                       qa_cvars **, qa_command_context *);
qa_cvars *application_native_q3_console_registry(const application_provider *);
bool application_native_q3_console_settings_bound(const application_provider *);
void application_native_q3_console_settings_commit(application_provider *);
qa_cvars *application_native_q3_cvar_owner(const application_provider *, const char *);

#endif
