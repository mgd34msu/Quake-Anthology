#ifndef QA_APPLICATION_NATIVE_Q3_SETTINGS_H
#define QA_APPLICATION_NATIVE_Q3_SETTINGS_H

#include "internal.h"

typedef struct application_native_q3_cvar_snapshot {
    const char *name, *value, *reset_value, *latched_value;
    uint32_t flags;
    uint64_t modification_count;
    double numeric_value;
    int32_t integer_value;
    bool modified;
} application_native_q3_cvar_snapshot;

typedef struct application_native_q3_settings_options {
    void *context;
    bool (*send_server_command)(void *, int32_t, const char *, qa_error *);
    bool (*remap_teams)(void *, qa_error *);
    bool (*print)(void *, const char *, qa_error *);
} application_native_q3_settings_options;

/* Construction only allocates an empty owner. Source Init registers and copies
 * the real registry; a save candidate imports the independent copied values. */
bool application_native_q3_settings_create(application_provider *, qa_q3_product,
    const application_native_q3_settings_options *, qa_error *);
/* Register the real source tables before configuration without copying the
 * GAME cache or entering source Init. */
bool application_native_q3_settings_prepare_definitions(application_provider *, qa_q3_product, qa_error *);
qa_cvar_save_policy application_native_q3_cvar_save_policy(const char *);
bool application_native_q3_settings_destroy(application_provider *, qa_error *);
bool application_native_q3_settings_initialized(const application_provider *);
bool application_native_q3_settings_idle(const application_provider *);
bool application_native_q3_settings_register_cache(application_provider *,
    const char *build_date, qa_error *);
bool application_native_q3_settings_reset_cache(application_provider *,
    const char *build_date, qa_error *);

/* Only the actual source G_UpdateCvars producer calls update. Changed registry
 * values and force-set writes do not otherwise refresh this owner. */
bool application_native_q3_settings_update(application_provider *, qa_error *);
bool application_native_q3_settings_force_set(application_provider *, const char *,
    const char *, qa_error *);
/* Source Cvar_Set writes the actual registry, including names outside the
 * product-qualified copied table. It does not refresh cached settings. */
bool application_native_q3_settings_source_set(application_provider *, const char *,
    const char *, qa_error *);
bool application_native_q3_settings_remap_teams(application_provider *, qa_error *);
/* Source CheckCvars runs once at the END tail after the match/team vote work.
 * It commits the last observed password count before writing live g_needpass;
 * that write does not refresh copied settings. New source Init resets it. */
bool application_native_q3_settings_check_cvars(application_provider *, qa_error *);

/* Views and text are borrowed until the next cache update, reset or import.
 * Reads never consult the live registry or invoke an external callback. */
bool application_native_q3_settings_snapshot(const application_provider *, const char *,
    const application_native_q3_cvar_snapshot **, qa_error *);
bool application_native_q3_settings_integer(const application_provider *, const char *,
    int32_t *, qa_error *);
bool application_native_q3_settings_number(const application_provider *, const char *,
    float *, qa_error *);
bool application_native_q3_settings_string(const application_provider *, const char *,
    const char **, qa_error *);

/* Save only pending copied gameplay values and the actual warmup observation.
 * Import rebuilds declaration metadata and settings from the current bank;
 * it neither registers cvars nor changes their values or sends effects. */
bool application_native_q3_settings_capture(application_provider *, qa_buffer *, qa_error *);
bool application_native_q3_settings_restore(application_provider *, qa_bytes, qa_error *);

#endif
