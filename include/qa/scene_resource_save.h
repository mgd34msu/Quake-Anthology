#ifndef QA_SCENE_RESOURCE_SAVE_H
#define QA_SCENE_RESOURCE_SAVE_H
#include "qa/scene.h"

typedef struct qa_scene_resource_checkpoint_refs {
    void *context;
    bool (*image_encode)(void *, const qa_scene_image *, uint64_t *, qa_error *);
    bool (*image_decode)(void *, uint64_t, const qa_scene_image **, qa_error *);
} qa_scene_resource_checkpoint_refs;
/* Actual borrowed content view, unchanged by observation. Its lifetime is
 * supplied by the resource owner's existing constructor/caller. */
qa_vfs *qa_scene_resources_files(const qa_scene_resources *);
/* Immutable image versions must already be restored in the qualified owner.
 * Candidate VFS mount order is the enclosing content owner's saved order.
 * Restoring this layer replaces builtin image references and an empty cache;
 * it never decodes images or advances animation. */
bool qa_scene_resources_checkpoint(const qa_scene_resources *, const qa_scene_resource_checkpoint_refs *,
                                    qa_buffer *, qa_error *);
bool qa_scene_resources_restore(qa_scene_resources *, qa_bytes, const qa_scene_resource_checkpoint_refs *, qa_error *);
#endif
