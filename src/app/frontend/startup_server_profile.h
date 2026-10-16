#ifndef QA_FRONTEND_STARTUP_SERVER_PROFILE_H
#define QA_FRONTEND_STARTUP_SERVER_PROFILE_H

#include "qa/settings_server_profile.h"
#include "qa/application_startup_prepare.h"

struct qa_frontend;
typedef struct frontend_server_settings {
    qa_application_startup_source source;
    qa_server_setting_selection selection;
    qa_server_setting_catalog definitions;
} frontend_server_settings;
bool frontend_server_settings_read(struct qa_frontend *, frontend_server_settings *, bool *present, qa_error *);
bool frontend_server_setting_read(struct qa_frontend *, const frontend_server_settings *,
    const qa_server_setting_definition *, qa_server_setting_status *, qa_error *);
bool frontend_server_profile_path(const char *name, char out[46], qa_error *);

/* The selection owner retains only this relative path in its existing state.
 * A NULL path clears it. Failed selection leaves the previous path untouched. */
bool frontend_startup_server_profile_select(const char *relative, char **retained, qa_error *);
/* Read once at Play, after the launch draft has been copied/finalized. The
 * existing startup launch ticket owns the returned immutable profile until
 * the actual candidate completes; seat/menu disposal cannot invalidate it. */
bool frontend_startup_server_profile_capture(qa_settings_store, const char *relative,
    const qa_launch_draft *, qa_server_profile **, qa_error *);
/* Call at the real config-store apply_launch phase, after archives and explicit
 * initial launch values. Nonprimary Sources/CLIENT scopes do not receive this
 * profile. Owner callbacks must qualify the supplied fresh Source tuple and
 * force writes through its existing configuration authority. */
bool frontend_startup_server_profile_apply(const qa_server_profile *,
    const qa_launch_snapshot *, const qa_application_startup_source *,
    const qa_server_profile_owner *, qa_error *);

#endif
