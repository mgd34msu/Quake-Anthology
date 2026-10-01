#ifndef QA_Q3_NATIVE_VIEW_H
#define QA_Q3_NATIVE_VIEW_H

#include "player_state.h"
#include "weapon.h"

typedef struct q3n_view q3n_view;
typedef struct q3n_view_settings {
    int32_t view_size, camera_orbit_integer, camera_orbit_delay, dm_flags;
    float third_person_range, third_person_angle, camera_orbit_value, error_decay;
    float run_pitch, run_roll, bob_pitch, bob_roll, bob_up, fov, zoom_fov;
    float gun_x, gun_y, gun_z;
    bool third_person, camera_mode, ragepro;
} q3n_view_settings;
typedef struct q3n_view_options {
    qa_application *application;
    const qa_application_native_q3_presentation *source;
    qa_q3_presentation_assets *assets;
    qa_native_q3_client_service *client;
    uint32_t seat;
    void *context;
    bool (*set_view_size)(void *, int32_t, qa_error *);
    bool (*set_third_person_angle_value)(void *, float, qa_error *);
    void (*print)(void *, const char *);
} q3n_view_options;
typedef struct q3n_view_state {
    int32_t bob_cycle, next_orbit_time, zoom_time, predicted_error_time;
    float bob_fraction_sin, xy_speed, zoom_sensitivity;
    qa_vec3 kick_angles, kick_origin, predicted_error;
    bool zoomed, hyperspace, test_gun;
} q3n_view_state;
bool q3n_view_create(const q3n_view_options *, q3n_view **, qa_error *);
bool q3n_view_create_restored(const q3n_view_options *, q3n_view **, qa_error *);
void q3n_view_destroy(q3n_view *);
bool q3n_view_idle(const q3n_view *);
const q3n_view_state *q3n_view_read(const q3n_view *);
bool q3n_view_frame(q3n_view *, q3n_frame *, const q3n_view_settings *,
    const q3n_player_state *, qa_scene_rect viewport, bool *in_water, qa_error *);
bool q3n_view_damage_blob(q3n_view *, const q3n_frame *, const q3n_view_settings *,
    const q3n_player_state *, qa_error *);
void q3n_view_zoom(q3n_view *, bool down, int32_t source_time);
void q3n_view_kick(q3n_view *, qa_vec3 angles, qa_vec3 origin);
void q3n_view_error(q3n_view *, qa_vec3 error, int32_t source_time);
void q3n_view_hyperspace(q3n_view *, bool);
bool q3n_view_test_model(q3n_view *, const q3n_frame *, const char *,
    const float *back_lerp, bool gun, qa_error *);
void q3n_view_test_clear(q3n_view *);
void q3n_view_test_step(q3n_view *, bool skin, int32_t delta);
bool q3n_view_test_submit(q3n_view *, const q3n_frame *, const q3n_view_settings *, qa_error *);
void q3n_view_round(q3n_view *);
bool q3n_view_checkpoint(const q3n_view *, qa_buffer *, qa_error *);
bool q3n_view_restore(q3n_view *, qa_bytes, qa_error *);

#endif
