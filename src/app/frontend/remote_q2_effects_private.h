#ifndef QA_FRONTEND_REMOTE_Q2_EFFECTS_PRIVATE_H
#define QA_FRONTEND_REMOTE_Q2_EFFECTS_PRIVATE_H
#include "remote_q2_effects.h"
#include "selected_effects_particles.h"

enum { Q2FX_POOL = 32, Q2FX_LIGHT_CAPACITY = 32 };
typedef enum q2fx_model {
    Q2FX_EXPLODE, Q2FX_SMOKE, Q2FX_FLASH, Q2FX_PARASITE, Q2FX_CABLE,
    Q2FX_ROCKET, Q2FX_BFG, Q2FX_LIGHTNING, Q2FX_HEAT, Q2FX_BIG,
    Q2FX_MUZZLE_MACHINE, Q2FX_MUZZLE_SSHOTGUN, Q2FX_MUZZLE_SHOTGUN,
    Q2FX_MUZZLE_ROCKET, Q2FX_MUZZLE_RAIL, Q2FX_MUZZLE_LAUNCH,
    Q2FX_MUZZLE_ETF, Q2FX_MUZZLE_DIST, Q2FX_MUZZLE_BOOMER,
    Q2FX_MUZZLE_BLAST, Q2FX_MUZZLE_BFG, Q2FX_MUZZLE_BEAM,
    Q2FX_MODEL_COUNT
} q2fx_model;
typedef struct q2fx_explosion {
    bool active;
    uint8_t kind, model;
    int32_t frames, base, skin;
    uint32_t flags;
    qa_vec3 origin, angles, light_color;
    double start;
    float light, scale;
} q2fx_explosion;
typedef struct q2fx_beam {
    bool active, player, monster, unkeyed;
    uint8_t model;
    qa_actor_id actor, destination;
    qa_vec3 start, end, offset;
    double die, sound_until;
} q2fx_beam;
typedef struct q2fx_laser {
    bool active;
    qa_vec3 start, end;
    double die;
    uint32_t color;
    float width;
} q2fx_laser;
typedef struct q2fx_light {
    bool active;
    qa_actor_id actor;
    qa_vec3 origin, color;
    double born, die;
    float radius, decay, minimum;
} q2fx_light;
typedef struct q2fx_sustain {
    bool active;
    uint8_t kind;
    int32_t id, count, color, magnitude;
    qa_vec3 origin, direction;
    double end, next;
} q2fx_sustain;
typedef struct q2fx_trail {
    qa_actor_id actor;
    qa_vec3 origin;
    int32_t count;
    double fly_end;
    float flashlight_fraction;
} q2fx_trail;
typedef struct q2fx_model_draw {
    uint8_t model;
    qa_vec3 origin, angles;
    int32_t frame, old_frame, skin;
    uint32_t flags;
    float alpha, back_lerp;
    qa_vec3 scale;
} q2fx_model_draw;
typedef struct q2fx_weapon_muzzle {
    bool active;
    uint8_t model;
    qa_actor_id actor;
    qa_vec3 offset;
    float roll, scale;
    double start;
} q2fx_weapon_muzzle;
struct frontend_remote_q2_effects {
    frontend_remote_q2_effects_source source;
    frontend_remote_q2_effects_policy *pending;
    unsigned busy;
    qa_builtin_random random;
    frontend_fx_particles particles;
    const qa_scene_image *particle_image;
    qa_scene_model *models[Q2FX_MODEL_COUNT];
    bool model_admitted[Q2FX_MODEL_COUNT];
    q2fx_explosion explosions[Q2FX_POOL];
    q2fx_beam beams[Q2FX_POOL], player_beams[Q2FX_POOL];
    q2fx_laser lasers[Q2FX_POOL];
    q2fx_light lights[Q2FX_POOL];
    q2fx_sustain sustains[Q2FX_POOL];
    q2fx_trail *trails;
    size_t trail_count;
    size_t sampled_particle_count;
    qa_scene_light sampled_lights[Q2FX_LIGHT_CAPACITY];
    size_t light_count;
    q2fx_model_draw *draws;
    size_t draw_count, draw_capacity;
    double time, server_time;
    uint64_t frame_sequence;
    bool sampled, dirty;
    bool event_received, event_failed;
    uint64_t event_sequence;
    qa_error event_error;
    q2fx_weapon_muzzle weapon_muzzle;
    uint32_t sampled_dlight_hacks, sampled_disable_particles;
    uint32_t slow_bin, slow_base, slow_seed;
    uint64_t slow_frame, render_frame;
};
extern const char *const q2fx_model_paths[Q2FX_MODEL_COUNT];
bool q2fx_fail(qa_error *, qa_status, const char *);
bool q2fx_source_valid(const frontend_remote_q2_effects_source *);
bool q2fx_source_current(const frontend_remote_q2_effects *, qa_error *);
bool q2fx_controls(frontend_remote_q2_effects *, frontend_remote_q2_effects_controls *, qa_error *);
bool q2fx_frame_milliseconds(frontend_remote_q2_effects *, double *, qa_error *);
qa_vec3 q2fx_random_direction(frontend_remote_q2_effects *);
bool q2fx_model_admit(frontend_remote_q2_effects *, q2fx_model, qa_error *);
bool q2fx_state_fields(qa_source_save_io *, frontend_remote_q2_effects *, const frontend_remote_q2_effects_refs *);
bool q2fx_entities(frontend_remote_q2_effects *, const frontend_remote_q2_effects_sample *,
    q2fx_trail *, bool advance, qa_error *);
void q2fx_sampled_light(frontend_remote_q2_effects *, qa_vec3, float, qa_vec3, float);
#endif
