#ifndef QA_FRONTEND_PARTICLE_AUDIO_H
#define QA_FRONTEND_PARTICLE_AUDIO_H
#include "qa/frontend.h"
#include "qa/builtin.h"
bool frontend_particle_sound(qa_frontend *, const qa_builtin_event *,
    uint32_t seat, qa_actor_id recipient, qa_error *);
#endif
