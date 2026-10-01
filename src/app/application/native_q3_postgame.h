#ifndef QA_APPLICATION_NATIVE_Q3_POSTGAME_H
#define QA_APPLICATION_NATIVE_Q3_POSTGAME_H

#include "internal.h"

/* BeginIntermission calls this after storing its actual source pose and time,
 * before respawning and moving the fixed source clients. */
bool application_native_q3_match_begin_product(application_provider *, qa_error *);
bool application_native_q3_postgame_console(application_provider *,
    const qa_command_invocation *, bool *handled, qa_error *);
bool application_native_q3_postgame_cvar_integer(void *, const char *, int32_t *, qa_error *);

#endif
