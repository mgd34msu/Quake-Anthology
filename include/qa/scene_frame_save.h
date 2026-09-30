#ifndef QA_SCENE_FRAME_SAVE_H
#define QA_SCENE_FRAME_SAVE_H
#include "qa/scene.h"
typedef struct qa_scene_frame_checkpoint_refs {
    void *context;
    bool (*image_encode)(void *, const qa_scene_image *, uint64_t *, qa_error *);
    bool (*image_decode)(void *, uint64_t, const qa_scene_image **, qa_error *);
    bool (*geometry_encode)(void *, const qa_scene_geometry *, uint64_t *, qa_error *);
    bool (*geometry_decode)(void *, uint64_t, const qa_scene_geometry **, qa_error *);
    bool (*material_encode)(void *, const qa_material *, uint64_t *, qa_error *);
    bool (*material_decode)(void *, uint64_t, const qa_material **, qa_error *);
    bool (*mesh_identity_encode)(void *, uint64_t, uint64_t *, qa_error *);
    bool (*mesh_identity_decode)(void *, uint64_t, uint64_t *, qa_error *);
    bool (*light_identity_encode)(void *, uint64_t, uint64_t *, qa_error *);
    bool (*light_identity_decode)(void *, uint64_t, uint64_t *, qa_error *);
} qa_scene_frame_checkpoint_refs;
/* The owning frontend has returned from all producers and consuming backends.
 * Resolvers qualify actual image/geometry/material and identity dictionaries.
 * Restore keeps the installed frame address and material-order binding; all
 * transient command storage is decoded into a detached arena before publish. */
bool qa_scene_frame_checkpoint(const qa_scene_frame *, const qa_scene_frame_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_scene_frame_restore(qa_scene_frame *, qa_bytes, const qa_scene_frame_checkpoint_refs *, qa_error *);
#endif
