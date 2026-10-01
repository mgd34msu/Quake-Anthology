#ifndef QA_FRONTEND_ROUND_H
#define QA_FRONTEND_ROUND_H
#include "qa/frontend.h"
#include "qa/application_q3_round.h"

const qa_application_q3_round_services *frontend_q3_round_services(void);
bool frontend_round_audio_pending(const qa_frontend *);
bool frontend_source_round_ready(const qa_frontend *, qa_actor_owner, qa_error *);
bool frontend_source_reset_round(qa_frontend *, qa_actor_owner, qa_error *);
void frontend_particle_reset_round(qa_frontend *);
void frontend_event_reset_round(qa_frontend *);
void frontend_audio_retire_round_aliases(qa_frontend *);
void frontend_audio_retire_dead_aliases(qa_frontend *);

#endif
