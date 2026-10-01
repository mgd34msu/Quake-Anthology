#ifndef QA_Q3_NATIVE_WEAPON_H
#define QA_Q3_NATIVE_WEAPON_H

#include "frame.h"

typedef struct q3n_weapon_settings {
    int32_t brass_time, fov, gun_frame;
    float rail_trail_time, true_lightning, gun_x, gun_y, gun_z;
    float tracer_length, tracer_width, tracer_chance;
    bool old_rail, no_projectile_trail, old_plasma, old_rocket, draw_gun, ragepro;
} q3n_weapon_settings;
typedef struct q3n_weapon_selection { int32_t weapon, time; } q3n_weapon_selection;
typedef struct q3n_weapon_options {
    qa_q3_product product;
    qa_q3_presentation_assets *assets;
    void *context;
    /* A replacement has emitted its actual selected resources before setting
     * suppressed. Its input is the authored hands and raw local PS. */
    bool (*view_replacement)(void *, const q3n_frame *, const qa_q3_player *,
        const qa_q3_ref_entity *hands, bool *suppressed, qa_error *);
    bool (*held_replacement)(void *, const q3n_frame *, const qa_q3_entity *,
        const qa_q3_ref_entity *torso, bool *suppressed, qa_error *);
    bool (*particle_explosion)(void *, const q3n_frame *, const char *animation,
        qa_vec3 origin, qa_vec3 velocity, int32_t duration, float start_size,
        float end_size, qa_error *);
} q3n_weapon_options;
typedef struct q3n_weapon_view {
    q3n_entity *predicted_entity;
    const qa_q3_entity *predicted_state;
    int32_t bob_cycle, land_time;
    float xy_speed, bob_fraction_sin, land_change;
    bool test_gun;
} q3n_weapon_view;
typedef enum q3n_impact_sound { Q3N_IMPACT_DEFAULT, Q3N_IMPACT_METAL, Q3N_IMPACT_FLESH } q3n_impact_sound;
typedef struct q3n_weapon_drawing {
    void *context;
    bool (*fade_color)(void *,int32_t start,int32_t duration,float color[4],bool *visible,qa_error *);
    bool (*set_color)(void *,const float color_or_null[4],qa_error *);
    bool (*picture)(void *,float x,float y,float width,float height,int32_t shader,qa_error *);
    size_t (*string_length)(void *,const char *);
    bool (*big_string)(void *,int32_t x,int32_t y,const char *,const float color[4],qa_error *);
} q3n_weapon_drawing;
bool q3n_weapons_create(const q3n_weapon_options *, q3n_weapons **, qa_error *);
void q3n_weapons_destroy(q3n_weapons *);
bool q3n_weapons_idle(const q3n_weapons *);
const q3n_weapon_selection *q3n_weapons_selection(const q3n_weapons *);
/* Respawn/pickup explicitly authors cg.weaponSelect without menu ownership
 * checks. Commands use select/cycle against the actual current snapshot PS. */
void q3n_weapons_set_selected(q3n_weapons *, int32_t weapon, int32_t time);
void q3n_weapons_select(const q3n_frame *, int32_t weapon);
void q3n_weapons_cycle(const q3n_frame *, int32_t direction);
bool q3n_weapons_out_of_ammo(const q3n_frame *, qa_error *);
bool q3n_weapons_fire(const q3n_frame *, q3n_entity *, const qa_q3_entity *, qa_error *);
bool q3n_weapons_player(const q3n_frame *, const qa_q3_ref_entity *parent,
    const qa_q3_player *or_null, q3n_entity *, const qa_q3_entity *, qa_error *);
bool q3n_weapons_view(const q3n_frame *, const q3n_weapon_view *, qa_error *);
bool q3n_weapons_draw_selection(const q3n_frame *,const q3n_weapon_drawing *,qa_error *);
bool q3n_weapons_trail(const q3n_frame *, q3n_entity *, const qa_q3_entity *, qa_error *);
bool q3n_weapons_rail(const q3n_frame *, int32_t client, qa_vec3 *start, qa_vec3 end, qa_error *);
bool q3n_weapons_impact(const q3n_frame *, int32_t weapon, int32_t client,
    qa_vec3 origin, qa_vec3 direction, q3n_impact_sound, qa_error *);
bool q3n_weapons_event(void *, const q3n_frame *, q3n_entity *,
    const qa_q3_entity *, int32_t event, qa_vec3 position, qa_error *);
bool q3n_weapons_checkpoint(const q3n_weapons *, qa_buffer *, qa_error *);
bool q3n_weapons_restore(q3n_weapons *, qa_bytes, qa_error *);

#endif
