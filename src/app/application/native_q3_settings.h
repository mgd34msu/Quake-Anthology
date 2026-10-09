#ifndef QA_APPLICATION_NATIVE_Q3_SETTINGS_H
#define QA_APPLICATION_NATIVE_Q3_SETTINGS_H

#include "internal.h"

typedef enum application_native_q3_setting {
    APPLICATION_Q3_SETTING_SV_CHEATS,
    APPLICATION_Q3_SETTING_G_RESTARTED,
    APPLICATION_Q3_SETTING_G_GAMETYPE,
    APPLICATION_Q3_SETTING_SV_MAXCLIENTS,
    APPLICATION_Q3_SETTING_G_MAX_GAME_CLIENTS,
    APPLICATION_Q3_SETTING_DMFLAGS,
    APPLICATION_Q3_SETTING_FRAGLIMIT,
    APPLICATION_Q3_SETTING_TIMELIMIT,
    APPLICATION_Q3_SETTING_CAPTURELIMIT,
    APPLICATION_Q3_SETTING_G_SYNCHRONOUS_CLIENTS,
    APPLICATION_Q3_SETTING_G_FRIENDLY_FIRE,
    APPLICATION_Q3_SETTING_G_TEAM_AUTO_JOIN,
    APPLICATION_Q3_SETTING_G_TEAM_FORCE_BALANCE,
    APPLICATION_Q3_SETTING_G_WARMUP,
    APPLICATION_Q3_SETTING_G_DO_WARMUP,
    APPLICATION_Q3_SETTING_G_LOG,
    APPLICATION_Q3_SETTING_G_LOG_SYNC,
    APPLICATION_Q3_SETTING_G_PASSWORD,
    APPLICATION_Q3_SETTING_G_BAN_IPS,
    APPLICATION_Q3_SETTING_G_FILTER_BAN,
    APPLICATION_Q3_SETTING_G_NEEDPASS,
    APPLICATION_Q3_SETTING_DEDICATED,
    APPLICATION_Q3_SETTING_G_SPEED,
    APPLICATION_Q3_SETTING_G_GRAVITY,
    APPLICATION_Q3_SETTING_G_KNOCKBACK,
    APPLICATION_Q3_SETTING_G_QUADFACTOR,
    APPLICATION_Q3_SETTING_G_WEAPONRESPAWN,
    APPLICATION_Q3_SETTING_G_WEAPON_TEAM_RESPAWN,
    APPLICATION_Q3_SETTING_G_FORCERESPAWN,
    APPLICATION_Q3_SETTING_G_INACTIVITY,
    APPLICATION_Q3_SETTING_G_DEBUG_MOVE,
    APPLICATION_Q3_SETTING_G_DEBUG_DAMAGE,
    APPLICATION_Q3_SETTING_G_DEBUG_ALLOC,
    APPLICATION_Q3_SETTING_G_MOTD,
    APPLICATION_Q3_SETTING_COM_BLOOD,
    APPLICATION_Q3_SETTING_G_PODIUM_DIST,
    APPLICATION_Q3_SETTING_G_PODIUM_DROP,
    APPLICATION_Q3_SETTING_G_ALLOW_VOTE,
    APPLICATION_Q3_SETTING_G_LIST_ENTITY,
    APPLICATION_Q3_SETTING_G_OBELISK_HEALTH,
    APPLICATION_Q3_SETTING_G_OBELISK_REGEN_PERIOD,
    APPLICATION_Q3_SETTING_G_OBELISK_REGEN_AMOUNT,
    APPLICATION_Q3_SETTING_G_OBELISK_RESPAWN_DELAY,
    APPLICATION_Q3_SETTING_G_CUBE_TIMEOUT,
    APPLICATION_Q3_SETTING_G_REDTEAM,
    APPLICATION_Q3_SETTING_G_BLUETEAM,
    APPLICATION_Q3_SETTING_UI_SINGLE_PLAYER_ACTIVE,
    APPLICATION_Q3_SETTING_G_ENABLE_DUST,
    APPLICATION_Q3_SETTING_G_ENABLE_BREATH,
    APPLICATION_Q3_SETTING_G_PROX_MINE_TIMEOUT,
    APPLICATION_Q3_SETTING_G_SMOOTH_CLIENTS,
    APPLICATION_Q3_SETTING_PMOVE_FIXED,
    APPLICATION_Q3_SETTING_PMOVE_MSEC,
    APPLICATION_Q3_SETTING_G_RANKINGS,
    APPLICATION_Q3_SETTING_SV_ENABLE_RANKINGS,
    APPLICATION_Q3_SETTING_SV_RANKINGS_ACTIVE,
    APPLICATION_Q3_SETTING_COUNT
} application_native_q3_setting;

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
bool application_native_q3_settings_force_set(application_provider *, application_native_q3_setting,
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
/* Fixed engine callers address the same product-qualified GAME snapshots. */
bool application_native_q3_settings_snapshot_at(const application_provider *, application_native_q3_setting,
    const application_native_q3_cvar_snapshot **, qa_error *);
bool application_native_q3_settings_integer_at(const application_provider *, application_native_q3_setting,
    int32_t *, qa_error *);
bool application_native_q3_settings_number_at(const application_provider *, application_native_q3_setting,
    float *, qa_error *);
bool application_native_q3_settings_string_at(const application_provider *, application_native_q3_setting,
    const char **, qa_error *);

/* Save only pending copied gameplay values and the actual warmup observation.
 * Import rebuilds declaration metadata and settings from the current bank;
 * it neither registers cvars nor changes their values or sends effects. */
bool application_native_q3_settings_capture(application_provider *, qa_buffer *, qa_error *);
bool application_native_q3_settings_restore(application_provider *, qa_bytes, qa_error *);

#endif
