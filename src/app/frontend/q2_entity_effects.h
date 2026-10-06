#ifndef QA_FRONTEND_Q2_ENTITY_EFFECTS_H
#define QA_FRONTEND_Q2_ENTITY_EFFECTS_H
#include "selected_effects_particles.h"
#include "qa/collision.h"

typedef struct frontend_q2_entity_pose {
    qa_actor_id actor;
    uint32_t number, event, model_index;
    uint64_t model_identity;
    uint64_t effects;
    int32_t frame;
    qa_vec3 origin, angles;
    qa_bounds bounds;
    float radius, scale;
    bool bounds_present, model_present;
} frontend_q2_entity_pose;
typedef struct frontend_q2_entity_trail {
    qa_actor_id actor;
    qa_vec3 origin;
    int32_t count;
    double fly_end;
    float flashlight_fraction;
    uint32_t model_index;
    uint64_t model_identity, sample_frame;
} frontend_q2_entity_trail;
typedef struct frontend_q2_entity_cache {
    frontend_q2_entity_trail *rows;
    size_t capacity;
} frontend_q2_entity_cache;
typedef struct frontend_q2_entity_effect_view {
    double milliseconds;
    qa_scene_view view;
    qa_actor_id viewer;
    int32_t hand;
    bool per_pixel_lighting;
    float frame_seconds;
} frontend_q2_entity_effect_view;
typedef struct frontend_q2_entity_effects {
    frontend_fx_particles *particles;
    qa_builtin_random *random;
    bool rerelease;
    uint32_t disable_particles, dlight_hacks;
    void *context;
    void (*light)(void *, const qa_scene_light *);
    bool (*trace)(void *, const qa_trace_query *, qa_trace_result *, qa_error *);
} frontend_q2_entity_effects;

void frontend_q2_effect_particles_initialize(frontend_fx_particles *, qa_builtin_random *, bool rerelease);
qa_vec3 frontend_q2_effect_random_direction(qa_builtin_random *, bool rerelease);
bool frontend_q2_entity_cache_reserve(frontend_q2_entity_cache *, uint32_t actor_slot, qa_error *);
void frontend_q2_entity_frame_particles(frontend_q2_entity_effects *, const frontend_q2_entity_pose *, double milliseconds);
bool frontend_q2_entity_effect(frontend_q2_entity_effects *, const frontend_q2_entity_effect_view *,
    const frontend_q2_entity_pose *, frontend_q2_entity_trail *, bool advance, qa_error *);
bool frontend_q2_entity_beam(qa_builtin_random *, qa_bytes palette, const qa_scene_image *,
    const qa_scene_view *, qa_vec3, qa_vec3, uint32_t packed_colors, int32_t width,
    qa_scene_frame *, qa_error *);
#endif
