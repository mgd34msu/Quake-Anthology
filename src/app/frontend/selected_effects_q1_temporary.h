#ifndef QA_FRONTEND_SELECTED_EFFECTS_Q1_TEMPORARY_H
#define QA_FRONTEND_SELECTED_EFFECTS_Q1_TEMPORARY_H
#include "selected_effects_particles.h"

typedef struct frontend_fx_q1_light_recipe {
    float radius, decay, minimum;
    double duration;
    qa_vec3 color;
} frontend_fx_q1_light_recipe;
bool frontend_fx_q1_temporary_light(const qa_q1_temp *, frontend_fx_q1_light_recipe *);
const char *frontend_fx_q1_temporary_sound(const qa_q1_temp *, qa_builtin_random *);
int frontend_fx_q1_model_trail(uint32_t flags, bool quakeworld);
bool frontend_fx_q1_entity_light(qa_builtin_random *, qa_vec3 origin, qa_vec3 angles,
    uint32_t effects, uint32_t model_flags, bool quakeworld, bool rerelease, double seconds,
    qa_vec3 *light_origin, frontend_fx_q1_light_recipe *);
const char *frontend_fx_q1_beam_model(uint8_t type);
typedef struct frontend_fx_q1_beam_cursor {
    qa_vec3 origin, direction, angles;
    float remaining;
} frontend_fx_q1_beam_cursor;
void frontend_fx_q1_beam_begin(frontend_fx_q1_beam_cursor *, qa_vec3 start, qa_vec3 end);
bool frontend_fx_q1_beam_next(frontend_fx_q1_beam_cursor *, qa_builtin_random *, qa_model_transform *);
#endif
