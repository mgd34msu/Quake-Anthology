#ifndef QA_Q3_NATIVE_EFFECTS_H
#define QA_Q3_NATIVE_EFFECTS_H

#include "local_entities.h"

typedef struct q3n_smoke {
    qa_vec3 origin, velocity;
    float radius, color[4], duration;
    int32_t start_time, fade_in_time, flags, shader;
} q3n_smoke;
typedef struct q3n_explosion {
    qa_vec3 origin, direction;
    bool has_direction, sprite;
    int32_t model, shader, duration;
} q3n_explosion;
q3n_local_entity *q3n_effect_smoke(const q3n_frame *, const q3n_smoke *);
bool q3n_effect_bubbles(const q3n_frame *, qa_vec3 start, qa_vec3 end, float spacing, qa_error *);
q3n_local_entity *q3n_effect_explosion(const q3n_frame *, const q3n_explosion *, qa_error *);
void q3n_effect_spawn(const q3n_frame *, qa_vec3);
void q3n_effect_bleed(const q3n_frame *, qa_vec3, int32_t physical_client);
q3n_local_entity *q3n_effect_bleed_entity(const q3n_frame *,qa_vec3);
q3n_local_entity *q3n_effect_gib(const q3n_frame *, qa_vec3, qa_vec3 velocity, int32_t model);
void q3n_effect_gib_player(const q3n_frame *, qa_vec3);
void q3n_effect_big_explode(const q3n_frame *, qa_vec3);
void q3n_effect_score(const q3n_frame *, int32_t client, qa_vec3, int32_t score);
bool q3n_effect_mission(const q3n_frame *, int32_t event, qa_vec3, qa_vec3 angles_or_endpoint, qa_error *);

#endif
