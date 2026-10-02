#ifndef QA_FRONTEND_SHARED_SETTINGS_H
#define QA_FRONTEND_SHARED_SETTINGS_H
#include "shared_values.h"
#include "input_settings.h"

typedef struct frontend_shared_settings frontend_shared_settings;
/* One candidate owns the canonical scalar ticket and every retained physical
 * release. A failed entered command keeps its actual parents and history. */
bool frontend_shared_settings_begin(qa_frontend *,frontend_config_store *,qa_application *,
    const qa_launch_snapshot *,const qa_application_startup_source *,frontend_shared_settings **,qa_error *);
bool frontend_shared_settings_begin_root(qa_frontend *,frontend_config_store *,qa_application *,
    const qa_launch_snapshot *,frontend_shared_settings **,qa_error *);
bool frontend_shared_settings_begin_client(qa_frontend *,frontend_config_store *,qa_application *,
    qa_application_client_preparation *,frontend_shared_settings **,qa_error *);
qa_application_client_preparation *frontend_shared_settings_client(const frontend_shared_settings *);
bool frontend_shared_settings_refresh(frontend_shared_settings *,
    const qa_application_startup_source *,qa_error *);
bool frontend_shared_settings_current(const frontend_shared_settings *,const qa_frontend *,
    const qa_application *,const qa_launch_snapshot *);
frontend_shared_values *frontend_shared_settings_values(const frontend_shared_settings *);
/* After the real bootstrap images programme returns, project its actual
 * edit for first native constructors. Caller display hardware/placement
 * options remain authoritative; no native owner is created or published. */
bool frontend_shared_settings_constructor_settings(frontend_shared_settings *,
    qa_display_options *,int *swap_interval,float *gamma,qa_audio_output_format *,
    float *effects,float *music,qa_input_platform_settings *,qa_error *);
/* Invoke at the actual before-validation and after-validation flow hooks.
 * Each phase completes source effects, then checked-aborts its unentered
 * native candidate before allowing source or final resource work. */
bool frontend_shared_settings_advance(frontend_shared_settings *,bool validated,
    bool *complete,qa_error *);
bool frontend_shared_settings_abort(frontend_shared_settings **,qa_error *);
/* Before startup cancellation, finish only the already entered release phase.
 * WAIT retains the live candidate; no source validation or native entry runs. */
bool frontend_shared_settings_cancel_advance(frontend_shared_settings *,bool *complete,qa_error *);
/* Actual failed entered physical history and pre-dispatch complete teardown
 * coverage. This is cancellation authority, never publication readiness. */
bool frontend_shared_settings_retirement_ready(const frontend_shared_settings *,
    const qa_frontend *,const qa_application *,const qa_launch_snapshot *,const qa_cvars_edit *);
/* Loan-qualified cleanup retains every history until checked native cleanup
 * is terminal. complete=true requires the shared owner slot to be consumed. */
bool frontend_shared_settings_engine_shutdown(frontend_shared_settings **,
    const qa_application_engine_shutdown *,bool *complete,qa_error *);
bool frontend_shared_settings_client_shutdown(frontend_shared_settings **,bool *complete,qa_error *);
#endif
