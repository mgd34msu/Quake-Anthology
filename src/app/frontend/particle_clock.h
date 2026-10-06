#ifndef QA_FRONTEND_PARTICLE_CLOCK_H
#define QA_FRONTEND_PARTICLE_CLOCK_H

#include "qa/frontend.h"
#include "qa/application_native_q2_client.h"

/* The committed transformed host duration precedes GAME execution. Complete
 * clamps the retained Classic client clock before source effect delivery. */
bool frontend_particle_source_begin(qa_frontend *, uint64_t elapsed_ns, qa_error *);
bool frontend_particle_source_complete(qa_frontend *, qa_error *);

/* Borrow the already completed local CLIENT clock for this emitting GAME. */
bool frontend_particle_q2_client_time(qa_frontend *, qa_actor_owner,
    double *seconds, bool *found, qa_error *);

/* Local CLIENT interpolation reads the same completed Source samples as effects. */
bool frontend_particle_q2_player_sample(qa_frontend *, uint32_t seat, qa_actor_id,
    qa_application_native_q2_player_sample *, uint32_t *old_gun_frame,
    float *back_lerp, bool *found, qa_error *);
bool frontend_particle_q2_entity_sample(qa_frontend *, qa_application_visual_view *,
    float *back_lerp, qa_error *);

#endif
