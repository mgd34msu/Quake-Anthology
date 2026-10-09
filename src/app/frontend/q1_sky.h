#ifndef QA_FRONTEND_Q1_SKY_H
#define QA_FRONTEND_Q1_SKY_H
#include "qa/frontend.h"
#include "qa/scene.h"

typedef struct frontend_q1_sky frontend_q1_sky;
typedef struct frontend_q1_sky_policy frontend_q1_sky_policy;
typedef struct frontend_q1_sky_controls {
    qa_cvar_handle fast, quality, alpha, fog, far_clip;
} frontend_q1_sky_controls;
typedef struct frontend_q1_sky_view {
    bool classic_q1, boxed, fast;
    float quality, alpha, fog, far_clip;
    const qa_scene_image *images[6];
} frontend_q1_sky_view;

void frontend_q1_sky_controls_bind(const qa_cvars *, frontend_q1_sky_controls *);
bool frontend_q1_sky_controls_read(const qa_cvars *, const frontend_q1_sky_controls *,
    qa_scene_q1_sky_environment *, uint64_t *fog_modification);
bool frontend_q1_sky_create(qa_frontend *, frontend_q1_sky **, qa_error *);
bool frontend_q1_sky_destroy(frontend_q1_sky **, qa_error *);
bool frontend_q1_sky_idle(const frontend_q1_sky *);
/* The global map is its real geometry publication; services retain their
 * own provider bank and complete recipient generation. */
bool frontend_q1_sky_map(frontend_q1_sky *, qa_error *);
bool frontend_q1_sky_receive(frontend_q1_sky *, qa_actor_owner, qa_actor_id,
    const char *, qa_error *);
bool frontend_q1_sky_command(frontend_q1_sky *, const char *, qa_error *);
bool frontend_q1_sky_name(const frontend_q1_sky *, qa_actor_id, const char **, qa_error *);
bool frontend_q1_sky_view_read(frontend_q1_sky *, qa_actor_id,
    frontend_q1_sky_view *, qa_error *);
bool frontend_q1_sky_retire_provider(frontend_q1_sky *, qa_actor_owner, qa_error *);
bool frontend_q1_sky_retire_actor(frontend_q1_sky *, qa_actor_id, qa_error *);

bool frontend_q1_sky_policy_prepare(frontend_q1_sky *,
    qa_scene_resource_policy *const *, size_t, frontend_q1_sky_policy **, qa_error *);
bool frontend_q1_sky_policy_ready(frontend_q1_sky_policy *, qa_error *);
bool frontend_q1_sky_policy_ready_is(const frontend_q1_sky_policy *);
void frontend_q1_sky_policy_publish(frontend_q1_sky_policy *);
bool frontend_q1_sky_policy_finish(frontend_q1_sky_policy **, qa_error *);
bool frontend_q1_sky_policy_abort(frontend_q1_sky_policy **, qa_error *);
#endif
