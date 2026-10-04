#ifndef QA_NATIVE_Q3_CLIENT_SETTINGS_H
#define QA_NATIVE_Q3_CLIENT_SETTINGS_H
#include "qa/application_native_q3_client.h"
#include "../../presentation/q3_native/view.h"
#include "../../presentation/q3_native/hud.h"
#include "../../presentation/q3_native/native.h"

/* Borrowed cache/registry access for one projection. The caller retains its
 * actual local, received or compiled Source lifetime and current witness. */
typedef struct application_q3_client_settings_source {
    const void *context;
    bool (*read)(const void *, const char *, qa_native_q3_client_cvar *, qa_error *);
    qa_cvars *cvars;
    qa_q3_product product;
} application_q3_client_settings_source;
bool application_q3_client_view_settings(const application_q3_client_settings_source *,
    int32_t actual_dm_flags, bool actual_ragepro, q3n_view_settings *, qa_error *);
bool application_q3_client_hud_settings(const application_q3_client_settings_source *,
    q3n_hud_settings *, qa_error *);
bool application_q3_client_info_settings(const application_q3_client_settings_source *,
    size_t actual_memory_remaining, bool actual_loading, q3n_client_settings *, qa_error *);
bool application_q3_client_frame_settings(const application_q3_client_settings_source *,
    int32_t actual_dm_flags, bool actual_ragepro, size_t actual_memory_remaining,
    bool actual_loading, bool actual_demo_playback, uint32_t actual_stereo,
    q3n_native_frame_options *, qa_error *);
bool application_q3_client_cache_copy(qa_native_q3_client_cvar *, const qa_cvar_view *,
    bool force, const char *oversized_error, qa_error *);

bool application_native_q3_client_view_settings(const qa_native_q3_client_service *,
    int32_t actual_dm_flags, bool actual_ragepro, q3n_view_settings *, qa_error *);
bool application_native_q3_client_hud_settings(const qa_native_q3_client_service *,
    q3n_hud_settings *, qa_error *);
bool application_native_q3_client_frame_settings(const qa_native_q3_client_service *,
    int32_t actual_dm_flags, bool actual_ragepro, size_t actual_memory_remaining,
    bool actual_loading, bool actual_demo_playback, uint32_t actual_stereo,
    q3n_native_frame_options *, qa_error *);
bool application_native_q3_client_set_view_size(void *, int32_t, qa_error *);
bool application_native_q3_client_set_orbit_angle(void *, float, qa_error *);
#endif
