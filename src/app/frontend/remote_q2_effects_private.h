#ifndef QA_FRONTEND_REMOTE_Q2_EFFECTS_PRIVATE_H
#define QA_FRONTEND_REMOTE_Q2_EFFECTS_PRIVATE_H
#include "remote_q2_effects.h"
#include "selected_effects_particles.h"
#include "q2_temporary_beams.h"

enum { Q2FX_POOL = 32, Q2FX_LASER_CAPACITY = 256, Q2FX_LIGHT_CAPACITY = 32 };
typedef struct q2fx_explosion {
    bool active;
    uint8_t kind, model;
    int32_t frames, base, skin;
    uint32_t flags;
    qa_vec3 origin, angles, light_color;
    double start;
    float light, scale;
} q2fx_explosion;
typedef frontend_q2_temporary_beam q2fx_beam;
typedef struct q2fx_laser {
    bool active;
    qa_vec3 start, end;
    double born, die;
    uint32_t color, rgba;
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

typedef frontend_q2_beam_draw q2fx_model_draw;
typedef struct q2fx_weapon_muzzle {
    bool active;
    uint8_t model;
    qa_actor_id actor;
    qa_vec3 offset;
    float roll, scale;
    double start;
} q2fx_weapon_muzzle;
typedef struct q2fx_source_beam {
    q2fx_beam beam;
} q2fx_source_beam;
typedef struct q2fx_source_light {
    frontend_remote_q2_effects_shadow_light light;
    uint64_t identity, revision;
    bool shadow;
} q2fx_source_light;
typedef struct q2fx_flashlight {
    qa_actor_id actor;
    uint64_t identity;
    int32_t hand;
} q2fx_flashlight;
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
    q2fx_laser lasers[Q2FX_LASER_CAPACITY];
    q2fx_light lights[Q2FX_POOL];
    q2fx_sustain sustains[Q2FX_POOL];
    qa_arena semantic_storage;
    frontend_q2_entity_cache entity_trails;
    size_t sampled_particle_count;
    qa_scene_light *sampled_lights;
    size_t light_count, light_capacity, transient_light_count;
    q2fx_source_beam *source_beams;
    size_t source_beam_count, source_beam_capacity;
    q2fx_source_light *source_lights;
    size_t source_light_count, source_light_capacity;
    q2fx_flashlight *flashlights;
    size_t flashlight_count, flashlight_capacity;
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
    int32_t sampled_gun;
    float sampled_gun_fov;
    frontend_q2_beam_random beam_random;
    uint64_t render_frame;
};
bool q2fx_fail(qa_error *, qa_status, const char *);
bool q2fx_source_valid(const frontend_remote_q2_effects_source *);
bool q2fx_source_current(const frontend_remote_q2_effects *, qa_error *);
bool q2fx_controls(frontend_remote_q2_effects *, frontend_remote_q2_effects_controls *, qa_error *);
bool q2fx_frame_milliseconds(frontend_remote_q2_effects *, double *, qa_error *);
qa_vec3 q2fx_random_direction(frontend_remote_q2_effects *);
bool q2fx_model_admit(frontend_remote_q2_effects *, q2fx_model, qa_error *);
bool q2fx_semantic_prepare(frontend_remote_q2_effects *, const frontend_remote_q2_effects_sample *,
    const frontend_remote_q2_effects_controls *, bool, qa_error *);
bool q2fx_prepare_beams(frontend_remote_q2_effects *, q2fx_beam *, size_t,
    const frontend_remote_q2_effects_sample *, const frontend_remote_q2_effects_controls *, bool, qa_error *);
bool q2fx_entities(frontend_remote_q2_effects *, const frontend_remote_q2_effects_sample *,
    bool advance, qa_error *);
void q2fx_sampled_light(frontend_remote_q2_effects *, qa_vec3, float, qa_vec3, float);
#endif
