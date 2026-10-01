#ifndef QA_Q3_NATIVE_WEAPON_H
#define QA_Q3_NATIVE_WEAPON_H

#include "frame.h"
#include "qa/source_save.h"

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
    /* View admission precedes all primary weapon prerequisites. The receiver
     * owns selected hands/pose and consumes only a genuinely admitted selected
     * output or its source-qualified hidden state. Fallback runs once. */
    bool (*view_replacement)(void *, const q3n_frame *, const qa_q3_player *,
        bool *consumed, qa_error *);
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
/* The selected equipment owner retains these actual resources and animation
 * bytes. Handles belong to assets, independently of the primary CGAME. */
typedef struct q3n_selected_weapon_media {
    qa_q3_presentation_assets *assets;
    int32_t gun, hands, barrel, flash, invisibility, battle_weapon, quad_weapon;
} q3n_selected_weapon_media;
typedef struct q3n_selected_weapon_barrel {
    int32_t time;
    float angle;
    bool spinning;
} q3n_selected_weapon_barrel;
typedef struct q3n_selected_weapon_state {
    q3n_lerp_frame torso;
    q3n_selected_weapon_barrel view_barrel, world_barrel;
    uint32_t random_seed;
} q3n_selected_weapon_state;
typedef struct q3n_selected_weapon_draw {
    const qa_q3_player *player;
    int32_t time, last_fire;
    bool firing, has_last_fire, reduced_flashes;
    void *context;
    bool (*current)(void *);
    bool (*submit)(void *, qa_q3_presentation_assets *, const qa_q3_ref_entity *, qa_error *);
} q3n_selected_weapon_draw;
typedef struct q3n_selected_weapon_view {
    const qa_player_animation_config *animations;
    qa_vec3 origin, angles;
    double horizontal_speed;
    int32_t bob_cycle;
    bool draw_gun;
} q3n_selected_weapon_view;
typedef struct q3n_selected_weapon_held {
    const qa_q3_presentation_assets *parent_assets;
    const qa_q3_ref_entity *torso;
    qa_vec3 lighting_origin;
    int32_t powerups;
    bool personal_model;
} q3n_selected_weapon_held;
/* Zero initialization is the real presenter constructor. The outer owner
 * qualifies seat, provider, full actor, immutable animation and registry before
 * this primitive codec. No source PS/S or borrowed pointer is serialized. */
bool q3n_selected_weapon_state_fields(qa_source_save_io *, q3n_selected_weapon_state *);
/* Suppression requires success AND submitted. These entry points do not invoke
 * primary view/held replacement callbacks or manufacture a primary frame. */
bool q3n_weapons_selected_view(q3n_weapons *, const q3n_selected_weapon_media *,
    q3n_selected_weapon_state *, const q3n_selected_weapon_draw *,
    const q3n_selected_weapon_view *, bool *submitted, qa_error *);
bool q3n_weapons_selected_held(q3n_weapons *, const q3n_selected_weapon_media *,
    q3n_selected_weapon_state *, const q3n_selected_weapon_draw *,
    const q3n_selected_weapon_held *, bool *submitted, qa_error *);
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
