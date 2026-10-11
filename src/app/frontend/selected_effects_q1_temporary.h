#ifndef QA_FRONTEND_SELECTED_EFFECTS_Q1_TEMPORARY_H
#define QA_FRONTEND_SELECTED_EFFECTS_Q1_TEMPORARY_H
#include "selected_effects_particles.h"

typedef struct frontend_fx_q1_light_recipe {
    float radius, decay, minimum;
    double duration;
    qa_vec3 color;
} frontend_fx_q1_light_recipe;
enum { FRONTEND_FX_Q1_LIGHTS = 64, FRONTEND_FX_Q1_BEAMS = 32 };
typedef struct frontend_fx_q1_light {
    qa_actor_id actor;
    uint32_t source_entity;
    qa_vec3 origin, color;
    double born, die;
    float radius, decay, minimum;
    uint64_t identity;
    bool active;
} frontend_fx_q1_light;
typedef struct frontend_fx_q1_beam {
    qa_actor_id actor;
    uint32_t source_entity, roll_seed;
    qa_vec3 start, end;
    double die;
    uint8_t type;
    bool active;
} frontend_fx_q1_beam;
typedef struct frontend_fx_q1_state {
    frontend_fx_particles particles;
    frontend_fx_q1_light lights[FRONTEND_FX_Q1_LIGHTS];
    frontend_fx_q1_beam beams[FRONTEND_FX_Q1_BEAMS];
    size_t light_capacity, beam_capacity;
} frontend_fx_q1_state;
void frontend_fx_q1_state_initialize(frontend_fx_q1_state *, size_t lights, size_t beams);
frontend_fx_q1_light *frontend_fx_q1_state_light(frontend_fx_q1_state *, qa_actor_id,
    uint32_t source_entity, qa_vec3 origin, double seconds, const frontend_fx_q1_light_recipe *);
bool frontend_fx_q1_state_beam(frontend_fx_q1_state *, qa_actor_id,
    bool received, const qa_q1_temp *, double seconds, uint32_t roll_seed);
bool frontend_fx_q1_state_temporary(frontend_fx_q1_state *, qa_builtin_random *,
    const qa_q1_temp *, qa_actor_id, bool received, bool quakeworld, double seconds,
    const char **sound);
bool frontend_fx_q1_temporary_light(const qa_q1_temp *, frontend_fx_q1_light_recipe *);
const char *frontend_fx_q1_temporary_sound(const qa_q1_temp *, qa_builtin_random *);
int frontend_fx_q1_model_trail(uint32_t flags, bool quakeworld);
bool frontend_fx_q1_entity_light(qa_builtin_random *, qa_vec3 origin, qa_vec3 angles,
    uint32_t effects, uint32_t model_flags, bool quakeworld, bool rerelease, double seconds,
    qa_vec3 *light_origin, frontend_fx_q1_light_recipe *);
int frontend_fx_q1_beam_index(uint8_t type);
const char *frontend_fx_q1_beam_model(uint8_t type);
typedef struct frontend_fx_q1_beam_cursor {
    qa_vec3 origin, direction, angles;
    float remaining;
} frontend_fx_q1_beam_cursor;
void frontend_fx_q1_beam_begin(frontend_fx_q1_beam_cursor *, qa_vec3 start, qa_vec3 end);
bool frontend_fx_q1_beam_next(frontend_fx_q1_beam_cursor *, qa_builtin_random *, qa_model_transform *);
bool frontend_fx_q1_entity_effects(frontend_fx_particles *, qa_builtin_random *,
    qa_vec3 start, qa_vec3 origin, qa_vec3 angles, uint32_t effects, uint32_t model_flags,
    bool quakeworld, bool rerelease, double seconds, qa_vec3 *light_origin,
    frontend_fx_q1_light_recipe *);

#endif
