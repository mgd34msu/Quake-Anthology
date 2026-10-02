#ifndef QA_FRONTEND_SHARED_VALUES_H
#define QA_FRONTEND_SHARED_VALUES_H
#include "config_store.h"
#include "qa/console_cvars_prepare.h"
#include "qa/application_engine_shutdown.h"
#include "qa/application_client_prepare.h"

typedef struct frontend_shared_values frontend_shared_values;
struct frontend_input_settings;
/* One retained scalar ticket edits the actual application ENGINE registry.
 * The manager owns this ticket and keeps its app/candidate/source rows alive.
 * Begin once before cfg; subsequent sources join that same admitted ticket. */
bool frontend_shared_values_begin(qa_frontend *,frontend_config_store *,qa_application *,
    const qa_launch_snapshot *,const qa_application_startup_source *,
    frontend_shared_values **,qa_error *);
/* The real StartupFlow ENGINE root, including its explicit source-free
 * bootstrap, owns this preparation before any physical source joins. */
bool frontend_shared_values_begin_root(qa_frontend *,frontend_config_store *,qa_application *,
    const qa_launch_snapshot *,frontend_shared_values **,qa_error *);
/* Command registration after the real images boundary reads the retained
 * canonical edit while its actual ENGINE console has returned. */
bool frontend_shared_values_root_access(const frontend_shared_values *,const qa_console *,
    const qa_command_context *,qa_cvars **,qa_cvars_edit **,qa_error *);
bool frontend_shared_values_begin_client(qa_frontend *,frontend_config_store *,qa_application *,
    qa_application_client_preparation *,frontend_shared_values **,qa_error *);
bool frontend_shared_values_client_access(const frontend_shared_values *,
    const qa_application_client_preparation *,const qa_command_context *,
    qa_cvars **,qa_cvars_edit **,qa_error *);
bool frontend_shared_values_client_input_restart(frontend_shared_values *,
    const qa_application_client_preparation *,const qa_command_context *,qa_error *);
/* The real validated StartupFlow supplies its newly enumerated declaration
 * owner. All physical source identity remains unchanged from begin. */
bool frontend_shared_values_refresh(frontend_shared_values *,
    const qa_application_startup_source *,qa_error *);
qa_cvars *frontend_shared_values_registry(const frontend_shared_values *);
/* These helpers require the actual manager's still-pending physical tuple and
 * its current captured command. A refusal never exposes live ENGINE values. */
bool frontend_shared_values_resolve(const frontend_shared_values *,
    const qa_application_startup_source *,const qa_command_context *,const char *,
    qa_cvars **,qa_error *);
bool frontend_shared_values_edit(const frontend_shared_values *,
    const qa_application_startup_source *,const qa_command_context *,qa_cvars *,
    qa_cvars_edit **,qa_error *);
/* A real held release on the published ENGINE console uses its retained
 * synchronous programme capability, never a pending source's identity. */
bool frontend_shared_values_release_access(const frontend_shared_values *,
    const struct frontend_input_settings *,const qa_console *,const qa_command_context *,
    qa_cvars **,qa_cvars_edit **,qa_error *);
/* The separate image-settings programme owns a genuine transient console.
 * Its manager proves that exact executing programme and candidate context;
 * unknown image cvars belong to ENGINE rather than a source-private heap. */
bool frontend_shared_values_programme_access(const frontend_shared_values *,
    const qa_console *,const qa_command_context *,qa_cvars **,qa_cvars_edit **,qa_error *);
/* A genuine pending in_restart stages its request before any held-key or
 * device side effect. The enclosing native ticket consumes this exact bit. */
bool frontend_shared_values_input_restart(frontend_shared_values *,
    const qa_application_startup_source *,const qa_command_context *,qa_error *);
bool frontend_shared_values_release_input_restart(frontend_shared_values *,
    const struct frontend_input_settings *,const qa_console *,const qa_command_context *,qa_error *);
bool frontend_shared_values_input_restart_pending(const frontend_shared_values *);
bool frontend_shared_values_archive(frontend_shared_values *,const qa_cvar_archive *,qa_error *);
/* Source R_Register initialization applies its actual r_znear range to the
 * mutable canonical ticket. Later live sets retain their normal semantics. */
bool frontend_shared_values_q3_renderer_initialize(frontend_shared_values *,qa_error *);
bool frontend_shared_values_source_color_initialize(frontend_shared_values *,qa_error *);
bool frontend_shared_values_source_color_register(frontend_shared_values *,qa_error *);
/* First native construction applies only the real joystick/profile latches;
 * it does not request another device restart after those devices exist. */
bool frontend_shared_values_native_initialize(frontend_shared_values *,qa_error *);
bool frontend_shared_values_window_observed(frontend_shared_values *,const qa_display_info *,
    int swap_interval,bool opengl,qa_error *);
const qa_cvars_edit *frontend_shared_values_prepared(const frontend_shared_values *);
/* Scalar admission is only one child of shared settings. The enclosing owner
 * must ready all actual resource/device/UI children before publishing any. */
bool frontend_shared_values_ready(frontend_shared_values *,qa_error *);
bool frontend_shared_values_ready_is(const frontend_shared_values *);
void frontend_shared_values_publish(frontend_shared_values *);
bool frontend_shared_values_finish(frontend_shared_values *,qa_error *);
bool frontend_shared_values_abort(frontend_shared_values *,qa_error *);
/* Cancellation under the actual candidate ENGINE detach loan. It consumes
 * only this retained unpublished edit, including a faulted edit, and rows. */
bool frontend_shared_values_engine_shutdown(frontend_shared_values **,
    const qa_application_engine_shutdown *,qa_error *);
bool frontend_shared_values_destroy(frontend_shared_values **,qa_error *);
#endif
