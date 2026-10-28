/* Port of src/app/bootstrap/q1-source-cvars.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q1_SOURCE_CVARS_H
#define QA_APP_BOOTSTRAP_Q1_SOURCE_CVARS_H
#include "qa/core/cvars/index.h"
/* NULL only on OOM. print/print_ctx become the candidate's output. */
qa_cvar_registry *qa_app_clone_q1_source_cvars(qa_cvar_registry *source,
    qa_command_print_fn print, void *print_ctx);
void qa_app_register_q1_bot_controls(qa_cvar_registry *cvars);
#endif
