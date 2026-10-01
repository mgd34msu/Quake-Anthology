#ifndef QA_SCENE_MODEL_SAVE_H
#define QA_SCENE_MODEL_SAVE_H
#include "qa/scene.h"

typedef enum qa_scene_model_identity_kind {
    QA_SCENE_MODEL_IDENTITY_MODEL,
    QA_SCENE_MODEL_IDENTITY_MESH,
    QA_SCENE_MODEL_IDENTITY_SHADOW
} qa_scene_model_identity_kind;
typedef struct qa_scene_model_saved_identity {
    qa_scene_model_identity_kind kind;
    size_t node, ordinal;
    uint64_t saved;
} qa_scene_model_saved_identity;
typedef struct qa_scene_model_content_lease {
    void *context;
    void (*release)(void *);
} qa_scene_model_content_lease;
typedef enum qa_scene_model_content_kind {
    QA_SCENE_MODEL_CONTENT_SOURCE,
    QA_SCENE_MODEL_CONTENT_REPLACEMENT_SOURCE,
    QA_SCENE_MODEL_CONTENT_ANIMATION
} qa_scene_model_content_kind;
typedef struct qa_scene_model_owner_refs {
    void *context;
    bool (*model_encode)(void *, const qa_model *, uint64_t *, qa_error *);
    /* Candidate decoded holders are owned by the enclosing content inventory.
     * Their original resource provenance and immutable parsed arrays must be
     * qualified there. The byte span here is the exact original source. */
    bool (*model_decode)(void *, uint64_t, qa_bytes, const qa_model **, qa_error *);
    bool (*animation_encode)(void *, const qa_model_animation *, uint64_t *, qa_error *);
    /* Qualification includes the actual installed scale/joint policy and
     * resulting immutable poses, not only the original animation bytes. */
    bool (*animation_decode)(void *, uint64_t, qa_bytes, const qa_model_animation **, qa_error *);
    /* Restore acquires stable per-consumer holds, independent of this temporary
     * resolver context. A successful callback fills both fields of an empty
     * lease. The model releases them after dependent children are destroyed. */
    bool (*model_retain)(void *, const qa_model *, qa_scene_model_content_lease *, qa_error *);
    bool (*animation_retain)(void *, const qa_model_animation *, qa_scene_model_content_lease *, qa_error *);
    bool (*source_qualify)(void *, const qa_model *, qa_scene_resources *,
        qa_material_library *, const qa_scene_image_options *, qa_error *);
    bool (*geometry_encode)(void *, const qa_scene_geometry *, uint64_t *, qa_error *);
    bool (*geometry_decode)(void *, uint64_t, const qa_scene_geometry **, qa_error *);
    bool (*image_encode)(void *, const qa_scene_image *, uint64_t *, qa_error *);
    bool (*image_decode)(void *, uint64_t, const qa_scene_image **, qa_error *);
    bool (*material_encode)(void *, const qa_material *, uint64_t *, qa_error *);
    bool (*material_decode)(void *, uint64_t, const qa_material **, qa_error *);
    /* Uses the preallocated namespace shared with material and frame imports.
     * This callback must not mint identities or dispatch source services. */
    bool (*identity_decode)(void *, qa_scene_model_identity_kind, size_t node,
        size_t ordinal, uint64_t saved, uint64_t *installed, qa_error *);
    /* Optional canonical capture mapping at the same physical node/ordinal.
     * NULL encodes the actual identity. It never mutates installed nodes. */
    bool (*identity_encode)(void *, qa_scene_model_identity_kind, size_t node,
        size_t ordinal, uint64_t installed, uint64_t *saved, qa_error *);
} qa_scene_model_owner_refs;

bool qa_scene_model_owner_checkpoint(const qa_scene_model *, const qa_scene_model_owner_refs *,
    qa_buffer *, qa_error *);
/* Bind a detached owner to genuine qualified content/image/material/geometry
 * holders. No model builder, image load, registration or animation runs.
 * Stable content leases retain decoded models/animations through their actual
 * consumers; resource/material resolver owners outlive the scene. The output
 * must be empty and is unchanged on failure. */
bool qa_scene_model_owner_restore(const qa_model *qualified_source, qa_scene_resources *,
    qa_material_library *, qa_bytes, const qa_scene_model_owner_refs *, qa_scene_model **, qa_error *);
/* Read only the saved namespace prefix before material/frame import. Its rows
 * become authoritative only after full owner restore verifies every row
 * against the actual decoded graph. Returned storage is freed with free(). */
bool qa_scene_model_owner_identities_read(qa_bytes, qa_scene_model_saved_identity **,
    size_t *, qa_error *);

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
