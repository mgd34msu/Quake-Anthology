#ifndef QA_APPLICATION_NATIVE_Q2_CONSOLE_H
#define QA_APPLICATION_NATIVE_Q2_CONSOLE_H
#include "internal.h"

typedef struct application_q2_source_scripts {
    void *context;
    bool (*read)(void *, const qa_command_context *, const char *, qa_bytes *, void **, qa_error *);
    void (*release)(void *, void *);
    void (*complete)(void *, const qa_command_context *, const char *, bool);
} application_q2_source_scripts;
/* Borrow preparation callbacks until an idle, empty console unbinds them with
 * NULL, or until console destruction. The caller retains the callback owner. */
bool application_native_q2_console_scripts(application_provider *, const application_q2_source_scripts *, qa_error *);
bool application_native_q2_console_prepare(application_provider *, const qa_q2_options *,
    const qa_launch_choices *, qa_error *);
/* Call after the actual configuration continuation has completed. */
bool application_native_q2_console_finalize(application_provider *, qa_q2_options *,
    const qa_launch_choices *, qa_error *);
bool application_native_q2_console_create_restored(application_provider *, qa_error *);
bool application_native_q2_console_destroy(application_provider *, qa_error *);
bool application_native_q2_console_idle(const application_provider *);
bool application_native_q2_console_at(application_provider *, qa_console **, qa_cvars **,
    qa_command_context *);
qa_cvars *application_native_q2_console_registry(const application_provider *);
bool application_native_q2_console_capture(application_provider *, qa_buffer *, qa_error *);
bool application_native_q2_console_restore(application_provider *, qa_bytes, qa_error *);
/* Pure projection, also required after host player-service configuration and
 * candidate import. Current values are read; pending latches stay pending. */
bool application_native_q2_console_refresh(application_provider *, qa_error *);
bool application_native_q2_rotation_changed(void *, const qa_string_id *, size_t, qa_error *);
bool application_native_q2_source_player_rules(application_provider *, qa_q2_player_rules *, qa_error *);
bool application_native_q2_source_mode_rules(application_provider *, qa_mode_rules *, qa_error *);
/* Runtime source-frame projection, after current GAME values have changed.
 * Candidate import uses the pure rules reader and restores its own mode owner. */
bool application_native_q2_source_modes_refresh(application_provider *, qa_error *);
bool application_native_q2_source_number(const application_provider *, const char *, float *, qa_error *);
bool application_native_q2_source_integer(const application_provider *, const char *, int32_t *, qa_error *);
bool application_native_q2_source_weapon_input(application_provider *, qa_q2_weapon_input *, qa_error *);
bool application_native_q2_cvar(void *, qa_string_id, float *, qa_error *);
#endif
