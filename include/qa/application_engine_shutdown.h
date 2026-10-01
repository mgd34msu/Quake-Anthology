#ifndef QA_APPLICATION_ENGINE_SHUTDOWN_H
#define QA_APPLICATION_ENGINE_SHUTDOWN_H
#include "qa/application.h"

typedef struct qa_application_engine_shutdown qa_application_engine_shutdown;
/* Final returned-driver boundary. Detach the actual ENGINE slots while all
 * application, world, source and frontend callback parents remain retained.
 * A repeated call returns the same application-owned shutdown loan. */
bool qa_application_engine_shutdown_begin(qa_application *, qa_application_engine_shutdown **, qa_error *);
bool qa_application_engine_shutdown_read(const qa_application_engine_shutdown *,
    qa_console **, qa_cvars **, qa_error *);
qa_application *qa_application_engine_shutdown_owner(const qa_application_engine_shutdown *);
/* Qualifies retained release history on this exact detached ENGINE console.
 * Historical actor/publication stamps alone do not prove this disposition. */
bool qa_application_engine_shutdown_retiring(const qa_application *, const qa_console *,
    const qa_command_context *);
/* After actual source retirement and all frontend/native/input borrowers have
 * returned. A refusal retains the loan and every application parent. */
bool qa_application_engine_shutdown_finish(qa_application *, qa_application_engine_shutdown **, qa_error *);
#endif
