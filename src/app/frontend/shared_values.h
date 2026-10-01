#ifndef QA_FRONTEND_SHARED_VALUES_H
#define QA_FRONTEND_SHARED_VALUES_H
#include "config_store.h"
#include "qa/console_cvars_prepare.h"

typedef struct frontend_shared_values frontend_shared_values;
/* One retained scalar ticket edits the actual application ENGINE registry.
 * The manager owns this ticket and keeps its app/candidate/source rows alive.
 * Begin once before cfg; subsequent sources join that same admitted ticket. */
bool frontend_shared_values_begin(qa_frontend *,frontend_config_store *,qa_application *,
    const qa_launch_snapshot *,const qa_application_startup_source *,
    frontend_shared_values **,qa_error *);
qa_cvars *frontend_shared_values_registry(const frontend_shared_values *);
/* These helpers require the actual manager's still-pending physical tuple and
 * its current captured command. A refusal never exposes live ENGINE values. */
bool frontend_shared_values_resolve(const frontend_shared_values *,
    const qa_application_startup_source *,const qa_command_context *,const char *,
    qa_cvars **,qa_error *);
bool frontend_shared_values_edit(const frontend_shared_values *,
    const qa_application_startup_source *,const qa_command_context *,qa_cvars *,
    qa_cvars_edit **,qa_error *);
/* A genuine pending in_restart stages its request before any held-key or
 * device side effect. The enclosing native ticket consumes this exact bit. */
bool frontend_shared_values_input_restart(frontend_shared_values *,
    const qa_application_startup_source *,const qa_command_context *,qa_error *);
bool frontend_shared_values_input_restart_pending(const frontend_shared_values *);
bool frontend_shared_values_archive(frontend_shared_values *,const qa_cvar_archive *,qa_error *);
const qa_cvars_edit *frontend_shared_values_prepared(const frontend_shared_values *);
/* Scalar admission is only one child of shared settings. The enclosing owner
 * must ready all actual resource/device/UI children before publishing any. */
bool frontend_shared_values_ready(frontend_shared_values *,qa_error *);
void frontend_shared_values_publish(frontend_shared_values *);
bool frontend_shared_values_finish(frontend_shared_values *,qa_error *);
bool frontend_shared_values_abort(frontend_shared_values *,qa_error *);
bool frontend_shared_values_destroy(frontend_shared_values **,qa_error *);
#endif
