#ifndef QA_FRONTEND_REMOTE_Q2_FOOTSTEPS_H
#define QA_FRONTEND_REMOTE_Q2_FOOTSTEPS_H
#include "remote_q2_effects.h"
#include "qa/persistence_content.h"
struct frontend_remote_q2;
struct remote_q2_footsteps;
bool remote_q2_footsteps_prepare(struct frontend_remote_q2 *, qa_error *);
void remote_q2_footsteps_clear(struct frontend_remote_q2 *);
bool remote_q2_footstep(void *, const frontend_remote_q2_effects_pose *, uint32_t,
    double, qa_builtin_random *, qa_error *);
bool remote_q2_footsteps_visit(const struct frontend_remote_q2 *, const qa_application_content_visitor *, qa_error *);
bool remote_q2_footsteps_checkpoint(const struct frontend_remote_q2 *, const qa_application_content_graph *, qa_buffer *, qa_error *);
bool remote_q2_footsteps_restore(struct frontend_remote_q2 *, qa_application_content_graph *, qa_bytes, qa_error *);
#endif
