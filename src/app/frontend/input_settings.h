#ifndef QA_FRONTEND_INPUT_SETTINGS_H
#define QA_FRONTEND_INPUT_SETTINGS_H
#include "internal.h"
#include "qa/application_engine_shutdown.h"

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
bool frontend_input_settings_reconnect_prepare(qa_frontend *,double now_ms,
    frontend_input_settings **,qa_error *);
bool frontend_input_settings_current(const frontend_input_settings *,const qa_frontend *,qa_error *);
bool frontend_input_settings_read(const frontend_input_settings *,frontend_input_settings_view *,qa_error *);
/* Advance at a returned callback boundary before ordinary input, capture or
 * owner-idle admission. A wait is successful with complete=false. Failed
 * entered history remains owned; subsequent advance never replays dispatch.
 * After an abort request, advance finishes that retained abort; read terminal
 * before choosing publication. */
bool frontend_input_settings_advance(frontend_input_settings *,bool *complete,qa_error *);
bool frontend_input_settings_ready(const frontend_input_settings *,qa_error *);
void frontend_input_settings_publish(frontend_input_settings *);
/* Checked abort never claims reversal of an entered source/native effect.
 * A refusal retains the owner and all remaining physical parents. */
bool frontend_input_settings_abort(frontend_input_settings *,qa_error *);
/* Disposes an entered native replacement using retained completed source
 * proofs. RETIRED preserves the actual irreversible endpoint history. */
bool frontend_input_settings_retire_entered(frontend_input_settings *,qa_error *);
bool frontend_input_settings_retire_actor(frontend_input_settings *,qa_error *);
bool frontend_input_settings_retire_source(frontend_input_settings *,qa_application *,
    const qa_console *,qa_error *);
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
