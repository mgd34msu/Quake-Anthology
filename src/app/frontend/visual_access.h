#ifndef QA_FRONTEND_VISUAL_ACCESS_H
#define QA_FRONTEND_VISUAL_ACCESS_H

#include "visual_restore.h"
#include "qa/q3_model_opening.h"

/* Live media admission through the actual selected provider. Returned holders
 * borrow the physical appearance cache; restore uses imported inventories. */
bool frontend_visual_model_acquire(qa_frontend *, qa_actor_owner provider,
    qa_game_family, const char *path, const qa_resource *retained_source,
    frontend_visual_model_view *, qa_error *);
bool frontend_visual_model_admission(void *, qa_application *,
    const qa_application_model_admission_request *, qa_application_model_admission *, qa_error *);
bool frontend_visual_media_acquire(qa_frontend *, qa_actor_owner provider,
    qa_game_family, frontend_visual_owner_view *, qa_error *);
/* Pure rendering read of a previously admitted physical media owner. */
bool frontend_visual_media_read(const qa_frontend *, qa_actor_owner provider,
    qa_game_family, frontend_visual_owner_view *, qa_error *);
bool frontend_visual_scene_model_source_read(const qa_scene_model *, qa_scene_model_content_kind,
    frontend_model_source *);
bool frontend_visual_scene_animation_source_read(const qa_scene_model *, frontend_animation_source *);
bool frontend_visual_scene_model_content_clone(const qa_scene_model *, qa_scene_model_content_kind,
    qa_scene_model_content_lease *, qa_error *);
struct frontend_model_policy;
typedef struct frontend_visual_policy frontend_visual_policy;
typedef struct frontend_visual_policy_binding {
    qa_scene_model *model;
    qa_scene_model_image_policy *ticket;
} frontend_visual_policy_binding;
bool frontend_visual_policy_prepare(qa_frontend *, const struct frontend_model_policy *,
    const frontend_visual_policy_binding *, size_t, frontend_visual_policy **, qa_error *);
bool frontend_visual_registered_model_policy_prepare(qa_frontend *, const struct frontend_model_policy *,
    qa_q3_presentation_assets *const *, size_t, const frontend_visual_policy_binding *, size_t, qa_error *);
/* The actual registry calls this after native decode, before publishing the
 * holder. Its first acquisition receipt and registered parent remain owned. */
bool frontend_visual_registered_model_initialize(qa_frontend *, const qa_q3_model_opening *,
    const qa_model *, qa_scene_model *, qa_error *);
/* Native CLIENT caches retain this exact acquisition, parsed parent and scene
 * through destruction of its replacement children. No registry is synthesized. */
bool frontend_visual_model_opening_initialize(qa_frontend *, qa_game_family, qa_vfs *,
    const qa_resource *, const qa_vfs_acquisition *, const qa_model *, qa_scene_model *, qa_error *);
bool frontend_visual_policy_ready(frontend_visual_policy *, qa_error *);
bool frontend_visual_policy_ready_is(const frontend_visual_policy *);
void frontend_visual_policy_publish(frontend_visual_policy *);
bool frontend_visual_policy_finish(frontend_visual_policy **, qa_error *);
bool frontend_visual_policy_abort(frontend_visual_policy **, qa_error *);

#endif
