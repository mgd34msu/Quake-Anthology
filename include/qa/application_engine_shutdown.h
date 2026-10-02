#ifndef QA_APPLICATION_ENGINE_SHUTDOWN_H
#define QA_APPLICATION_ENGINE_SHUTDOWN_H
#include "qa/application.h"
#include "qa/console_cvars_prepare.h"

typedef struct qa_application_engine_shutdown qa_application_engine_shutdown;
/* Final returned-driver boundary. Detach the actual ENGINE slots while all
 * application, world, source and frontend callback parents remain retained.
 * A repeated call returns the same application-owned shutdown loan. */
bool qa_application_engine_shutdown_begin(qa_application *, qa_application_engine_shutdown **, qa_error *);
/* A genuinely refused candidate cancellation retains its source/program
 * parents. Admit only its exact canonical edit and entered failed input-history
 * receipt with complete retained physical coverage, then detach ENGINE so those
 * histories can be retired without replay or fresh command capture. A null
 * candidate is admitted only by the actual retained ENGINE-only bootstrap. */
bool qa_application_engine_shutdown_begin_candidate(qa_application *, const qa_launch_snapshot *,
    const qa_cvars_edit *, qa_application_engine_shutdown **, qa_error *);
/* Retained only until that actual candidate's checked cancellation succeeds. */
const qa_launch_snapshot *qa_application_engine_shutdown_candidate(const qa_application_engine_shutdown *);
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
