#ifndef QA_FRONTEND_INPUT_SETTINGS_H
#define QA_FRONTEND_INPUT_SETTINGS_H
#include "internal.h"
#include "qa/application_engine_shutdown.h"
#include "qa/display_settings.h"

typedef struct frontend_input_settings frontend_input_settings;
typedef struct frontend_input_settings_view {
    qa_input_platform_settings_outcome native;
    qa_input_release_outcome source[QA_INPUT_LOCAL_SEATS];
    bool prepared, aborting, terminal;
    unsigned retained_sources;
    qa_error failure;
} frontend_input_settings_view;

/* A returned owner, including on failed preparation, retains the actual
 * frontend/application/native and physical source parents until checked
 * abort or admitted publication and retirement. No settings registry exists
 * here: desired values and configurations belong to the canonical producer. */
bool frontend_input_settings_prepare(qa_frontend *,const qa_input_platform_settings *,
    qa_input_seat *const configuration[QA_INPUT_LOCAL_SEATS],double now_ms,
    frontend_input_settings **,qa_error *);
bool frontend_input_settings_prepare_selected(qa_frontend *,const qa_input_platform_settings *,
    qa_input_seat *const configuration[QA_INPUT_LOCAL_SEATS],const qa_controller_selection [QA_INPUT_LOCAL_SEATS],
    double,frontend_input_settings **,qa_error *);
bool frontend_input_settings_reconnect_prepare(qa_frontend *,double now_ms,
    frontend_input_settings **,qa_error *);
bool frontend_input_settings_current(const frontend_input_settings *,const qa_frontend *,qa_error *);
/* Exact advancing captured release command; this is separate from pending
 * GAME/CLIENT startup tuple admission and does not restamp source identity. */
bool frontend_input_settings_command_current(const frontend_input_settings *,const qa_console *,
                                             const qa_command_context *);
/* Exact retained native/physical release associations for final detach
 * admission. No completed-source or retirement proof is substituted here. */
bool frontend_input_settings_shutdown_ready(const frontend_input_settings *,const qa_frontend *,qa_error *);
/* Extend this same installed owner's scopes to ALL before ENGINE detach,
 * capturing only uncovered held rows. Partial success remains retained and
 * retryable; failed entered programmes are not dispatched or recaptured. */
bool frontend_input_settings_shutdown_prepare(frontend_input_settings *,double now_ms,qa_error *);
/* A real window replacement releases every physical held row while keeping
 * the native settings publication admissible. This is not a restart request
 * or a shutdown disposition. Partial captures remain owned for retry. */
bool frontend_input_settings_release_all_prepare(frontend_input_settings *,double now_ms,qa_error *);
/* Retain complete teardown coverage before scoped authored commands can fail.
 * Uncovered rows are captured but remain dormant during normal release. */
bool frontend_input_settings_reserve_all(frontend_input_settings *,qa_error *);
/* Pure actual failed entered history plus complete physical coverage receipt. */
bool frontend_input_settings_failed_coverage_is(const frontend_input_settings *,const qa_frontend *);
bool frontend_input_settings_release_all_ready(const frontend_input_settings *,qa_error *);
/* Checked native association plus empty, unentered physical histories before
 * final fallible resource admission. Native queries remain preparation work. */
bool frontend_input_settings_unentered_empty(const frontend_input_settings *,qa_error *);
/* Associates the actual candidate surface after every physical ALL release
 * completes. Both native windows remain retained through checked cleanup. */
bool frontend_input_settings_window_stage(frontend_input_settings *,qa_display_surface_ticket *,qa_error *);
bool frontend_input_settings_read(const frontend_input_settings *,frontend_input_settings_view *,qa_error *);
/* Complete genuine source programmes without entering native endpoint changes.
 * This gives the enclosing canonical owner a returned boundary to read their
 * final scalar effects and qualify its prepared resource targets. Waits retain
 * all history. An aborting owner advances through the ordinary abort API. */
bool frontend_input_settings_release_advance(frontend_input_settings *,bool *complete,qa_error *);
/* Enter native work only after those actual programmes complete and the
 * enclosing owner has qualified their final canonical targets. */
bool frontend_input_settings_enter(frontend_input_settings *,qa_error *);
/* Advance at a returned callback boundary before ordinary input, capture or
 * owner-idle admission. A wait is successful with complete=false. Failed
 * entered history remains owned; subsequent advance never replays dispatch.
 * After an abort request, advance finishes that retained abort; read terminal
 * before choosing publication. */
bool frontend_input_settings_advance(frontend_input_settings *,bool *complete,qa_error *);
bool frontend_input_settings_ready(const frontend_input_settings *,qa_error *);
/* Pure proof of this installed owner's actual successful native receipt and
 * the same retained completed physical release identities. */
bool frontend_input_settings_ready_is(const frontend_input_settings *);
void frontend_input_settings_publish(frontend_input_settings *);
/* Checked abort never claims reversal of an entered source/native effect.
 * A refusal retains the owner and all remaining physical parents. */
bool frontend_input_settings_abort(frontend_input_settings *,qa_error *);
/* Final resource cleanup after a fault may retire an actual entered native
 * endpoint only with completed, empty physical release histories. */
bool frontend_input_settings_abort_empty(frontend_input_settings *,qa_error *);
/* Disposes an entered native replacement using retained completed source
 * proofs. RETIRED preserves the actual irreversible endpoint history. */
bool frontend_input_settings_retire_entered(frontend_input_settings *,qa_error *);
bool frontend_input_settings_retire_actor(frontend_input_settings *,qa_error *);
bool frontend_input_settings_retire_source(frontend_input_settings *,qa_application *,
    const qa_cvars *,qa_error *);
/* Actual detach-first final ENGINE loan. Preflights every captured history
 * and physical scope before checked native cleanup; no command dispatch runs.
 * complete reports terminal consumption even when a real close error returns
 * false. A refusing cleanup retains the loan's frontend/native/source parents. */
bool frontend_input_settings_engine_shutdown(frontend_input_settings *,
    const qa_application_engine_shutdown *,bool *complete,qa_error *);
const char *frontend_input_settings_diagnostic(const frontend_input_settings *);
/* Only a terminal owner is destroyable. Terminal native close errors consume
 * the owner too; the returned status still reports that real cleanup error. */
bool frontend_input_settings_destroy(frontend_input_settings **,qa_error *);
#endif
