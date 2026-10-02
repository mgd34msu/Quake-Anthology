#ifndef QA_FRONTEND_REMOTE_Q1_SKY_POLICY_H
#define QA_FRONTEND_REMOTE_Q1_SKY_POLICY_H
#include "remote_q1_client.h"
typedef struct frontend_remote_q1_sky_policy frontend_remote_q1_sky_policy;
bool frontend_remote_q1_sky_policy_prepare(qa_frontend *, qa_scene_resource_policy *const *, size_t,
    frontend_remote_q1_sky_policy **, qa_error *);
bool frontend_remote_q1_sky_policy_ready(frontend_remote_q1_sky_policy *, qa_error *);
bool frontend_remote_q1_sky_policy_ready_is(const frontend_remote_q1_sky_policy *);
void frontend_remote_q1_sky_policy_publish(frontend_remote_q1_sky_policy *);
bool frontend_remote_q1_sky_policy_finish(frontend_remote_q1_sky_policy **, qa_error *);
bool frontend_remote_q1_sky_policy_abort(frontend_remote_q1_sky_policy **, qa_error *);
#endif
