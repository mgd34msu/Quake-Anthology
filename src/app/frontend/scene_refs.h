#ifndef QA_FRONTEND_SCENE_REFS_H
#define QA_FRONTEND_SCENE_REFS_H
#include "scene_identity.h"
#include "model_inventory.h"
#include "qa/q3_assets_save.h"

/* The owner ordinal is the real root's shared namespace row. This context is
 * borrowed only during component codecs; retained content tokens never borrow
 * it, the namespace or the construction graph. */
typedef struct frontend_scene_model_scope {
    frontend_scene_identity_scope identity;
    frontend_model_inventory *models;
} frontend_scene_model_scope;
qa_scene_model_owner_refs frontend_scene_model_refs(frontend_scene_model_scope *);
/* Q3's aggregate adapter forwards its genuine parsed-holder context here. */
bool frontend_scene_q3_model_retain(void *, const qa_model *, qa_q3_asset_model_lease *, qa_error *);
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
