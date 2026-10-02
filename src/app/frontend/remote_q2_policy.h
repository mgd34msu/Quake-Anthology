#ifndef QA_FRONTEND_REMOTE_Q2_POLICY_H
#define QA_FRONTEND_REMOTE_Q2_POLICY_H
#include "remote_q2_client.h"
typedef struct frontend_remote_q2_image_policy frontend_remote_q2_image_policy;
bool frontend_remote_q2_image_policy_prepare(qa_frontend *, qa_scene_resource_policy *const *,
    size_t, frontend_remote_q2_image_policy **, qa_error *);
bool frontend_remote_q2_image_policy_ready(frontend_remote_q2_image_policy *, qa_error *);
bool frontend_remote_q2_image_policy_ready_is(const frontend_remote_q2_image_policy *);
void frontend_remote_q2_image_policy_publish(frontend_remote_q2_image_policy *);
bool frontend_remote_q2_image_policy_finish(frontend_remote_q2_image_policy **, qa_error *);
bool frontend_remote_q2_image_policy_abort(frontend_remote_q2_image_policy **, qa_error *);
#endif
