#ifndef QA_FRONTEND_SCENE_IDENTITY_H
#define QA_FRONTEND_SCENE_IDENTITY_H
#include "qa/frontend.h"
#include "qa/scene_world_save.h"
#include "qa/scene_model_save.h"
#include "qa/scene_frame_save.h"
#include "qa/scene_save.h"

typedef struct frontend_scene_namespace frontend_scene_namespace;
typedef struct frontend_scene_identity_scope {
    frontend_scene_namespace *space;
    uint64_t owner;
} frontend_scene_identity_scope;

/* The aggregate holds its actual frontend/content lifetime lease throughout
 * collection, all component codecs, qualification and disposal. Owner keys
 * are the aggregate's qualified physical owner ordinals, shared by capture
 * and candidate preparation. They must be nonzero and never pointer casts.
 * Readonly world/model traversal permits the aggregate's root capture tokens
 * while rejecting actual submissions or nested component checkpoints. It
 * grants no permission to mutate or destroy those protected owners. */
bool frontend_scene_namespace_create(frontend_scene_namespace **, qa_error *);
void frontend_scene_namespace_destroy(frontend_scene_namespace *);
bool frontend_scene_namespace_capture_images(frontend_scene_namespace *, qa_frontend *, qa_error *);
bool frontend_scene_namespace_capture_library(frontend_scene_namespace *, uint64_t owner,
    const qa_material_library *, qa_error *);
bool frontend_scene_namespace_capture_world(frontend_scene_namespace *, uint64_t owner,
    const qa_scene_world *, qa_error *);
bool frontend_scene_namespace_capture_model(frontend_scene_namespace *, uint64_t owner,
    const qa_scene_model *, qa_error *);
bool frontend_scene_namespace_capture_light(frontend_scene_namespace *, uint64_t owner,
    size_t ordinal, uint64_t identity, qa_error *);
/* Actual ambient-sound producer keys share the global numeric allocation
 * domain but are never mesh, image or light references. The ordinal is the
 * real event owner's static-sound traversal, independent of seat playback. */
bool frontend_scene_namespace_capture_static_audio(frontend_scene_namespace *, uint64_t owner,
    size_t ordinal, uint64_t key, qa_error *);
/* Images, libraries and genuine mesh/light producers precede their frames. */
bool frontend_scene_namespace_capture_frame(frontend_scene_namespace *, uint64_t owner,
    const qa_scene_frame *, qa_error *);
/* Retained renderer cache rows can survive their source world/model/frame.
 * Capture these genuine physical holders after those producer inventories;
 * existing pointers preserve their earlier shared row instead of duplicating
 * geometry storage. Import qualifies the actual renderer row before seal. */
bool frontend_scene_namespace_capture_renderer_geometry(frontend_scene_namespace *,
    uint64_t owner, size_t ordinal, const qa_scene_geometry *, qa_error *);
bool frontend_scene_namespace_qualify_renderer_geometry(frontend_scene_namespace *,
    uint64_t owner, size_t ordinal, const qa_scene_geometry *, qa_error *);
bool frontend_scene_namespace_seal(frontend_scene_namespace *, qa_error *);
bool frontend_scene_namespace_checkpoint(const frontend_scene_namespace *, qa_buffer *, qa_error *);
/* A final fresh capture follows the same real physical owner rows. Qualify
 * every installed pointer/domain/ordinal against the imported dictionary,
 * then use its original identities only in this new capture's encoded fields. */
bool frontend_scene_namespace_rebase_capture(frontend_scene_namespace *fresh,
    const frontend_scene_namespace *installed, qa_error *);
bool frontend_scene_world_saved(void *, qa_scene_world_identity_kind, size_t ordinal,
    uint64_t installed, uint64_t *saved, qa_error *);
bool frontend_scene_model_saved(void *, qa_scene_model_identity_kind, size_t node,
    size_t ordinal, uint64_t installed, uint64_t *saved, qa_error *);
bool frontend_scene_light_saved(frontend_scene_identity_scope *, size_t ordinal,
    uint64_t installed, uint64_t *saved, qa_error *);
bool frontend_scene_static_audio_saved(frontend_scene_identity_scope *, size_t ordinal,
    uint64_t installed, uint64_t *saved, qa_error *);

/* Imports the complete shared prefix and immutable geometry allocations.
 * The genuine restored image inventory must have the same physical order.
 * Numeric scene IDs are reserved here, before any resolver runs. Geometry and
 * image construction references belong to this dictionary until consumers
 * retain them. Materials and frames borrow the aggregate's actual owners. */
bool frontend_scene_namespace_restore(qa_bytes, const qa_scene_image_set *,
    frontend_scene_namespace **, qa_error *);
bool frontend_scene_namespace_bind_library(frontend_scene_namespace *, uint64_t owner,
    const qa_material_library *, qa_error *);
bool frontend_scene_namespace_bind_frame(frontend_scene_namespace *, uint64_t owner,
    const qa_scene_frame *, qa_error *);
/* Call after the full respective owner codec succeeds, before final seal. */
bool frontend_scene_namespace_qualify_world(frontend_scene_namespace *, uint64_t owner,
    const qa_scene_world *, qa_error *);
bool frontend_scene_namespace_qualify_model(frontend_scene_namespace *, uint64_t owner,
    const qa_scene_model *, qa_error *);
bool frontend_scene_namespace_qualify_light(frontend_scene_namespace *, uint64_t owner,
    size_t ordinal, uint64_t installed, qa_error *);
bool frontend_scene_namespace_qualify_static_audio(frontend_scene_namespace *, uint64_t owner,
    size_t ordinal, uint64_t installed, qa_error *);
bool frontend_scene_namespace_qualify_frame(frontend_scene_namespace *, uint64_t owner,
    const qa_scene_frame *, qa_error *);
bool frontend_scene_light_install(frontend_scene_identity_scope *, size_t ordinal,
    uint64_t saved, uint64_t *, qa_error *);
bool frontend_scene_static_audio_install(frontend_scene_identity_scope *, size_t ordinal,
    uint64_t saved, uint64_t *, qa_error *);
/* Pure producer membership checks use the saved identity during capture and
 * the installed identity during restore. They accept neither a different
 * physical owner/ordinal nor a different numeric domain, and never qualify
 * a producer, reserve IDs or change preparation state. */
bool frontend_scene_light_owner_ready(const frontend_scene_identity_scope *, size_t ordinal,
    uint64_t actual, qa_error *);
bool frontend_scene_static_audio_owner_ready(const frontend_scene_identity_scope *, size_t ordinal,
    uint64_t actual, qa_error *);

/* Resolver context is the dictionary except for the scoped owner callbacks.
 * They only inspect rows. A missing or wrong-kind reference always fails. */
bool frontend_scene_image_encode(void *, const qa_scene_image *, uint64_t *, qa_error *);
bool frontend_scene_image_decode(void *, uint64_t, const qa_scene_image **, qa_error *);
bool frontend_scene_geometry_encode(void *, const qa_scene_geometry *, uint64_t *, qa_error *);
bool frontend_scene_geometry_decode(void *, uint64_t, const qa_scene_geometry **, qa_error *);
bool frontend_scene_material_encode(void *, const qa_material *, uint64_t *, qa_error *);
bool frontend_scene_material_decode(void *, uint64_t, const qa_material **, qa_error *);
bool frontend_scene_material_mutable_decode(void *, uint64_t, qa_material **, qa_error *);
bool frontend_scene_frame_encode(void *, const qa_scene_frame *, uint64_t *, qa_error *);
bool frontend_scene_frame_decode(void *, uint64_t, const qa_scene_frame **, qa_error *);
bool frontend_scene_image_identity_encode(void *, uint64_t, uint64_t *, qa_error *);
bool frontend_scene_image_identity_decode(void *, uint64_t, uint64_t *, qa_error *);
bool frontend_scene_world_identity_encode(void *, uint64_t, uint64_t *, qa_error *);
bool frontend_scene_world_identity_decode(void *, uint64_t, uint64_t *, qa_error *);
bool frontend_scene_mesh_identity_encode(void *, uint64_t, uint64_t *, qa_error *);
bool frontend_scene_mesh_identity_decode(void *, uint64_t, uint64_t *, qa_error *);
bool frontend_scene_light_identity_encode(void *, uint64_t, uint64_t *, qa_error *);
bool frontend_scene_light_identity_decode(void *, uint64_t, uint64_t *, qa_error *);
bool frontend_scene_world_install(void *, qa_scene_world_identity_kind, size_t ordinal,
    uint64_t saved, uint64_t *, qa_error *);
bool frontend_scene_model_install(void *, qa_scene_model_identity_kind, size_t node,
    size_t ordinal, uint64_t saved, uint64_t *, qa_error *);
#endif
