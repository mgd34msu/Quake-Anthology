#ifndef QA_Q3_NATIVE_PARTICLES_H
#define QA_Q3_NATIVE_PARTICLES_H

#include "frame.h"

typedef struct q3n_particles q3n_particles;
bool q3n_particles_create(qa_q3_presentation_assets *, qa_q3_product, q3n_particles **, qa_error *);
bool q3n_particles_create_remote(qa_q3_presentation_assets *, q3n_remote_source *, q3n_particles **, qa_error *);
void q3n_particles_destroy(q3n_particles *);
bool q3n_particles_idle(const q3n_particles *);
/* Selected cg_marks.c profile: 1024 retained slots and 23 explode1 shaders.
 * Real registration precedes frames; same-map resets keep registered handles. */
bool q3n_particles_load(q3n_particles *, qa_application *,
    const qa_application_native_q3_presentation *, int32_t source_time, qa_error *);
/* Genuine entered CG_Init or reached-command clear; no transport replay. */
bool q3n_particles_load_remote(q3n_particles *, const q3n_frame *, qa_error *);
bool q3n_particles_load_unified(q3n_particles *, const q3n_frame *, qa_error *);
void q3n_particles_round(q3n_particles *, int32_t source_time);
bool q3n_particles_explosion(const q3n_frame *, const char *, qa_vec3 origin,
    qa_vec3 velocity, int32_t duration, float start_size, float end_size, qa_error *);
bool q3n_particles_weapon_explosion(void *, const q3n_frame *, const char *,
    qa_vec3, qa_vec3, int32_t, float, float, qa_error *);
/* Actual frame calls after marks and before local entities/buffered audio. */
bool q3n_particles_add(const q3n_frame *, qa_error *);
bool q3n_particles_checkpoint(const q3n_particles *, qa_buffer *, qa_error *);
bool q3n_particles_restore(q3n_particles *, qa_bytes, qa_error *);

#endif
