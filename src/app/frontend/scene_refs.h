#ifndef QA_FRONTEND_SCENE_REFS_H
#define QA_FRONTEND_SCENE_REFS_H
#include "scene_identity.h"
#include "model_inventory.h"

/* Qualify only genuine tokens installed by this frontend. Empty or foreign
 * tokens are not source authority; ordinary cache/registry producers provide
 * their own exact resource rows to the enclosing collector. */
bool frontend_scene_model_source_read(const qa_scene_model *, qa_scene_model_content_kind,
    frontend_model_source *);
bool frontend_scene_animation_source_read(const qa_scene_model *, frontend_animation_source *);
/* Copies the real root's owning content token without reopening its source. */
bool frontend_scene_model_content_clone(const qa_scene_model *, qa_scene_model_content_kind,
    qa_scene_model_content_lease *, qa_error *);
#endif
