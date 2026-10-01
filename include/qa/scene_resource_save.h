#ifndef QA_SCENE_RESOURCE_SAVE_H
#define QA_SCENE_RESOURCE_SAVE_H
#include "qa/scene.h"

typedef struct qa_scene_resource_checkpoint_refs {
    void *context;
    bool (*image_encode)(void *, const qa_scene_image *, uint64_t *, qa_error *);
    bool (*image_decode)(void *, uint64_t, const qa_scene_image **, qa_error *);
    bool (*resource_encode)(void *, const qa_resource *, uint64_t *pool, uint64_t *version, qa_error *);
    bool (*resource_decode)(void *, uint64_t pool, uint64_t version, const qa_resource **, qa_error *);
} qa_scene_resource_checkpoint_refs;
typedef struct qa_scene_resources_capture qa_scene_resources_capture;
/* Creates only the genuine owner/name table. Saved image versions and QARS
 * install builtin references and policy later; no procedural image is made. */
qa_scene_resources *qa_scene_resources_create_detached(qa_vfs *, qa_error *);
/* Holds the actual owner and every currently installed image version across
 * aggregate collection and callbacks. The caller keeps the opaque token;
 * ordinary image/cache/policy admission and destruction reject this lease. */
bool qa_scene_resources_capture_begin(const qa_scene_resources *, qa_scene_resources_capture **, qa_error *);
void qa_scene_resources_capture_end(qa_scene_resources_capture *);
bool qa_scene_resources_idle(const qa_scene_resources *);
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
