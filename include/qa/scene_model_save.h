#ifndef QA_SCENE_MODEL_SAVE_H
#define QA_SCENE_MODEL_SAVE_H
#include "qa/scene.h"

typedef enum qa_scene_model_identity_kind {
    QA_SCENE_MODEL_IDENTITY_MODEL,
    QA_SCENE_MODEL_IDENTITY_MESH,
    QA_SCENE_MODEL_IDENTITY_SHADOW
} qa_scene_model_identity_kind;
bool qa_scene_model_source_resource_bind(qa_scene_model *, const qa_resource *, qa_error *);

typedef struct qa_scene_model_content_lease {
    void *context;
    void (*release)(void *);
    const qa_resource *resource;
} qa_scene_model_content_lease;
typedef enum qa_scene_model_content_kind {
    QA_SCENE_MODEL_CONTENT_SOURCE,
    QA_SCENE_MODEL_CONTENT_REPLACEMENT_SOURCE,
    QA_SCENE_MODEL_CONTENT_ANIMATION
} qa_scene_model_content_kind;
bool qa_scene_model_idle(const qa_scene_model *);
/* Read-only codec observation under an outer aggregate lease. Real nested
 * submissions/checkpoints still reject; this grants no destruction authority. */
bool qa_scene_model_observation_ready(const qa_scene_model *);
typedef struct qa_scene_model_capture qa_scene_model_capture;
/* Holds one genuine root and its complete replacement tree across aggregate
 * collection and component codecs. Mutations/submission/destruction reject
 * this opaque lease; read-only owner checkpoint remains available. */
bool qa_scene_model_capture_begin(const qa_scene_model *, qa_scene_model_capture **, qa_error *);
void qa_scene_model_capture_end(qa_scene_model_capture *);
const qa_model *qa_scene_model_source(const qa_scene_model *);
const qa_scene_image_options *qa_scene_model_image_options(const qa_scene_model *);
qa_scene_resources *qa_scene_model_resource_owner(const qa_scene_model *);
qa_material_library *qa_scene_model_material_owner(const qa_scene_model *);
/* Borrow the actual installed consumer token for fresh source collection.
 * Empty tokens describe ordinary producer-owned content. The caller must not
 * release this borrowed copy; the model remains its destructor owner. */
bool qa_scene_model_content_read(const qa_scene_model *, qa_scene_model_content_kind,
    qa_scene_model_content_lease *);
const qa_scene_mesh *qa_scene_model_mesh_at(const qa_scene_model *, size_t);
/* All results borrow the actual owner; replacement nodes cannot be destroyed
 * separately from their parent. List order is the real retained LRU order. */
const qa_scene_model *qa_scene_model_replacement_first(const qa_scene_model *);
const qa_scene_model *qa_scene_model_replacement_next(const qa_scene_model *);
const qa_model_replacement *qa_scene_model_replacement_description(const qa_scene_model *);
uint64_t qa_scene_model_identity(const qa_scene_model *);
size_t qa_scene_model_shadow_identity_count(const qa_scene_model *);
bool qa_scene_model_shadow_identity_at(const qa_scene_model *, size_t, uint32_t *, uint64_t *);
#endif
