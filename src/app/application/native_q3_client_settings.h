#ifndef QA_NATIVE_Q3_CLIENT_SETTINGS_H
#define QA_NATIVE_Q3_CLIENT_SETTINGS_H
#include "qa/application_native_q3_client.h"
#include "../../presentation/q3_native/view.h"
#include "../../presentation/q3_native/hud.h"
#include "../../presentation/q3_native/native.h"
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
