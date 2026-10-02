#ifndef QA_FRONTEND_RESOURCE_BINDINGS_H
#define QA_FRONTEND_RESOURCE_BINDINGS_H
#include "internal.h"

typedef struct frontend_native_q2_image_policy frontend_native_q2_image_policy;
typedef struct frontend_event_image_policy frontend_event_image_policy;
/* Borrow an already admitted physical Q1 event bank; never create resources. */
bool frontend_event_q1_images_read(const qa_frontend *, qa_actor_owner,
    qa_scene_resources **, qa_vfs **, qa_error *);
/* These children own actual cached picture and physical sky bindings. The
 * complete prepared banks outlive their checked disposal. */
bool frontend_native_q2_image_policy_prepare(qa_frontend *, qa_scene_resource_policy *const *, size_t,
    frontend_native_q2_image_policy **, qa_error *);
bool frontend_native_q2_image_policy_ready(frontend_native_q2_image_policy *, qa_error *);
bool frontend_native_q2_image_policy_ready_is(const frontend_native_q2_image_policy *);
void frontend_native_q2_image_policy_publish(frontend_native_q2_image_policy *);
bool frontend_native_q2_image_policy_finish(frontend_native_q2_image_policy **, qa_error *);
bool frontend_native_q2_image_policy_abort(frontend_native_q2_image_policy **, qa_error *);
bool frontend_event_image_policy_prepare(qa_frontend *, qa_scene_resource_policy *const *, size_t,
    frontend_event_image_policy **, qa_error *);
bool frontend_event_image_policy_ready(frontend_event_image_policy *, qa_error *);
bool frontend_event_image_policy_ready_is(const frontend_event_image_policy *);
void frontend_event_image_policy_publish(frontend_event_image_policy *);
bool frontend_event_image_policy_finish(frontend_event_image_policy **, qa_error *);
bool frontend_event_image_policy_abort(frontend_event_image_policy **, qa_error *);
#endif
